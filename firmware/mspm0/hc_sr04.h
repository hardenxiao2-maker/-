#ifndef HC_SR04_H
#define HC_SR04_H

#include <stdint.h>

/**
 * @brief 初始化 HC-SR04 超声波传感器引脚
 * @details 将 PB12 配置为 Trig (输出)，PB13 配置为 Echo (输入，带内部下拉)
 */
void HC_SR04_Init(void);

/**
 * @brief 设置超声波测距使用的环境温度。
 * @details 声速按 v = 331.4 + 0.6 * T 计算，T 单位为摄氏度。
 */
void HC_SR04_SetTemperatureC(float temperature_c);

/**
 * @brief 获取当前测距温度参数。
 */
float HC_SR04_GetTemperatureC(void);

/**
 * @brief 执行一次超声波测距
 * @param[out] pulse_width_us 接收高电平脉宽时间指针 (单位: 微秒)
 * @return float 测得的距离 (单位: 厘米)，若测量超时或无效则返回 -1.0f
 */
float HC_SR04_GetDistance(uint32_t *pulse_width_us);

#endif // HC_SR04_H
