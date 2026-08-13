#include "hc_sr04.h"
#include "ti/devices/msp/m0p/mspm0g350x.h"
#include "ti/driverlib/dl_gpio.h"
#include "board.h"

// 引用 empty.c 中由 SysTick 维护的微秒计时函数，用于测量 Echo 高电平宽度。
extern uint32_t micros(void);

static float s_temperature_c = 25.0f;

void HC_SR04_SetTemperatureC(float temperature_c)
{
    if (temperature_c < -20.0f) {
        temperature_c = -20.0f;
    } else if (temperature_c > 60.0f) {
        temperature_c = 60.0f;
    }
    s_temperature_c = temperature_c;
}

float HC_SR04_GetTemperatureC(void)
{
    return s_temperature_c;
}

void HC_SR04_Init(void)
{
    // 确保 GPIOB 端口时钟使能。
    DL_GPIO_enablePower(GPIOB);
    
    // Trig 脚 PB12：输出 10us 高电平，触发超声波模块发射。
    DL_GPIO_initDigitalOutput(IOMUX_PINCM29);
    DL_GPIO_clearPins(GPIOB, DL_GPIO_PIN_12);
    DL_GPIO_enableOutput(GPIOB, DL_GPIO_PIN_12);
    
    // Echo 脚 PB13：输入回响脉冲宽度。内部下拉可避免无信号时引脚悬空。
    DL_GPIO_initDigitalInputFeatures(IOMUX_PINCM30,
        DL_GPIO_INVERSION_DISABLE,
        DL_GPIO_RESISTOR_PULL_DOWN,
        DL_GPIO_HYSTERESIS_DISABLE,
        DL_GPIO_WAKEUP_DISABLE);
}

float HC_SR04_GetDistance(uint32_t *pulse_width_us)
{
    // 发送触发脉冲前先确保 Trig 为低电平。
    DL_GPIO_clearPins(GPIOB, DL_GPIO_PIN_12);
    delay_us(2);
    
    // 产生 10us 高电平触发信号。
    DL_GPIO_setPins(GPIOB, DL_GPIO_PIN_12);
    delay_us(10);
    DL_GPIO_clearPins(GPIOB, DL_GPIO_PIN_12);
    
    // 等待 Echo 变为高电平。若 30ms 内没有响应，返回错误，防止程序卡死。
    uint32_t wait_start = micros();
    while (DL_GPIO_readPins(GPIOB, DL_GPIO_PIN_13) == 0) {
        if (micros() - wait_start > 30000) {
            if (pulse_width_us) *pulse_width_us = 0;
            return -1.0f;
        }
    }
    
    // 记录 Echo 高电平开始时刻。
    uint32_t echo_start = micros();
    
    // 等待 Echo 恢复为低电平。高电平持续时间就是声波往返时间。
    while (DL_GPIO_readPins(GPIOB, DL_GPIO_PIN_13) != 0) {
        if (micros() - echo_start > 30000) {
            if (pulse_width_us) *pulse_width_us = 0;
            return -1.0f;
        }
    }
    
    // 记录 Echo 高电平结束时刻。
    uint32_t echo_end = micros();
    
    uint32_t duration = echo_end - echo_start;
    if (pulse_width_us) {
        *pulse_width_us = duration;
    }
    
    // 距离计算：v = 331.4 + 0.6*T (m/s)，Echo 是往返时间，所以需要除以 2。
    // 换算到 cm/us 后的单程系数为 v / 20000。
    float sound_speed = 331.4f + 0.6f * s_temperature_c;
    float distance = (float)duration * (sound_speed / 20000.0f);
    
    // 检验是否在 HC-SR04 常见有效量程内。
    if (distance < 2.0f || distance > 400.0f) {
        return -1.0f;
    }
    
    return distance;
}
