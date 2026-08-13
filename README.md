# 乒乓球位置控制实验装置

本项目是 2026 年佛山大学大学生电子设计竞赛 B 题“乒乓球位置控制实验装置”的完整实现。系统以 TI MSPM0G3507 为主控，通过 HC-SR04 测距、距离校准与 PID 闭环调节涡轮风机 PWM，使乒乓球在倾斜半圆导槽内稳定在设定位置；同时支持实体按键、OLED、ESP32 网页和 Android App 调试。

## 主要功能

- 30–70 cm 目标距离设定，实体按键以 5 cm 步长增减
- 超声波静态测距、温度补偿、多点分段线性校准和低通滤波
- PID 闭环风力控制，带基础 PWM、积分限幅、异常测距安全输出
- OLED 显示运行状态、目标距离、实际距离、PWM/RPM
- UART2 遥测和远程命令协议
- ESP32 自建热点网页监控，支持实时曲线和远程控制
- Android App 直连 WiFi/蓝牙串口透传模块，支持在线调参和校准

## 系统组成

```text
HC-SR04 ──> MSPM0G3507 ──PWM──> 风机驱动 ──> 乒乓球
                 │
                 ├── OLED / 按键
                 └── UART2 ──> ESP32 ──> 网页或 Android App
```

## 仓库结构

```text
firmware/mspm0/       MSPM0G3507 CCS/SysConfig 工程和驱动
esp32/web_bridge/     ESP32 热点、网页服务器和 UART 桥接程序
android/              Android App 源码及可安装 APK
docs/problem/         B 题题目原文
docs/guides/          程序、调参、无线通信和调试说明
videos/               题目 1～4 实机演示视频
```

## 演示视频

- [题目 1 演示](videos/题目1.mp4)
- [题目 2 演示](videos/题目2.mp4)
- [题目 3 演示](videos/题目3.mp4)
- [题目 4 演示](videos/题目4.mp4)

视频内容和文件校验值见 [演示视频说明](videos/README.md)。

## 硬件与接口

| 功能 | 接口 |
| --- | --- |
| HC-SR04 Trig / Echo | PB12 / PB13 |
| 风机 PWM | PA16、PA17（TIMA1） |
| 风机转速反馈 | PB7 |
| 目标距离减小 / 增加 | PB25 / PA29 |
| 启动 / 待机 | PB26 |
| OLED | I2C |
| UART2 TX / RX | PB15 / PA22，115200 baud |

实际接线和供电注意事项见 [调试方法](docs/guides/调试方法.md)。

## 快速开始

### 1. MSPM0 固件

1. 安装 Code Composer Studio、MSPM0 SDK 2.5.1.00、SysConfig 1.24.0 和 TI Clang 4.0.0 LTS。
2. 在 CCS 中导入 `firmware/mspm0`。
3. 打开 `empty.syscfg` 检查引脚和外设配置。
4. 编译并下载到 MSPM0G3507。
5. 首次带风机运行前，先按 [调试方法](docs/guides/调试方法.md) 完成静态测距、基础风量和 PID 调整。

### 2. ESP32 网页监控

1. 在 Arduino IDE 安装 ESP32 开发板支持和 `arduinoWebSockets` 库。
2. 打开并烧录 `esp32/web_bridge/esp32_web_bridge.ino`。
3. 连接热点 `PingPongCtrl`（示例密码 `12345678`）。
4. 浏览器访问 `http://192.168.4.1`。

完整说明见 [ESP32 网页调试说明](docs/guides/功能4_ESP32_WIFI网页调试说明.md)。

### 3. Android App

- 安装包：`android/release/PingPongCtrl.apk`
- 源码：用 Android Studio 打开 `android`，需要 Android SDK 35，最低支持 Android 6.0（API 23）。
- App 默认面向 `192.168.1.1:2001` 的串口透传模块；IP 和端口可在界面修改。

完整说明见 [Android App 调试说明](docs/guides/功能4_Android_APP调试说明.md)。

## 串口协议

MSPM0 周期上传：

```text
T,ms,active,target_cm,current_cm,pwm
```

远程端可发送 `RUN`、`STOP`、`UP`、`DOWN`、`SET 50`、`GET`，也可使用 `CFG,...` 和 `CAL,...` 命令在线修改参数与校准表。协议细节见 [参数调节功能说明](docs/guides/参数调节功能说明.md)。

## 发布说明

- 本仓库只保留 B 题可复现内容；SDK、IDE、供应商工具包、编译缓存、备份文件、H 题和其他历史项目均未收录。
- ESP32 和 App 中的密码均为演示默认值，部署时请修改。

## License

[MIT](LICENSE)
