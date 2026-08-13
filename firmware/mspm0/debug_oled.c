#include "debug_oled.h"

#include <stdio.h>

#include "fan_rpm.h"
#include "hc_sr04.h"
#include "oled.h"
#include "wifi_remote.h"

void DebugOled_ShowRemotePage(void)
{
    /*
     * OLED 四行调试信息：
     *   RX/L : UART2 累计收到字节数 / 已解析命令行数
     *   CMD  : 最近收到的一条 App 命令
     *   T/Kp : 当前温度参数 / 当前 Kp
     *   CFG/RPM : 成功执行 CFG 次数 / 当前风机转速
     *
     * App 连接后会自动同步保存的参数。同步发生时，RX、L、CMD、CFG
     * 都应该变化；这能直接验证手机端到单片机端的下行链路。
     */
    WifiRemoteDebugInfo_t info;
    char buf[64];

    WifiRemote_GetDebugInfo(&info);

    OLED_Clear();

    sprintf(buf, "RX:%lu L:%lu",
            (unsigned long)info.rx_bytes,
            (unsigned long)info.rx_lines);
    OLED_ShowString(0, 0, (u8*)buf, 16, 1);

    sprintf(buf, "CMD:%s", info.last_line);
    OLED_ShowString(0, 16, (u8*)buf, 16, 1);

    sprintf(buf, "T:%.1f Kp:%.1f",
            HC_SR04_GetTemperatureC(),
            info.kp);
    OLED_ShowString(0, 32, (u8*)buf, 16, 1);

    sprintf(buf, "CFG:%lu RPM:%d",
            (unsigned long)info.cfg_ok,
            FanRpm_GetRPM());
    OLED_ShowString(0, 48, (u8*)buf, 16, 1);

    OLED_Refresh();
}
