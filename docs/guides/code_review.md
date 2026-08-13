# B题程序代码审查报告

## 一、总体结论

程序**整体架构合理、模块划分清晰**，三个评测功能在逻辑框架上已经完整实现。但经逐行审查，发现了 **3 个必须修复的严重问题** 和 **4 个建议优化的项目**。如果不修复严重问题，在实际硬件上运行大概率会出现测距不准、控制失效或显示异常。

---

## 二、严重问题（必须修复）

### 🔴 问题 1：主循环实际周期远大于 50ms，PID 的 `dt` 假设失效

**位置**：[empty.c:149-150](../../firmware/mspm0/empty.c#L149-L150) + [hover_ctrl.c:102](../../firmware/mspm0/hover_ctrl.c#L102)

**现象**：主循环末尾调用了 `delay_ms(50)` 来实现"20Hz 控制频率"。但这个 50ms **只是循环末尾的等待时间**，没有计入循环体本身的执行耗时。实际每一轮循环的总耗时 = 超声波测距时间 + OLED 刷屏时间 + PID 计算 + 50ms 延时。

超声波测距 `HC_SR04_GetDistance()` 中：
- 等待 Echo 高电平最大 30ms（[hc_sr04.c:41](../../firmware/mspm0/hc_sr04.c#L41)）
- 等待 Echo 低电平最大 30ms（[hc_sr04.c:52](../../firmware/mspm0/hc_sr04.c#L52)）
- 正常测距 70cm 时，声波往返 ≈ 4.1ms

OLED I2C 刷屏（128×64 像素、100kHz I2C）≈ 10~15ms

因此正常情况下实际循环周期约为 **50 + 4 + 12 ≈ 66ms**，而不是 50ms。PID 中假设的 `dt = 0.05f` 与实际不符，会导致 **积分项和微分项的计算偏差约 30%**，直接影响闭环控制精度。

**修复方案**：用 `micros()` 测量真实循环间隔，用实际 `dt` 进行 PID 计算：

```diff
// hover_ctrl.c
+static uint32_t s_last_tick_us = 0;
+
 void Hover_Control_Tick(void)
 {
+    // 计算真实 dt
+    extern uint32_t micros(void);
+    uint32_t now = micros();
+    float dt = (float)(now - s_last_tick_us) / 1000000.0f;
+    s_last_tick_us = now;
+    if (dt <= 0.0f || dt > 0.5f) dt = 0.05f; // 首次运行或异常保护
+
     // ...
-    float dt = 0.05f;
+    // dt 已由上方实测值替代
```

---

### 🔴 问题 2：`micros()` 在约 71.6 分钟后发生 `uint32_t` 溢出，导致测距和 PID 全部失效

**位置**：[empty.c:35-48](../../firmware/mspm0/empty.c#L35-L48) + [hc_sr04.c:39-61](../../firmware/mspm0/hc_sr04.c#L39-L61)

**现象**：`micros()` 返回 `uint32_t`，最大值约 4,294,967,295 μs ≈ 71.6 分钟。溢出后：
- `micros() - wait_start` 差值计算仍然正确（无符号减法的溢出特性），所以 **hc_sr04.c 中的超时判断不受影响**。✅
- 但 `ms_ticks * 1000 + (32000 - val) / 32` 中，`ms_ticks * 1000` 在 `ms_ticks > 4294967` 时会溢出，导致 `micros()` 返回的时间戳回绕。✅ 回绕后差值计算依然正确。

**结论**：经过复核，由于 `hc_sr04.c` 中使用的是差值比较（`micros() - wait_start > 30000`），`uint32_t` 的无符号溢出不会造成逻辑错误。**此问题无需修复**。但如果将来需要绝对时间戳（如显示运行时长），需要改用 `uint64_t`。

---

### 🔴 问题 3：`delay_ms(50)` 使用的是 board.c 中的忙等待（`delay_cycles`），会**阻塞 SysTick 中断响应**

**位置**：[board.c:84](../../firmware/mspm0/Board/board.c#L84) + [empty.c:150](../../firmware/mspm0/empty.c#L150)

**现象**：`board.c` 中的 `delay_ms` 实现是 `delay_cycles((CPUCLK_FREQ / 1000) * __ms)`，这是一个 CPU 忙等待循环。在 50ms 的忙等待期间：
- SysTick 中断**仍然可以触发**（因为 `delay_cycles` 只是在循环中递减计数器，不会禁用中断）。✅
- 但是 `empty.c` 中额外定义的 `Delay_ms()`（大写 D，第 28-32 行）使用了 `delay_times` 变量和 `SysTick_Handler` 配合的非阻塞方案，但**主程序实际调用的是 `delay_ms`（小写 d，来自 board.h）**，所以 `Delay_ms` 函数和 `delay_times` 变量**完全没有被使用**，属于死代码。

**结论**：`delay_ms(50)` 虽然是忙等待，但不影响中断。`Delay_ms` 和 `delay_times` 是无用代码，不影响功能但造成混淆。建议删除。

---

### 🔴 问题 4（实际严重）：OLED 每次循环都调用 `OLED_Clear()` + 全屏重绘 + `OLED_Refresh()`，严重拖慢控制频率

**位置**：[empty.c:122-147](../../firmware/mspm0/empty.c#L122-L147)

**现象**：每次主循环都先清空整个 OLED 帧缓存（`OLED_Clear()`），然后重新绘制 4 行文字，再通过 I2C 把整个 128×64 帧缓存发送出去（`OLED_Refresh()`）。这在 100kHz I2C 上需要约 10~15ms。加上超声波测距的 4ms 和末尾的 50ms 延时，实际控制周期被拉到 ~66ms（约 15Hz），而不是注释中声称的 20Hz。

**修复方案**：将 OLED 显示与控制解耦——使用计数器，每 N 次控制 Tick 才刷新一次 OLED，或者把 OLED 刷新放到控制计算之后、delay 之前（当前已经是这样），但把 `delay_ms(50)` 改成**基于 `micros()` 的真实 50ms 定时**。

---

## 三、建议优化项

### 🟡 优化 1：PID 微分项缺少低通滤波，容易产生高频噪声尖峰

**位置**：[hover_ctrl.c:115](../../firmware/mspm0/hover_ctrl.c#L115)

```c
float d_out = s_pid.kd * (error - s_pid.error_prev) / dt;
```

超声波传感器的测量值存在 ±2mm 左右的随机噪声。微分项直接用 `(error - error_prev) / dt` 会将噪声放大 $K_d / dt = 6.0 / 0.05 = 120$ 倍。这会导致风机产生高频抖动，乒乓球在目标附近出现可见的颤振。

**建议**：在 `s_current_height` 上加一阶指数滑动平均滤波（EMA），或对微分项单独加低通滤波：

```c
// 一阶滤波，alpha 越小越平滑
static float s_filtered_height = 0.0f;
const float alpha = 0.3f;
s_filtered_height = alpha * dist + (1.0f - alpha) * s_filtered_height;
s_current_height = s_filtered_height;
```

---

### 🟡 优化 2：按键消抖时间不可靠

**位置**：[empty.c:83-111](../../firmware/mspm0/empty.c#L83-L111)

当前的按键"消抖"实质上是**边沿状态机检测**（检测按下→释放的跳变），而不是真正的时间消抖。由于主循环每 ~66ms 执行一次，**机械触点的弹跳周期（通常 5~20ms）在两次轮询之间就已经结束了**，所以实际上不太会出现多次触发——但这只是巧合。

如果未来循环更快（如优化掉 OLED 后），弹跳就可能被误读。建议加一个简单的计时消抖：

```c
static uint32_t btn1_press_time = 0;
if (DL_GPIO_readPins(GPIOB, DL_GPIO_PIN_25) == 0) {
    if (!s_btn1_active && (micros() - btn1_press_time > 200000)) { // 200ms 间隔
        s_btn1_active = true;
        btn1_press_time = micros();
        Hover_DecreaseTarget(HEIGHT_STEP);
    }
} else {
    s_btn1_active = false;
}
```

---

### 🟡 优化 3：`display_buf` 缓冲区仅 32 字节，`sprintf` 存在潜在溢出风险

**位置**：[empty.c:74](../../firmware/mspm0/empty.c#L74)

```c
char display_buf[32];
```

`"PWM Val: %d / 1000"` 在 `pwm_val = 950` 时产生 `"PWM Val: 950 / 1000"`（21 字符 + `\0` = 22 字节），安全。但如果将来添加更长的格式化字符串，32 字节可能不够。**建议改为 48 或 64**。

---

### 🟡 优化 4：`s_current_height` 初始值为 0，首次显示时会显示 "Actual: Error"

**位置**：[hover_ctrl.c:20](../../firmware/mspm0/hover_ctrl.c#L20) + [empty.c:136](../../firmware/mspm0/empty.c#L136)

系统上电后 `s_current_height = 0.0f`，首次进入主循环时 `Hover_Control_Tick` 还没来得及更新距离值，OLED 就会显示 `"Actual: Error"`。虽然只持续一帧（~66ms），但在评测时可能会被注意到。

**建议**：在主循环开始前先执行一次测距预热：
```c
// 在 while(1) 之前
Hover_Control_Tick(); // 预热一次测距
```

---

## 四、引脚冲突检查

| 引脚 | 用途 | SysConfig 中的定义 | 冲突? |
|:---|:---|:---|:---|
| PB12 (PINCM29) | HC-SR04 Trig 输出 | **未被 SysConfig 使用** | ✅ 无冲突 |
| PB13 (PINCM30) | HC-SR04 Echo 输入 | **未被 SysConfig 使用** | ✅ 无冲突 |
| PA16 (PINCM38) | PWM 通道 1 输出 | `GPIO_PWM_1_C1` (TIMA1_CCP1) | ✅ 一致 |
| PA17 (PINCM39) | PWM 通道 2 输出 | `GPIO_PWM_1_C0` (TIMA1_CCP0) | ✅ 一致 |
| PB25 (PINCM56) | 按键 1 输入 | `GPIO_BUTTON_PIN_TASK1` (中断上升沿) | ⚠️ 见下方说明 |
| PA29 (PINCM4) | 按键 2 输入 | `GPIO_BUTTON_PIN_TASK2` (中断上升沿) | ⚠️ 见下方说明 |
| PB26 (PINCM57) | 按键 3 输入 | `GPIO_BUTTON_PIN_TASK4` (中断上升沿) | ⚠️ 见下方说明 |

> [!WARNING]
> **PB25 / PA29 / PB26 的中断配置可能干扰轮询**
>
> 这三个引脚在 SysConfig 中被配置为**上升沿中断触发**（`GROUP1_IRQHandler`）。在 `empty_backup.c` 的中断处理函数中，这些中断被捕获并启动了防抖计数器。虽然当前 `empty.c` 中没有定义 `GROUP1_IRQHandler`，但如果 `empty_backup.c` 仍然参与编译链接（它确实在 makefile 中），**那个文件中的 `GROUP1_IRQHandler` 可能会响应中断并执行旧的编码器计数逻辑**，这可能会导致意外行为。
>
> **建议**：确认 `empty_backup.c` 中的 `GROUP1_IRQHandler` 不会与当前程序产生链接冲突。如果 `empty_backup.c` 也定义了 `GROUP1_IRQHandler`，可能会产生**重复定义的链接错误**或**弱符号覆盖**。

---

## 五、功能对照表

| 题目要求 | 实现状态 | 备注 |
|:---|:---|:---|
| **功能 1**：静态测距，实时显示，误差 ≤ 2cm | ✅ 已实现 | STANDBY 模式下持续测距并显示 |
| **功能 1**：数据刷新稳定、无明显跳变 | ⚠️ 需验证 | 未加滤波，超声波原始值可能有 ±2mm 波动 |
| **功能 2**：两个独立按键，加减各 5CM | ✅ 已实现 | `HEIGHT_STEP = 5.0f`，范围 30~70cm |
| **功能 2**：按键触发响应灵敏 | ✅ 基本满足 | 边沿检测，每次按下只触发一次 |
| **功能 2**：可自由调整悬浮定位区间 | ✅ 已实现 | 按键调节目标后 PID 自动跟踪 |
| **功能 2**：稳定时间越快越好、误差越小越好 | ⚠️ 需调参 | PID 参数需实车验证和精细调整 |
| **功能 3**：30cm~70cm 范围闭环控制 | ✅ 已实现 | 限幅范围 30~70，默认 50cm |
| **功能 3**：PID 闭环稳态精准 | ⚠️ 需验证 | dt 不准会影响精度（见问题 1） |
| **功能 3**：自适应导槽倾斜角度变化 | ✅ 闭环机制支持 | PID 积分项可消除稳态偏差 |
| **功能 3**：无明显偏移、坠落或飞出 | ⚠️ 需验证 | 微分噪声可能导致颤振（见优化 1） |

---

## 六、修复优先级

| 优先级 | 问题 | 影响 |
|:---|:---|:---|
| 🔴 **P0** | PID `dt` 固定为 0.05f 与实际不符 | 控制精度偏差 ~30% |
| 🔴 **P0** | 缺少距离测量值滤波 | 微分项噪声放大导致风机颤振 |
| 🟡 **P1** | OLED 每帧全清全刷拖慢循环 | 控制频率低于预期 |
| 🟡 **P1** | `empty_backup.c` 中 `GROUP1_IRQHandler` 可能冲突 | 按键行为异常的隐患 |
| 🟢 **P2** | 首帧显示 "Error"、死代码清理 | 观感问题 |
