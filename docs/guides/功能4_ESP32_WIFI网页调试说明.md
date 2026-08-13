# 功能4：ESP32 WiFi 网页实时监测调试说明

## 1. 功能目标

ESP32 作为 WiFi 热点和网页服务器，MSPM0G3507 主控通过 UART2 把乒乓球位置控制数据实时上传给 ESP32。电脑连接 ESP32 热点后，浏览器访问网页即可看到：

- 系统运行状态：ACTIVE / STANDBY
- 目标距离：target，单位 cm
- 实测距离：actual，单位 cm
- 风机 PWM：0~1000
- 目标距离和实测距离实时曲线

网页也可以发送调试命令，实现远程启动、停止、目标高度增减和直接设定。

## 2. 文件位置

- MSPM0 主程序：`empty.c`
- MSPM0 WiFi 串口协议模块：`wifi_remote.c`、`wifi_remote.h`
- 悬停控制接口：`hover_ctrl.c`、`hover_ctrl.h`
- ESP32 网页桥接程序：`esp32_web_bridge/esp32_web_bridge.ino`

## 3. 硬件接线

MSPM0G3507 使用 UART2，当前 SysConfig 配置为 115200 baud：

| MSPM0G3507 | ESP32 | 说明 |
| --- | --- | --- |
| PB15 / UART2_TX | GPIO16 / RX2 | MSPM0 上传数据到 ESP32 |
| PA22 / UART2_RX | GPIO17 / TX2 | ESP32 下发网页命令到 MSPM0 |
| GND | GND | 必须共地 |
| 3.3V | 3.3V 或 ESP32 独立供电 | 按模块供电要求接入 |

注意：

- MSPM0 和 ESP32 都是 3.3V TTL 串口，不要接 5V 串口电平。
- 如果 ESP32 模块使用 USB 供电，仍然必须和 MSPM0 共地。
- 如果网页只有数据显示但按钮无效，优先检查 PA22 <- GPIO17 这根线。

## 4. ESP32 烧录步骤

1. 打开 Arduino IDE。
2. 安装 ESP32 开发板支持包。
3. 安装 WebSocket 库：`arduinoWebSockets`，头文件名为 `WebSocketsServer.h`。
4. 打开 `esp32_web_bridge/esp32_web_bridge.ino`。
5. 选择对应 ESP32 开发板和串口。
6. 编译并上传。

上传成功后，ESP32 会创建热点：

- WiFi 名称：`PingPongCtrl`
- WiFi 密码：`12345678`
- 网页地址：`http://192.168.4.1`

## 5. MSPM0 端串口协议

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
| `T` | telemetry，遥测数据帧 |
| `ms` | MSPM0 上电后的毫秒时间 |
| `active` | 1 表示闭环运行，0 表示待机 |
| `target_cm` | 目标距离 |
| `current_cm` | 校准和滤波后的实测距离 |
| `pwm` | 当前风机 PWM 输出 |

网页可下发命令：

| 命令 | 功能 |
| --- | --- |
| `RUN` | 启动闭环控制 |
| `STOP` | 停止控制，PWM 清零 |
| `TOGGLE` | 切换启动/待机 |
| `UP` 或 `+` | 目标距离增加 5cm |
| `DOWN` 或 `-` | 目标距离减少 5cm |
| `SET 50` | 直接设定目标距离为 50cm |
| `GET` | 立即回传一帧当前状态 |

MSPM0 收到命令后会返回：

```text
A,OK,RUN
A,ERR,UNKNOWN_CMD
```

## 6. 调试流程

1. 先不要打开风机，只连接 MSPM0、ESP32 和超声波模块。
2. 给 ESP32 烧录网页桥接程序。
3. 给 MSPM0 编译下载当前工程。
4. 电脑连接 WiFi `PingPongCtrl`。
5. 浏览器访问 `http://192.168.4.1`。
6. 查看网页右侧日志，正常会周期出现 `T,...` 数据。
7. 移动乒乓球或挡板，观察“实测距离”和曲线是否变化。
8. 点击 `+5 cm`、`-5 cm` 或输入 `SET 50`，确认 OLED 和网页目标值同步变化。
9. 确认机械结构安全后，再点击“启动”进入闭环控制。

## 7. 常见问题

### 网页打不开

- 确认电脑连接的是 `PingPongCtrl` 热点。
- 确认地址是 `http://192.168.4.1`，不是 `https`。
- 复位 ESP32 后等待 3~5 秒再刷新网页。

### 网页打开但没有实时数据

- 检查 MSPM0 PB15 是否接到 ESP32 GPIO16。
- 检查两块板是否共地。
- 检查 MSPM0 程序是否已经下载并运行。
- 检查 ESP32 和 MSPM0 波特率是否都是 115200。

### 网页按钮无效

- 检查 ESP32 GPIO17 是否接到 MSPM0 PA22。
- 检查网页日志中是否出现 `TX RUN`、`TX SET 50`。
- 如果网页有 `TX` 但没有 `A,OK`，说明命令没有正确到达 MSPM0。

### 曲线跳变明显

- 先确认超声波校准表 `HOVER_DISTANCE_CAL[]` 是否已经按真实测量值填写。
- 适当减小 `HOVER_TUNE.filter_alpha` 可以让曲线更平滑，但响应会变慢。
- 保持超声波探头正对球，避免支架、风道边缘进入测距波束。

## 8. 后续扩展

当前方案已经满足电脑端网页实时接收数据。后续如果要做手机 App，可以继续沿用同一套串口协议；App 只需要连接 ESP32 的 WiFi 或蓝牙通道，发送相同命令并解析 `T,...` 数据帧即可。
