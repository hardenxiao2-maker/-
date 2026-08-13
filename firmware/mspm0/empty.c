#include "ti/devices/msp/m0p/mspm0g350x.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "ti/driverlib/dl_gpio.h"
#include "ti_msp_dl_config.h"
#include "board.h"
#include "debug_oled.h"
#include "fan_rpm.h"
#include "hc_sr04.h"
#include "hover_ctrl.h"
#include "oled.h"
#include "wifi_remote.h"

/*
 * 主程序说明
 * ---------------------------------------------------------------------------
 * 本文件只保留系统初始化、按键扫描、周期任务调度和 OLED 页面选择。
 * 复杂功能已经拆到独立模块：
 *   hover_ctrl.c   : PID 闭环控制、目标距离、测距校准表、运行参数
 *   hc_sr04.c      : 超声波测距和温度声速补偿
 *   wifi_remote.c  : App/WiFi/蓝牙串口透传命令解析
 *   fan_rpm.c      : PB7 风机测速脉冲计数和 RPM 换算
 *   debug_oled.c   : 临时 OLED 通信调试页面
 *
 * 这样主循环保持简洁，后续继续加功能时优先封装到模块里。
 */

/*
 * OLED_REMOTE_DEBUG_MODE = 1:
 *   OLED 显示远程通信调试页，用来确认 App 的 CFG/CAL/RUN 等命令
 *   是否已经通过 ESP32 透传模块进入单片机。
 *
 * OLED_REMOTE_DEBUG_MODE = 0:
 *   OLED 恢复比赛正常显示页，显示状态、目标距离、实际距离、PWM/RPM。
 */
#define OLED_REMOTE_DEBUG_MODE (0)

/*
 * SysTick 每 1ms 进入一次中断。
 * ms_ticks 给 App 遥测和低精度计时使用；micros() 用 SysTick 当前计数值
 * 拼出微秒级时间戳，供控制周期和 HC-SR04 回波计时使用。
 */
volatile uint32_t ms_ticks = 0;

/*
 * 调试温度参数，单位摄氏度。
 * HC_SR04_SetTemperatureC() 会使用 v = 331.4 + 0.6*T 修正声速。
 * App 端发送 CFG,TEMP,xx 后，也会在运行时覆盖这个温度值。
 */
static float DEBUG_TEMPERATURE_C = 25.0f;

/*
 * PID 和控制参数默认值。
 * App 参数页中的 Kp/Ki/Kd/base_pwm/pwm_min/pwm_max/safe_pwm/filter_alpha
 * 对应的就是这里的字段。单片机断电后会恢复这里的默认值；App 连接后会
 * 自动把手机本地保存的调试值重新下发给单片机。
 */
static const HoverDebugConfig_t HOVER_TUNE = {
    .kp = 3.6f,
    .ki = 2.0f,
    .kd = 0.05f,
    .integral_max = 200.0f,
    .base_pwm = 450.0f,
    .pwm_min = 0.0f,
    .pwm_max = 1000.0f,
    .safe_pwm = 450.0f,
    .filter_alpha = 0.78f,
    .target_default = 50.0f,
    .target_min = 30.0f,
    .target_max = 70.0f,
};

/*
 * 多温度测距校准表。
 * 每张表的 points[] 格式为：
 *   {超声波测得距离 measured_cm, 实际真实距离 true_cm}
 *
 * 当前先填等值表作为默认占位。实际标定时，可以在 App 校准页输入每个
 * 特征点的实测值，App 会通过 CAL,TEMP / CAL,POINT / CAL,APPLY 在线
 * 更新到单片机运行内存中。
 *
 * hover_ctrl.c 会先在同一温度表内做分段线性插值；如果当前温度位于两张
 * 温度表之间，再对两张表的结果做温度方向插值。
 */
static const HoverTempCalTable_t HOVER_TEMP_CAL_TABLES[] = {
    {
        .temperature_c = 20.0f,
        .count = 11,
        .points = {
            {23.3f, 20.0f}, {27.4f, 25.0f}, {32.0f, 30.0f},
            {38.0f, 35.0f}, {43.4f, 40.0f}, {46.5f, 45.0f},
            {51.0f, 50.0f}, {55.2f, 55.0f}, {60.4f, 60.0f},
            {65.3f, 65.0f}, {70.3f, 70.0f},
        },
    },
    {
        .temperature_c = 25.0f,
        .count = 11,
        .points = {
            {23.3f, 20.0f}, {27.4f, 25.0f}, {32.0f, 30.0f},
            {38.0f, 35.0f}, {43.4f, 40.0f}, {46.5f, 45.0f},
            {51.0f, 50.0f}, {55.2f, 55.0f}, {60.4f, 60.0f},
            {65.3f, 65.0f}, {70.3f, 70.0f},
        },
    },
    {
        .temperature_c = 30.0f,
        .count = 11,
        .points = {
            {23.3f, 20.0f}, {27.4f, 25.0f}, {32.0f, 30.0f},
            {38.0f, 35.0f}, {43.4f, 40.0f}, {46.5f, 45.0f},
            {51.0f, 50.0f}, {55.2f, 55.0f}, {60.4f, 60.0f},
            {65.3f, 65.0f}, {70.3f, 70.0f},
        },
    },
};

/* 实体按键和 App UP/DOWN 命令的默认调节步长。 */
static const float HEIGHT_STEP_CM = 5.0f;

/* PID 控制周期：50ms，即 20Hz。 */
static const uint32_t CONTROL_PERIOD_US = 50000u;

/* OLED 刷新周期：200ms，即 5Hz。 */
static const uint32_t OLED_PERIOD_US = 200000u;

void SysTick_Handler(void)
{
    ms_ticks++;
}

uint32_t micros(void)
{
    uint32_t ms;
    uint32_t val;

    do {
        ms = ms_ticks;
        val = SysTick->VAL;
    } while (ms != ms_ticks);

    /* SysTick 重装值为 32000，系统时钟 32MHz，约 32 个计数为 1us。 */
    return ms * 1000u + (32000u - val) / 32u;
}

static void ApplyButtons(void)
{
    /*
     * 三个按键均按“低电平为按下”处理，并用 s_btn*_active 做简单边沿检测。
     * 这样长按不会在主循环中连续触发很多次，只在刚按下那一刻触发一次。
     */
    static bool s_btn1_active = false;
    static bool s_btn2_active = false;
    static bool s_btn3_active = false;

    /* PB25：目标距离减小一个步长。 */
    if (DL_GPIO_readPins(GPIOB, DL_GPIO_PIN_25) == 0) {
        if (!s_btn1_active) {
            s_btn1_active = true;
            Hover_DecreaseTarget(HEIGHT_STEP_CM);
        }
    } else {
        s_btn1_active = false;
    }

    /* PA29：目标距离增加一个步长。 */
    if (DL_GPIO_readPins(GPIOA, DL_GPIO_PIN_29) == 0) {
        if (!s_btn2_active) {
            s_btn2_active = true;
            Hover_IncreaseTarget(HEIGHT_STEP_CM);
        }
    } else {
        s_btn2_active = false;
    }

    /* PB26：切换 ACTIVE/STANDBY。 */
    if (DL_GPIO_readPins(GPIOB, DL_GPIO_PIN_26) == 0) {
        if (!s_btn3_active) {
            s_btn3_active = true;
            Hover_ToggleActive();
        }
    } else {
        s_btn3_active = false;
    }
}

static void ShowNormalOledPage(void)
{
    char buf[64];
    HoverDebugConfig_t cfg;
    Hover_GetConfig(&cfg);

    OLED_Clear();
    OLED_ShowString(0, 0,
        (u8*)(Hover_IsActive() ? "State: ACTIVE" : "State: STANDBY"),
        16, 1);

    sprintf(buf, "Target: %.1f cm", Hover_GetTargetHeight());
    OLED_ShowString(0, 16, (u8*)buf, 16, 1);

    float current_h = Hover_GetCurrentHeight();
    if (current_h > 0.0f) {
        sprintf(buf, "Actual: %.1f cm", current_h);
    } else {
        sprintf(buf, "Actual: Error");
    }
    OLED_ShowString(0, 32, (u8*)buf, 16, 1);

    sprintf(buf, "PWM:%d a:%.2f", Hover_GetFanPWM(), cfg.filter_alpha);
    OLED_ShowString(0, 48, (u8*)buf, 16, 1);
    OLED_Refresh();
}

int main(void)
{
    SYSCFG_DL_init();

    OLED_Init();
    OLED_ColorTurn(0);
    OLED_DisplayTurn(0);
    OLED_Clear();

    HC_SR04_Init();
    HC_SR04_SetTemperatureC(DEBUG_TEMPERATURE_C);
    FanRpm_Init();

    Hover_Init();
    Hover_ApplyConfig(&HOVER_TUNE);
    Hover_ApplyTemperatureCalibrationTables(HOVER_TEMP_CAL_TABLES,
        (uint8_t)(sizeof(HOVER_TEMP_CAL_TABLES) / sizeof(HOVER_TEMP_CAL_TABLES[0])));

    /* 先测一次距离，避免 OLED 和 App 首帧显示无效值。 */
    Hover_Control_Tick();

    WifiRemote_Init(HEIGHT_STEP_CM);

    uint32_t last_control_time = micros();
    uint32_t last_oled_time = micros();
    uint32_t last_remote_time = micros();
    uint32_t last_rpm_time = micros();

    while (1) {
        /*
         * UART2 接收在中断中进入环形缓冲区。
         * WifiRemote_Poll() 只在主循环解析完整命令并调用对应模块接口。
         */
        WifiRemote_Poll(ms_ticks);
        ApplyButtons();

        uint32_t now = micros();

        if (now - last_control_time >= CONTROL_PERIOD_US) {
            last_control_time = now;
            Hover_Control_Tick();
        }

        if (now - last_rpm_time >= FAN_RPM_PERIOD_US) {
            last_rpm_time = now;
            FanRpm_Update();
        }

        if (now - last_remote_time >= WIFI_REMOTE_TELEMETRY_PERIOD_US) {
            last_remote_time = now;
            WifiRemote_SendTelemetry(ms_ticks);
        }

        if (now - last_oled_time >= OLED_PERIOD_US) {
            last_oled_time = now;
#if OLED_REMOTE_DEBUG_MODE
            DebugOled_ShowRemotePage();
#else
            ShowNormalOledPage();
#endif
        }
    }
}

// ============================================================================
// 兼容占位变量
// ============================================================================
// 工程中仍有一些历史模块会引用下面的全局变量或函数。当前 B 题主程序不再
// 使用这些变量，但保留定义可以避免链接错误，不影响实际控制逻辑。

float ActualL = 0.0f;
float ActualR = 0.0f;
float OutL = 0.0f;
float OutR = 0.0f;
float TargetR = 0.0f;
float TargetL = 0.0f;
float e = 0.0f;
float target = 0.0f;
float e_last = 0.0f;
float L2 = 0.0f;
float L1 = 0.0f;
float M0 = 0.0f;
float R1 = 0.0f;
float R2 = 0.0f;
uint8_t zhijiao = 0;
int left_motor_speed = 0;
int right_motor_speed = 0;
int base_speed = 0;
int max_speed = 0;
int min_speed = 0;
float Kp = 0.0f;
float Ki = 0.0f;
float Kd = 0.0f;
float integral = 0.0f;
float dt = 0.0f;
float output = 0.0f;
int right_angle_cnt = 0;
int lap_cnt = 0;
int count_locked = 0;

void uart0_send_string(char *str)
{
    (void)str;
}
