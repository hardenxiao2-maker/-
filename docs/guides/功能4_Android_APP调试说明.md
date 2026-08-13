# 功能4：Android App 直连 ESP32 透传模块调试说明

## 1. 当前方案

功能4采用 ESP32 WiFi/蓝牙二合一串口透传模块。模块通过 UART2 与 MSPM0G3507 通信，手机通过 WiFi 连接模块热点，Android App 直接连接模块 TCP 端口。

```text
MSPM0G3507 UART2
    -> ESP32 WiFi/蓝牙透传模块
    -> 手机连接模块 WiFi
    -> Android App 连接模块 TCP 端口
    -> App 显示状态、距离、PWM 和曲线
```

当前实测已完成：

- App 可接收单片机上传的 `T,...` 遥测帧。
- App 可下发 `RUN` / `STOP` 控制单片机启停。
- App 可下发 `UP` / `DOWN` / `SET` 调整目标距离。
- 实体按键改变状态后，App 界面能同步显示。

## 2. App 工程位置

```text
功能4_Android_APP
```

核心文件：

```text
app/src/main/java/com/ti/pingpongctrl/MainActivity.java
```

APK 文件：

```text
PingPongCtrl-debug.apk
```

## 3. 默认连接参数

根据模块资料和实测，常用默认参数为：

```text
WiFi 名称：Makerobo
WiFi 密码：12345678
模块 IP：192.168.1.1
TCP 端口：2001
串口波特率：115200
```

App 默认填写：

```text
192.168.1.1:2001
```

如果实际模块 IP 或端口不同，可直接在 App 界面修改。

## 4. 硬件接线

| MSPM0G3507 | ESP32 透传模块 | 说明 |
| --- | --- | --- |
| PB15 / UART2_TX | RXD | MSPM0 上传遥测数据 |
| PA22 / UART2_RX | TXD | App 下发命令给 MSPM0 |
| GND | GND | 必须共地 |
| 5V | VCC | 按商品页要求供电 |

注意：

- 模块 VCC 按商品页接 5V。
- MSPM0 串口是 3.3V 逻辑，ESP32 芯片本身也是 3.3V 逻辑。
- 若实测模块 TXD 输出为 5V，需要在模块 TXD 到 MSPM0 PA22 之间加电平转换或限流保护。

## 5. 串口协议

MSPM0 每 200ms 上传一帧：

```text
T,ms,active,target_cm,current_cm,pwm
```

示例：

```text
T,12345,1,50.0,49.6,382
```

字段含义：

| 字段 | 含义 |
| --- | --- |
| `T` | 遥测数据帧 |
| `ms` | 上电后的毫秒时间 |
| `active` | 1 表示运行，0 表示待机 |
| `target_cm` | 目标距离 |
| `current_cm` | 当前实测距离 |
| `pwm` | 风机 PWM 输出 |

App 可发送：

```text
RUN
STOP
UP
DOWN
SET 50
GET
```

## 6. 使用步骤

1. 给 MSPM0 主控和 ESP32 透传模块供电。
2. 手机 WiFi 连接模块热点，例如 `Makerobo`。
3. 密码输入 `12345678`。
4. 如果 Android 提示“该 WLAN 无法访问互联网”，选择继续使用该网络。
5. 打开 App，确认 IP 为 `192.168.1.1`、端口为 `2001`。
6. 点击“连接”。
7. 正常情况下 App 日志会周期性出现 `RX T,...`。
8. 点击“启动”或“停止”，观察 OLED 第一行是否切换 `ACTIVE/STANDBY`。
9. 点击 `+5cm`、`-5cm` 或 `SET`，观察 OLED 目标距离是否同步变化。

## 7. UART2 接收修复说明

早期版本使用主循环轮询 UART2 接收数据。在实测中发现，App 日志显示 `TX RUN`，但单片机不切换状态。进一步使用 OLED 显示 UART2 原始接收字节，并进行 TX/RX 自环回测试后确认：主循环轮询方式会因为 OLED 刷屏、超声波测距等耗时任务造成 UART FIFO 溢出，导致 `RUN`、`STOP` 等命令字符丢失。

当前版本已改为：

```text
UART2 RX 中断 -> 128 字节软件环形缓冲区 -> 主循环解析命令
```

修复后，App 控制启停和目标距离调节已通过实测。

## 8. 常见问题

### App 连接失败

- 确认手机连接的是模块 WiFi，不是家里路由器 WiFi。
- 确认 Android 没有因为“无互联网”自动切回其他网络。
- 确认 IP 和端口为 `192.168.1.1:2001`。

### 能连接但没有数据

- 检查 MSPM0 `PB15/UART2_TX` 是否接模块 `RXD`。
- 检查模块和 MSPM0 是否共地。
- 检查模块串口波特率是否和 MSPM0 UART2 一致，当前为 `115200bps`。

### 能接收数据但 App 按钮无效

- 检查模块 `TXD` 是否接 MSPM0 `PA22/UART2_RX`。
- 看 App 日志是否出现 `TX RUN` 或 `TX SET 50`。
- 确认当前单片机程序已使用 UART2 中断接收，避免轮询接收丢字节。

### 曲线显示占满后不明显滚动

当前 App 曲线采用固定点数显示，长时间运行后视觉上会铺满显示区域。该问题不影响控制功能；后续可改为最近 30s 或 60s 的时间窗口滚动显示。
# 2026-06-15 更新：新版 App 与固件调试要点

## 新增功能

1. App 支持 WiFi TCP 和蓝牙 SPP 两种连接方式，二者发送的命令完全一致。
2. App 新增“控制 / 曲线 / 参数 / 校准”四个页面。
3. 单片机遥测格式扩展为：

```text
T,ms,active,target,current,pwm,rpm,temp
```

旧格式 `T,ms,active,target,current,pwm` 仍可被 App 兼容解析。

## 参数调节命令

```text
CFG?
CFG,KP,22.0
CFG,KI,1.5
CFG,KD,6.0
CFG,BASE_PWM,350
CFG,PWM_MIN,0
CFG,PWM_MAX,950
CFG,SAFE_PWM,150
CFG,FILTER_ALPHA,0.35
CFG,TARGET_MIN,30
CFG,TARGET_MAX,70
CFG,STEP,5
CFG,TEMP,25
```

温度参数会同时影响 HC-SR04 声速补偿和多温度校准表选择。

## 测距校准命令

```text
CAL,TEMP,25
CAL,POINT,0,20.0,20.0
CAL,POINT,1,25.0,25.0
...
CAL,APPLY
```

App 校准页会按 20cm 到 70cm 的 11 个特征点批量发送。单片机会先写入临时表，`CAL,APPLY` 校验通过后才切换到新表。

## RPM 接线

风机反馈线接 PB7。程序按“每转 2 个脉冲”计算：

```text
RPM = pulse_count * 30
```

PB7 已在程序中配置为输入上拉、上升沿中断。若风机反馈线是 5V 电平，必须先做电平转换或分压后再接入 PB7。

## 2026-06-15 追加更新

### App 本地保存与自动同步

新版 App 会把参数页和校准页填写的数据保存在手机本地。每次 WiFi 或蓝牙连接成功后，App 会自动发送：

```text
CFG,KP...
CFG,KI...
CFG,KD...
CFG,TEMP...
CAL,TEMP...
CAL,POINT...
CAL,APPLY
CFG?
GET
```

这样单片机断电后虽然仍会恢复程序默认值，但手机 App 一连接就会把上一次调好的参数重新写入单片机，实现“调试效果由 App 保存”的使用方式。

### 曲线页

曲线页现在分为两块：

1. PID tracking：显示目标距离、实际距离和误差曲线。
2. Fan speed RPM：显示风机转速 RPM 曲线，并叠加 PWM 曲线用于观察控制量变化。

### OLED 临时调试页

当前固件中 `empty.c` 的：

```c
#define OLED_REMOTE_DEBUG_MODE (1)
```

表示 OLED 临时显示远程通信调试页。四行含义如下：

```text
RX:累计接收字节数 L:累计命令行数
CMD:最近收到的一条命令
T:当前温度 Kp:当前Kp
CFG:成功设置参数次数 RPM:风机转速
```

当 App 连接后自动同步参数时，OLED 上的 `RX`、`L`、`CMD`、`CFG` 应该变化。确认无误后，可把 `OLED_REMOTE_DEBUG_MODE` 改为 `0`，恢复正常状态页面。
