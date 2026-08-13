#ifndef FAN_RPM_H
#define FAN_RPM_H

#include <stdint.h>

/*
 * 风机转速采样周期。
 * fan_rpm.c 按 1 秒为窗口统计 PB7 上升沿脉冲数量。
 */
#define FAN_RPM_PERIOD_US (1000000u)

void FanRpm_Init(void);
void FanRpm_Update(void);
int FanRpm_GetRPM(void);

#endif // FAN_RPM_H
