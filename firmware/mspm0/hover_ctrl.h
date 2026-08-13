#ifndef HOVER_CTRL_H
#define HOVER_CTRL_H

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    // PID 三个核心参数：先调 kp，再少量加 kd，最后再加 ki 消除静差。
    float kp;
    float ki;
    float kd;

    // 积分限幅，防止长时间大误差造成积分堆积，导致小球冲出。
    float integral_max;

    // 风机 PWM 参数，范围按当前 TIMA1 周期 1000 计算。
    float base_pwm;
    float pwm_min;
    float pwm_max;
    float safe_pwm;

    // 超声波测距的一阶低通滤波系数，越大响应越快，越小显示越稳。
    float filter_alpha;

    // 目标距离默认值和按键可调范围，单位 cm。
    float target_default;
    float target_min;
    float target_max;
} HoverDebugConfig_t;

typedef struct {
    // 超声波原始测得距离，必须按从小到大排列。
    float measured_cm;

    // 该测得距离对应的真实距离，程序会在相邻点之间线性插值。
    float true_cm;
} HoverDistanceCalPoint_t;

#define HOVER_MAX_TEMP_TABLES (5u)
#define HOVER_MAX_CAL_POINTS  (12u)

typedef struct {
    float temperature_c;
    uint8_t count;
    HoverDistanceCalPoint_t points[HOVER_MAX_CAL_POINTS];
} HoverTempCalTable_t;

/**
 * @brief 初始化悬停控制系统
 * @details 初始化 PID 参数、风机输出和目标高度
 */
void Hover_Init(void);

/**
 * @brief 应用主程序中的 PID、PWM、滤波和目标范围调试参数。
 */
void Hover_ApplyConfig(const HoverDebugConfig_t *cfg);

/**
 * @brief 读取当前运行参数，供 App 在线调参时先取后改。
 */
void Hover_GetConfig(HoverDebugConfig_t *cfg);

/**
 * @brief 应用超声波距离校准表。
 * @details 表格式为 {测距值, 真实值}，程序用分段线性插值修正测距误差。
 */
void Hover_ApplyDistanceCalibration(const HoverDistanceCalPoint_t *points, uint8_t count);

/**
 * @brief 应用多温度校准表。
 * @details 每张表内部 measured_cm 递增，温度表按 temperature_c 递增。
 */
bool Hover_ApplyTemperatureCalibrationTables(const HoverTempCalTable_t *tables, uint8_t count);

/**
 * @brief 开始编辑某个温度档位的运行时校准表。
 */
bool Hover_CalibrationBegin(float temperature_c);

/**
 * @brief 写入运行时校准表中的一个标定点。
 */
bool Hover_CalibrationSetPoint(uint8_t index, float measured_cm, float true_cm);

/**
 * @brief 校验并应用运行时校准表。
 */
bool Hover_CalibrationApply(void);

/**
 * @brief 悬停控制主循环 Tick (建议以 20Hz 频率调用，即每 50ms 一次)
 * @details 读取超声波距离数据，通过 PID 算法调节风速并输出 PWM
 */
void Hover_Control_Tick(void);

/**
 * @brief 减小目标悬停高度
 * @param step 每次按键减小的具体高度值 (cm)
 */
void Hover_DecreaseTarget(float step);

/**
 * @brief 增加目标悬停高度
 * @param step 每次按键增加的具体高度值 (cm)
 */
void Hover_IncreaseTarget(float step);

/**
 * @brief 直接设置目标悬停高度，网页调试或串口调试时使用。
 * @param target_cm 目标高度，单位 cm，函数内部会自动限制到允许范围。
 */
void Hover_SetTargetHeight(float target_cm);

/**
 * @brief 直接设置控制系统启停状态，网页调试或串口调试时使用。
 * @param active true 启动闭环控制，false 停机并清零 PWM。
 */
void Hover_SetActive(bool active);

/**
 * @brief 切换控制系统启动/待机状态 (通常由主程序检测到状态按键被按下时调用)
 */
void Hover_ToggleActive(void);

/**
 * @brief 获取当前设定的目标高度
 * @return float 目标高度 (cm)
 */
float Hover_GetTargetHeight(void);

/**
 * @brief 获取最后一次成功测量的实际高度
 * @return float 实际高度 (cm)
 */
float Hover_GetCurrentHeight(void);

/**
 * @brief 获取当前输出给风机的 PWM 占空比值 (0 ~ 1000)
 * @return int PWM 值
 */
int Hover_GetFanPWM(void);

/**
 * @brief 获取当前系统运行状态 (是否启动风机控制)
 * @return true 系统已启动
 * @return false 系统已停机 (PWM 为 0)
 */
bool Hover_IsActive(void);

#endif // HOVER_CTRL_H
