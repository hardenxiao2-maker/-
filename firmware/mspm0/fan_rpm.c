#include "fan_rpm.h"

#include "ti/devices/msp/m0p/mspm0g350x.h"
#include "ti/driverlib/dl_gpio.h"
#include "ti_msp_dl_config.h"

/*
 * 风机测速硬件说明
 * ---------------------------------------------------------------------------
 * 选用 PB7 作为一路风机测速反馈输入。
 * 题设/风机资料：风机每转 1 圈输出 2 个方波脉冲。
 *
 * 计算公式：
 *   RPM = pulse_count / 2 / sample_time_s * 60
 *
 * 当前 sample_time_s = 1s，因此：
 *   RPM = pulse_count * 30
 *
 * 注意：
 *   1. PB7 在程序中配置为输入上拉、上升沿中断。
 *   2. 如果风机反馈线是开漏输出，上拉是必须的。
 *   3. 如果反馈线是 5V 电平，必须先分压或电平转换后再接入 PB7。
 */
#define FAN_TACH_PIN       (DL_GPIO_PIN_7)
#define FAN_TACH_IOMUX     (IOMUX_PINCM24)
#define FAN_PULSES_PER_REV (2u)

static volatile uint32_t s_fan_pulse_count = 0;
static int s_fan_rpm = 0;

void FanRpm_Init(void)
{
    DL_GPIO_initDigitalInputFeatures(FAN_TACH_IOMUX,
        DL_GPIO_INVERSION_DISABLE,
        DL_GPIO_RESISTOR_PULL_UP,
        DL_GPIO_HYSTERESIS_DISABLE,
        DL_GPIO_WAKEUP_DISABLE);

    DL_GPIO_setLowerPinsPolarity(GPIOB, DL_GPIO_PIN_7_EDGE_RISE);
    DL_GPIO_clearInterruptStatus(GPIOB, FAN_TACH_PIN);
    DL_GPIO_enableInterrupt(GPIOB, FAN_TACH_PIN);

    NVIC_ClearPendingIRQ(GPIOB_INT_IRQn);
    NVIC_EnableIRQ(GPIOB_INT_IRQn);
}

void FanRpm_Update(void)
{
    /*
     * pulse_count 在 GROUP1_IRQHandler() 中更新，这里读取并清零时要短暂
     * 关中断，避免读写过程中刚好有新脉冲进入导致计数不一致。
     */
    __disable_irq();
    uint32_t pulses = s_fan_pulse_count;
    s_fan_pulse_count = 0;
    __enable_irq();

    s_fan_rpm = (int)((pulses * 60u) / FAN_PULSES_PER_REV);
}

int FanRpm_GetRPM(void)
{
    return s_fan_rpm;
}

void GROUP1_IRQHandler(void)
{
    /*
     * MSPM0 的 GPIOA/GPIOB 共用 GROUP1 中断入口。
     * 这里除了处理 PB7 测速，也顺手清除 SysConfig 已启用的按键/编码器
     * GPIO 中断标志，避免中断标志悬挂导致重复进入。
     */
    uint32_t gpio_b_status = DL_GPIO_getEnabledInterruptStatus(GPIOB,
        FAN_TACH_PIN |
        GPIO_BUTTON_PIN_TASK1_PIN |
        GPIO_BUTTON_PIN_TASK3_PIN |
        GPIO_BUTTON_PIN_TASK4_PIN);

    if ((gpio_b_status & FAN_TACH_PIN) != 0u) {
        s_fan_pulse_count++;
    }
    if (gpio_b_status != 0u) {
        DL_GPIO_clearInterruptStatus(GPIOB, gpio_b_status);
    }

    uint32_t gpio_a_status = DL_GPIO_getEnabledInterruptStatus(GPIOA,
        GPIO_BUTTON_PIN_TASK2_PIN |
        GPIO_ENCODER_PIN_LA_PIN |
        GPIO_ENCODER_PIN_LB_PIN);
    if (gpio_a_status != 0u) {
        DL_GPIO_clearInterruptStatus(GPIOA, gpio_a_status);
    }
}
