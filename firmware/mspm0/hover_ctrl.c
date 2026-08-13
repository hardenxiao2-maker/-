#include "hover_ctrl.h"
#include "hc_sr04.h"
#include "PWM.h"
#include "ti_msp_dl_config.h"
#include "board.h"
#include <string.h>

// PID 控制器内部状态。
// kp/ki/kd 来自主程序 HOVER_TUNE，error_prev 和 integral 是运行时历史量。
typedef struct {
    float kp;
    float ki;
    float kd;
    float error_prev;
    float integral;
    float integral_max;
} HoverPID_t;

// 系统运行状态变量：目标距离、当前距离、风机 PWM 和启动状态都保存在这里。
static HoverPID_t s_pid;
static float s_target_height = 50.0f; // 默认目标悬停高度为 50cm (符合 30~70cm 设定范围)
static float s_current_height = 0.0f;
static int s_fan_pwm = 0;
static bool s_system_active = false;  // 系统默认关闭，等待按键3启动
static uint32_t s_last_tick_us = 0;   // 用于动态计算 PID dt
// 距离校准表指针。表本体放在 empty.c，控制模块只保存地址。
static const HoverDistanceCalPoint_t *s_cal_points = 0;
static uint8_t s_cal_count = 0;

static HoverTempCalTable_t s_temp_tables[HOVER_MAX_TEMP_TABLES];
static uint8_t s_temp_table_count = 0;
static HoverTempCalTable_t s_draft_table;
static bool s_draft_active = false;

/*
 * 超声波安装在乒乓球上方，测到的是“传感器到球”的距离。
 * 此时测量值和目标值均转换为“传感器到球心”的距离。
 * 测距值变大表示球变低，需要增大风机 PWM；测距值变小表示球变高，需要减小 PWM。
 * 因此该宏设为 1，PID 计算误差为：error = s_current_height - s_target_height。
 */
#define HOVER_ULTRASONIC_MOUNT_TOP (1)

// 控制模块兜底默认参数。正常运行时会被 empty.c 中的 HOVER_TUNE 覆盖。
static HoverDebugConfig_t s_cfg = {
    .kp = 22.0f,
    .ki = 1.5f,
    .kd = 6.0f,
    .integral_max = 200.0f,
    .base_pwm = 350.0f,
    .pwm_min = 0.0f,
    .pwm_max = 950.0f,
    .safe_pwm = 150.0f,
    .filter_alpha = 0.35f,
    .target_default = 50.0f,
    .target_min = 30.0f,
    .target_max = 70.0f,
};

static float interpolate_distance(const HoverDistanceCalPoint_t *a,
                                  const HoverDistanceCalPoint_t *b,
                                  float measured_cm)
{
    // 在两个相邻校准点之间做线性插值，保证修正后的距离连续不跳变。
    float span = b->measured_cm - a->measured_cm;
    if (span > -0.001f && span < 0.001f) {
        return a->true_cm;
    }

    float ratio = (measured_cm - a->measured_cm) / span;
    return a->true_cm + ratio * (b->true_cm - a->true_cm);
}

static bool validate_point_table(const HoverDistanceCalPoint_t *points, uint8_t count)
{
    if (points == 0 || count < 2u || count > HOVER_MAX_CAL_POINTS) {
        return false;
    }

    for (uint8_t i = 1u; i < count; i++) {
        if (points[i].measured_cm <= points[i - 1u].measured_cm) {
            return false;
        }
    }
    return true;
}

static float calibrate_with_table(const HoverDistanceCalPoint_t *points,
                                  uint8_t count,
                                  float measured_cm)
{
    if (!validate_point_table(points, count)) {
        return measured_cm;
    }

    if (measured_cm <= points[0].measured_cm) {
        return interpolate_distance(&points[0], &points[1], measured_cm);
    }

    for (uint8_t i = 1u; i < count; i++) {
        if (measured_cm <= points[i].measured_cm) {
            return interpolate_distance(&points[i - 1u], &points[i], measured_cm);
        }
    }

    return interpolate_distance(&points[count - 2u], &points[count - 1u], measured_cm);
}

static float calibrate_distance(float measured_cm)
{
    if (s_temp_table_count >= 1u) {
        float temp_c = HC_SR04_GetTemperatureC();

        if (temp_c <= s_temp_tables[0].temperature_c || s_temp_table_count == 1u) {
            return calibrate_with_table(s_temp_tables[0].points,
                                        s_temp_tables[0].count,
                                        measured_cm);
        }

        for (uint8_t i = 1u; i < s_temp_table_count; i++) {
            if (temp_c <= s_temp_tables[i].temperature_c) {
                const HoverTempCalTable_t *low = &s_temp_tables[i - 1u];
                const HoverTempCalTable_t *high = &s_temp_tables[i];
                float low_value = calibrate_with_table(low->points, low->count, measured_cm);
                float high_value = calibrate_with_table(high->points, high->count, measured_cm);
                float span = high->temperature_c - low->temperature_c;
                if (span > -0.001f && span < 0.001f) {
                    return low_value;
                }
                float ratio = (temp_c - low->temperature_c) / span;
                return low_value + ratio * (high_value - low_value);
            }
        }

        const HoverTempCalTable_t *last = &s_temp_tables[s_temp_table_count - 1u];
        return calibrate_with_table(last->points, last->count, measured_cm);
    }

    // 没有有效校准表时，直接返回原始测距值。
    if (s_cal_points == 0 || s_cal_count < 2) {
        return measured_cm;
    }

    // 小于第一个特征点时，使用前两个点外推。
    if (measured_cm <= s_cal_points[0].measured_cm) {
        return interpolate_distance(&s_cal_points[0], &s_cal_points[1], measured_cm);
    }

    // 找到 measured_cm 所在区间，用该区间两端特征点插值。
    for (uint8_t i = 1; i < s_cal_count; i++) {
        if (measured_cm <= s_cal_points[i].measured_cm) {
            return interpolate_distance(&s_cal_points[i - 1], &s_cal_points[i], measured_cm);
        }
    }

    // 大于最后一个特征点时，使用最后两个点外推。
    return interpolate_distance(&s_cal_points[s_cal_count - 2],
                                &s_cal_points[s_cal_count - 1],
                                measured_cm);
}

void Hover_Init(void)
{
    // 初始化 PID、目标值、测距值、风机输出和系统状态。
    s_pid.kp = s_cfg.kp;
    s_pid.ki = s_cfg.ki;
    s_pid.kd = s_cfg.kd;
    s_pid.error_prev = 0.0f;
    s_pid.integral = 0.0f;
    s_pid.integral_max = s_cfg.integral_max;

    s_target_height = s_cfg.target_default;
    s_current_height = 0.0f;
    s_fan_pwm = 0;
    s_system_active = false;
    s_last_tick_us = 0;

    // 初始状态关闭风机
    PWM_SetCompare1(0);
    PWM_SetCompare2(0);
}

void Hover_ApplyDistanceCalibration(const HoverDistanceCalPoint_t *points, uint8_t count)
{
    // 少于两个点无法插值，认为不启用校准。
    if (points == 0 || count < 2) {
        s_cal_points = 0;
        s_cal_count = 0;
        return;
    }

    // 校准表要求 measured_cm 严格递增。填错时直接禁用校准。
    for (uint8_t i = 1; i < count; i++) {
        if (points[i].measured_cm <= points[i - 1].measured_cm) {
            s_cal_points = 0;
            s_cal_count = 0;
            return;
        }
    }

    // 更换校准表后清空滤波和 PID 历史量，避免旧数据影响新标定。
    s_cal_points = points;
    s_cal_count = count;
    s_current_height = 0.0f;
    s_pid.integral = 0.0f;
    s_pid.error_prev = 0.0f;
}

bool Hover_ApplyTemperatureCalibrationTables(const HoverTempCalTable_t *tables, uint8_t count)
{
    if (tables == 0 || count == 0u || count > HOVER_MAX_TEMP_TABLES) {
        return false;
    }

    for (uint8_t i = 0u; i < count; i++) {
        if (!validate_point_table(tables[i].points, tables[i].count)) {
            return false;
        }
        if (i > 0u && tables[i].temperature_c <= tables[i - 1u].temperature_c) {
            return false;
        }
    }

    memcpy(s_temp_tables, tables, sizeof(HoverTempCalTable_t) * count);
    s_temp_table_count = count;
    s_current_height = 0.0f;
    s_pid.integral = 0.0f;
    s_pid.error_prev = 0.0f;
    return true;
}

bool Hover_CalibrationBegin(float temperature_c)
{
    memset(&s_draft_table, 0, sizeof(s_draft_table));
    s_draft_table.temperature_c = temperature_c;
    s_draft_active = true;
    return true;
}

bool Hover_CalibrationSetPoint(uint8_t index, float measured_cm, float true_cm)
{
    if (!s_draft_active || index >= HOVER_MAX_CAL_POINTS) {
        return false;
    }
    s_draft_table.points[index].measured_cm = measured_cm;
    s_draft_table.points[index].true_cm = true_cm;
    if ((uint8_t)(index + 1u) > s_draft_table.count) {
        s_draft_table.count = (uint8_t)(index + 1u);
    }
    return true;
}

bool Hover_CalibrationApply(void)
{
    if (!s_draft_active || !validate_point_table(s_draft_table.points, s_draft_table.count)) {
        return false;
    }

    int replace_index = -1;
    for (uint8_t i = 0u; i < s_temp_table_count; i++) {
        float diff = s_temp_tables[i].temperature_c - s_draft_table.temperature_c;
        if (diff > -0.05f && diff < 0.05f) {
            replace_index = (int)i;
            break;
        }
    }

    if (replace_index >= 0) {
        s_temp_tables[replace_index] = s_draft_table;
    } else {
        if (s_temp_table_count >= HOVER_MAX_TEMP_TABLES) {
            return false;
        }
        s_temp_tables[s_temp_table_count++] = s_draft_table;
    }

    for (uint8_t i = 0u; i + 1u < s_temp_table_count; i++) {
        for (uint8_t j = (uint8_t)(i + 1u); j < s_temp_table_count; j++) {
            if (s_temp_tables[j].temperature_c < s_temp_tables[i].temperature_c) {
                HoverTempCalTable_t tmp = s_temp_tables[i];
                s_temp_tables[i] = s_temp_tables[j];
                s_temp_tables[j] = tmp;
            }
        }
    }

    s_draft_active = false;
    s_current_height = 0.0f;
    s_pid.integral = 0.0f;
    s_pid.error_prev = 0.0f;
    return true;
}

void Hover_ApplyConfig(const HoverDebugConfig_t *cfg)
{
    // cfg 为空时保持当前参数不变。
    if (cfg == 0) {
        return;
    }

    s_cfg = *cfg;

    // 目标范围填反时自动交换，防止按键限幅逻辑失效。
    if (s_cfg.target_min > s_cfg.target_max) {
        float tmp = s_cfg.target_min;
        s_cfg.target_min = s_cfg.target_max;
        s_cfg.target_max = tmp;
    }
    // 默认目标距离必须落在允许范围内。
    if (s_cfg.target_default < s_cfg.target_min) {
        s_cfg.target_default = s_cfg.target_min;
    } else if (s_cfg.target_default > s_cfg.target_max) {
        s_cfg.target_default = s_cfg.target_max;
    }

    // PWM 限幅必须在 0~1000 内，和 TIMA1 的 PWM 周期保持一致。
    if (s_cfg.pwm_min < 0.0f) {
        s_cfg.pwm_min = 0.0f;
    }
    if (s_cfg.pwm_max > 1000.0f) {
        s_cfg.pwm_max = 1000.0f;
    }
    if (s_cfg.pwm_min > s_cfg.pwm_max) {
        float tmp = s_cfg.pwm_min;
        s_cfg.pwm_min = s_cfg.pwm_max;
        s_cfg.pwm_max = tmp;
    }

    // 安全风速也限制在 PWM 输出范围内。
    if (s_cfg.safe_pwm < s_cfg.pwm_min) {
        s_cfg.safe_pwm = s_cfg.pwm_min;
    } else if (s_cfg.safe_pwm > s_cfg.pwm_max) {
        s_cfg.safe_pwm = s_cfg.pwm_max;
    }

    // 滤波系数限制在可用范围内，避免配置错误导致距离不更新或发散。
    if (s_cfg.filter_alpha < 0.01f) {
        s_cfg.filter_alpha = 0.01f;
    } else if (s_cfg.filter_alpha > 1.0f) {
        s_cfg.filter_alpha = 1.0f;
    }

    // 新参数生效时清空积分和微分历史，避免旧累计量带入新参数。
    s_pid.kp = s_cfg.kp;
    s_pid.ki = s_cfg.ki;
    s_pid.kd = s_cfg.kd;
    s_pid.integral_max = s_cfg.integral_max;
    s_pid.integral = 0.0f;
    s_pid.error_prev = 0.0f;
    if (s_target_height < s_cfg.target_min) {
        s_target_height = s_cfg.target_min;
    } else if (s_target_height > s_cfg.target_max) {
        s_target_height = s_cfg.target_max;
    }
}

void Hover_GetConfig(HoverDebugConfig_t *cfg)
{
    if (cfg != 0) {
        *cfg = s_cfg;
    }
}

void Hover_DecreaseTarget(float step)
{
    s_target_height -= step;
    if (s_target_height < s_cfg.target_min) {
        s_target_height = s_cfg.target_min;
    }
}

void Hover_IncreaseTarget(float step)
{
    s_target_height += step;
    if (s_target_height > s_cfg.target_max) {
        s_target_height = s_cfg.target_max;
    }
}

void Hover_SetTargetHeight(float target_cm)
{
    if (target_cm < s_cfg.target_min) {
        target_cm = s_cfg.target_min;
    } else if (target_cm > s_cfg.target_max) {
        target_cm = s_cfg.target_max;
    }
    s_target_height = target_cm;
}

void Hover_SetActive(bool active)
{
    if (s_system_active == active) {
        return;
    }

    s_system_active = active;

    // 启停时清空 PID 历史量，让网页/串口远程操作和本地按键行为一致。
    s_pid.integral = 0.0f;
    s_pid.error_prev = 0.0f;

    if (!s_system_active) {
        s_fan_pwm = 0;
        PWM_SetCompare1(0);
        PWM_SetCompare2(0);
    }
}

void Hover_ToggleActive(void)
{
    Hover_SetActive(!s_system_active);
}

void Hover_Control_Tick(void)
{
    // 计算真实的 dt (秒)
    extern uint32_t micros(void);
    uint32_t now = micros();
    float dt = 0.05f;
    if (s_last_tick_us != 0) {
        dt = (float)(now - s_last_tick_us) / 1000000.0f;
    }
    s_last_tick_us = now;
    // 限制合理范围，防止调试暂停或首次运行导致 dt 异常
    if (dt <= 0.001f || dt > 0.5f) {
        dt = 0.05f;
    }

    // 1. 读取超声波传感器的当前距离数据 (无论系统是否启动，都进行测距以支持静态测量)
    uint32_t pulse_us = 0;
    float dist = HC_SR04_GetDistance(&pulse_us);
    if (dist >= 0.0f) {
        // 将测得的“传感器到小球表面的物理距离”转换为“小球球心到超声波传感器的测量距离”
        // 乒乓球直径为 4 cm，半径为 2 cm。
        // 传感器到球心的距离 = 超声波到球表面的距离 + 球半径 2.0
        dist = dist + 2.0f;
        // 然后使用标定表把测得的球心距离修正为真实的球心距离，再进入滤波和 PID。
        dist = calibrate_distance(dist);
        if (s_current_height <= 0.0f) {
            s_current_height = dist; // 首次测得有效值直接赋值，防止慢启动
        } else {
            // 一阶低通滤波 (EMA)，用于压制超声波跳变噪声。
            s_current_height = s_cfg.filter_alpha * dist + (1.0f - s_cfg.filter_alpha) * s_current_height;
        }
    }

    // 2. 如果系统未启动，则风机不工作，直接返回
    if (!s_system_active) {
        PWM_SetCompare1(0);
        PWM_SetCompare2(0);
        s_fan_pwm = 0;
        return;
    }

    // 3. 如果测距发生错误，为了安全降低风速，避免乒乓球飞出
    if (dist < 0.0f) {
        s_fan_pwm = (int)s_cfg.safe_pwm;
        PWM_SetCompare1(s_fan_pwm);
        PWM_SetCompare2(s_fan_pwm);
        return;
    }

    /*
     * 4. PID 算法计算 (使用实测的 dt)
     * 上置超声波：current > target 表示球偏低，应该加大 PWM，所以误差取
     * current - target。下置超声波则相反，距离越大表示球越高。
     */
#if HOVER_ULTRASONIC_MOUNT_TOP
    float error = s_current_height - s_target_height;
#else
    float error = s_target_height - s_current_height;
#endif

    // 误差积分累加：用于消除导槽倾角、风机推力偏差导致的长期静差。
    s_pid.integral += error * dt;
    if (s_pid.integral > s_pid.integral_max) {
        s_pid.integral = s_pid.integral_max;
    } else if (s_pid.integral < -s_pid.integral_max) {
        s_pid.integral = -s_pid.integral_max;
    }

    // P 负责快速响应，I 负责消除静差，D 负责抑制冲过目标点。
    float p_out = s_pid.kp * error;
    float i_out = s_pid.ki * s_pid.integral;
    float d_out = s_pid.kd * (error - s_pid.error_prev) / dt;
    s_pid.error_prev = error;

    // 总控制输出 = 基础重力补偿占空比 + PID 输出
    float output = s_cfg.base_pwm + p_out + i_out + d_out;

    // 限幅输出占空比，避免风机过载或输出无效负值。
    if (output > s_cfg.pwm_max) {
        output = s_cfg.pwm_max;
    } else if (output < s_cfg.pwm_min) {
        output = s_cfg.pwm_min;
    }

    s_fan_pwm = (int)output;

    // 5. 更新风机 PWM 驱动输出
    PWM_SetCompare1(s_fan_pwm);
    PWM_SetCompare2(s_fan_pwm);
}

float Hover_GetTargetHeight(void)
{
    return s_target_height;
}

float Hover_GetCurrentHeight(void)
{
    return s_current_height;
}

int Hover_GetFanPWM(void)
{
    return s_fan_pwm;
}

bool Hover_IsActive(void)
{
    return s_system_active;
}
