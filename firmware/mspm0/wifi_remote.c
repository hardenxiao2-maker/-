#include "wifi_remote.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "fan_rpm.h"
#include "hc_sr04.h"
#include "hover_ctrl.h"
#include "ti_msp_dl_config.h"

#define WIFI_REMOTE_CMD_BUF_SIZE (96u)
#define WIFI_REMOTE_UART_INST    UART_2_INST
#define RX_BUFFER_SIZE           (128u)

/*
 * UART2 接收缓存。
 * App 通过 WiFi TCP 或蓝牙 SPP 连接 ESP32 透传模块，模块再把收到的文本命令
 * 原样从 UART2 发给 MSPM0。UART2 中断只做“搬运字节”这件事，避免 OLED 刷屏、
 * 超声波测距等耗时任务导致串口 FIFO 溢出。
 */
static volatile uint8_t s_rx_buffer[RX_BUFFER_SIZE];
static volatile uint16_t s_rx_head = 0;
static volatile uint16_t s_rx_tail = 0;
static volatile uint32_t s_rx_bytes = 0;

static float s_target_step_cm = 5.0f;
static uint32_t s_last_now_ms = 0;

/* 下面几项用于 OLED 调试页显示。 */
static uint32_t s_rx_lines = 0;
static uint32_t s_cfg_ok = 0;
static uint32_t s_cal_apply_ok = 0;
static char s_last_line[32] = "";

static void WifiRemote_SendString(const char *str)
{
    while (str != NULL && *str != '\0') {
        while (!DL_UART_Main_transmitDataCheck(WIFI_REMOTE_UART_INST, (uint8_t)*str)) {
        }
        str++;
    }
}

void WifiRemote_Init(float target_step_cm)
{
    if (target_step_cm > 0.0f) {
        s_target_step_cm = target_step_cm;
    }

    NVIC_ClearPendingIRQ(UART2_INT_IRQn);
    NVIC_EnableIRQ(UART2_INT_IRQn);
}

void UART2_IRQHandler(void)
{
    /*
     * 清所有 UART2 中断标志，包含 RX 和可能出现的 RX timeout/overrun。
     * 随后立即把 FIFO 中已有字节搬到软件环形缓冲区。
     */
    DL_UART_Main_clearInterruptStatus(WIFI_REMOTE_UART_INST, 0xFFFFFFFFu);

    while (!DL_UART_Main_isRXFIFOEmpty(WIFI_REMOTE_UART_INST)) {
        uint8_t data = DL_UART_Main_receiveData(WIFI_REMOTE_UART_INST);
        uint16_t next_head = (uint16_t)((s_rx_head + 1u) % RX_BUFFER_SIZE);
        s_rx_bytes++;

        if (next_head != s_rx_tail) {
            s_rx_buffer[s_rx_head] = data;
            s_rx_head = next_head;
        }
    }
}

static bool WifiRemote_ReadFromBuffer(uint8_t *data)
{
    if (s_rx_head == s_rx_tail) {
        return false;
    }

    *data = s_rx_buffer[s_rx_tail];
    s_rx_tail = (uint16_t)((s_rx_tail + 1u) % RX_BUFFER_SIZE);
    return true;
}

static bool StartsWith(const char *line, const char *prefix)
{
    return strncmp(line, prefix, strlen(prefix)) == 0;
}

static void WifiRemote_SendConfig(void)
{
    /*
     * C 帧用于 App 参数页“读取当前参数”。
     * 字段顺序必须和 App 的 updateParamFields() 保持一致。
     */
    HoverDebugConfig_t cfg;
    char buf[160];

    Hover_GetConfig(&cfg);
    snprintf(buf, sizeof(buf),
             "C,%.2f,%.2f,%.2f,%.1f,%.1f,%.1f,%.1f,%.2f,%.1f,%.1f,%.1f,%.1f\n",
             cfg.kp, cfg.ki, cfg.kd,
             cfg.base_pwm, cfg.pwm_min, cfg.pwm_max, cfg.safe_pwm,
             cfg.filter_alpha, cfg.target_min, cfg.target_max,
             s_target_step_cm, HC_SR04_GetTemperatureC());
    WifiRemote_SendString(buf);
}

static bool WifiRemote_SetConfigValue(const char *key, float value)
{
    /*
     * CFG 命令在线修改运行参数，例如：
     *   CFG,KP,22.0
     *   CFG,TEMP,25
     *   CFG,BASE_PWM,350
     *
     * PID/PWM/滤波/目标范围参数写入 hover_ctrl.c；
     * TEMP 写入 hc_sr04.c，用于声速补偿和温度校准表选择；
     * STEP 是远程 UP/DOWN 的步长，只保存在本模块。
     */
    HoverDebugConfig_t cfg;

    if (key == NULL) {
        return false;
    }

    Hover_GetConfig(&cfg);

    if (strcmp(key, "KP") == 0) {
        cfg.kp = value;
    } else if (strcmp(key, "KI") == 0) {
        cfg.ki = value;
    } else if (strcmp(key, "KD") == 0) {
        cfg.kd = value;
    } else if (strcmp(key, "INTEGRAL_MAX") == 0) {
        cfg.integral_max = value;
    } else if (strcmp(key, "BASE_PWM") == 0) {
        cfg.base_pwm = value;
    } else if (strcmp(key, "PWM_MIN") == 0) {
        cfg.pwm_min = value;
    } else if (strcmp(key, "PWM_MAX") == 0) {
        cfg.pwm_max = value;
    } else if (strcmp(key, "SAFE_PWM") == 0) {
        cfg.safe_pwm = value;
    } else if (strcmp(key, "FILTER_ALPHA") == 0) {
        cfg.filter_alpha = value;
    } else if (strcmp(key, "TARGET_MIN") == 0) {
        cfg.target_min = value;
    } else if (strcmp(key, "TARGET_MAX") == 0) {
        cfg.target_max = value;
    } else if (strcmp(key, "TEMP") == 0) {
        HC_SR04_SetTemperatureC(value);
        return true;
    } else if (strcmp(key, "STEP") == 0) {
        if (value <= 0.0f || value > 20.0f) {
            return false;
        }
        s_target_step_cm = value;
        return true;
    } else {
        return false;
    }

    Hover_ApplyConfig(&cfg);
    return true;
}

static void WifiRemote_ProcessConfig(char *line)
{
    char key[20];
    float value = 0.0f;

    if (strcmp(line, "CFG?") == 0) {
        WifiRemote_SendConfig();
        return;
    }

    if (sscanf(line, "CFG,%19[^,],%f", key, &value) == 2) {
        if (WifiRemote_SetConfigValue(key, value)) {
            s_cfg_ok++;
            WifiRemote_SendString("A,CFG,OK\n");
        } else {
            WifiRemote_SendString("A,ERR,CFG_VALUE\n");
        }
    } else {
        WifiRemote_SendString("A,ERR,CFG_FORMAT\n");
    }
}

static void WifiRemote_ProcessCalibration(char *line)
{
    /*
     * CAL 命令在线填写测距校准表：
     *   CAL,TEMP,25              选择/新建 25℃ 表
     *   CAL,POINT,0,19.3,20.0   第 0 点：测得 19.3cm 对应真实 20.0cm
     *   CAL,APPLY               校验并应用整张表
     *
     * 单片机先写临时表，只有 CAL,APPLY 校验通过后才切换，避免半张表影响控制。
     */
    float temp = 0.0f;
    int index = 0;
    float measured = 0.0f;
    float real = 0.0f;

    if (sscanf(line, "CAL,TEMP,%f", &temp) == 1) {
        Hover_CalibrationBegin(temp);
        WifiRemote_SendString("A,CAL,TEMP,OK\n");
    } else if (sscanf(line, "CAL,POINT,%d,%f,%f", &index, &measured, &real) == 3) {
        if (index >= 0 && Hover_CalibrationSetPoint((uint8_t)index, measured, real)) {
            WifiRemote_SendString("A,CAL,POINT,OK\n");
        } else {
            WifiRemote_SendString("A,ERR,CAL_POINT\n");
        }
    } else if (strcmp(line, "CAL,APPLY") == 0) {
        if (Hover_CalibrationApply()) {
            s_cal_apply_ok++;
            WifiRemote_SendString("A,CAL,APPLY,OK\n");
        } else {
            WifiRemote_SendString("A,ERR,CAL_APPLY\n");
        }
    } else if (strcmp(line, "CAL?") == 0) {
        WifiRemote_SendString("A,CAL,USE_CFG_PAGE\n");
    } else {
        WifiRemote_SendString("A,ERR,CAL_FORMAT\n");
    }
}

static void WifiRemote_ProcessLine(char *line)
{
    if (line == NULL || *line == '\0') {
        return;
    }

    strncpy(s_last_line, line, sizeof(s_last_line) - 1u);
    s_last_line[sizeof(s_last_line) - 1u] = '\0';
    s_rx_lines++;

    if (strcmp(line, "RUN") == 0) {
        Hover_SetActive(true);
        WifiRemote_SendString("A,RUN,OK\n");
    } else if (strcmp(line, "STOP") == 0) {
        Hover_SetActive(false);
        WifiRemote_SendString("A,STOP,OK\n");
    } else if (strcmp(line, "TOGGLE") == 0) {
        Hover_ToggleActive();
        WifiRemote_SendString("A,TOGGLE,OK\n");
    } else if (strcmp(line, "UP") == 0) {
        Hover_IncreaseTarget(s_target_step_cm);
        WifiRemote_SendString("A,UP,OK\n");
    } else if (strcmp(line, "DOWN") == 0) {
        Hover_DecreaseTarget(s_target_step_cm);
        WifiRemote_SendString("A,DOWN,OK\n");
    } else if (StartsWith(line, "SET ")) {
        float val = 0.0f;
        if (sscanf(line + 4, "%f", &val) == 1) {
            Hover_SetTargetHeight(val);
            WifiRemote_SendString("A,SET,OK\n");
        } else {
            WifiRemote_SendString("A,ERR,BAD_SET\n");
        }
    } else if (strcmp(line, "GET") == 0) {
        WifiRemote_SendTelemetry(s_last_now_ms);
    } else if (StartsWith(line, "CFG")) {
        WifiRemote_ProcessConfig(line);
    } else if (StartsWith(line, "CAL")) {
        WifiRemote_ProcessCalibration(line);
    } else {
        WifiRemote_SendString("A,ERR,CMD\n");
    }
}

void WifiRemote_Poll(uint32_t now_ms)
{
    /*
     * 从软件环形缓冲区取字节，按 '\n' 拼成完整命令。
     * App 发送命令时必须以换行结尾，现有 App 已统一追加 '\n'。
     */
    static char cmd_buf[WIFI_REMOTE_CMD_BUF_SIZE];
    static uint8_t cmd_len = 0;
    uint8_t data = 0;

    s_last_now_ms = now_ms;

    while (WifiRemote_ReadFromBuffer(&data)) {
        if (data == '\n') {
            cmd_buf[cmd_len] = '\0';
            WifiRemote_ProcessLine(cmd_buf);
            cmd_len = 0;
        } else if (data != '\r') {
            if (cmd_len < (WIFI_REMOTE_CMD_BUF_SIZE - 1u)) {
                cmd_buf[cmd_len++] = (char)data;
            } else {
                cmd_len = 0;
                WifiRemote_SendString("A,ERR,CMD_TOO_LONG\n");
            }
        }
    }
}

void WifiRemote_SendTelemetry(uint32_t now_ms)
{
    /*
     * T 帧给 App 绘图：
     *   T,ms,active,target,current,pwm,rpm,temp
     */
    char buf[96];

    snprintf(buf, sizeof(buf), "T,%lu,%d,%.1f,%.1f,%d,%d,%.1f\n",
             (unsigned long)now_ms,
             Hover_IsActive() ? 1 : 0,
             Hover_GetTargetHeight(),
             Hover_GetCurrentHeight(),
             Hover_GetFanPWM(),
             FanRpm_GetRPM(),
             HC_SR04_GetTemperatureC());
    WifiRemote_SendString(buf);
}

void WifiRemote_GetDebugInfo(WifiRemoteDebugInfo_t *info)
{
    if (info == NULL) {
        return;
    }

    HoverDebugConfig_t cfg;
    Hover_GetConfig(&cfg);

    info->rx_bytes = s_rx_bytes;
    info->rx_lines = s_rx_lines;
    info->cfg_ok = s_cfg_ok;
    info->cal_apply_ok = s_cal_apply_ok;
    strncpy(info->last_line, s_last_line, sizeof(info->last_line) - 1u);
    info->last_line[sizeof(info->last_line) - 1u] = '\0';
    info->kp = cfg.kp;
}
