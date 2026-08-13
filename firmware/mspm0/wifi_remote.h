#ifndef WIFI_REMOTE_H
#define WIFI_REMOTE_H

#include <stdint.h>

/*
 * App 遥测周期。
 * 主程序每 200ms 调用一次 WifiRemote_SendTelemetry()，App 用这些数据绘制
 * PID 跟踪曲线、PWM 曲线和风机转速曲线。
 */
#define WIFI_REMOTE_TELEMETRY_PERIOD_US (200000u)

/*
 * OLED 调试页使用的通信统计信息。
 * 这些信息只用于现场确认 App 命令是否已经通过 WiFi/蓝牙透传到 UART2。
 */
typedef struct {
    uint32_t rx_bytes;      // UART2 累计收到的原始字节数
    uint32_t rx_lines;      // 已解析出的完整命令行数量
    uint32_t cfg_ok;        // 成功执行 CFG 参数命令的次数
    uint32_t cal_apply_ok;  // 成功应用 CAL 校准表的次数
    char last_line[32];     // 最近一条命令，过长时截断显示
    float kp;               // 当前 Kp，用于确认参数是否更新
} WifiRemoteDebugInfo_t;

void WifiRemote_Init(float target_step_cm);
void WifiRemote_Poll(uint32_t now_ms);
void WifiRemote_SendTelemetry(uint32_t now_ms);
void WifiRemote_GetDebugInfo(WifiRemoteDebugInfo_t *info);

#endif // WIFI_REMOTE_H
