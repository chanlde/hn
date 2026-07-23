# DS800 遥控器控制单片机逻辑说明

本文档按当前固件代码整理，用于检查 HOTRC/DS800 SBUS 遥控器控制单片机的全部基础功能和联动逻辑。

相关代码：

- `DJI_PSDK/APP/ds800_protocol/ds800_protocol.c`
- `DJI_PSDK/APP/ds800_protocol/ds800_protocol.h`
- `DJI_PSDK/BSP/pwm_swing.c`
- `DJI_PSDK/BSP/pwm_swing.h`
- `DJI_PSDK/module_sample/widget/test_widget.c`

## 1. 接收与通道

SBUS 接收：

- 串口：USART6
- RX 引脚：PC7
- 格式：100000 baud, 8E2
- RX 反相：开启
- 接收机 S 线接 USART6_RX/PC7

当前通道映射：

| 遥控器控件 | SBUS 通道 | 代码下标 | 当前用途 |
| --- | --- | --- | --- |
| 摇杆左右 | CH1 | `0U` | 固定模式下调固定角度 |
| 摇杆上下 | CH2 | `1U` | 调水泵压力设定值 |
| VRA | CH3 | `2U` | 摆动幅度 |
| VRB | CH4 | `3U` | 摆动速度 |
| A | CH5 | `4U` | 清洗开关 |
| B | CH6 | `5U` | 摆动开关 |
| C | CH7 | `6U` | 固定模式开关 |
| D | CH8 | `7U` | 当前未使用 |

代码里的通道宏：

```c
#define DS800_CH_STICK_LR 0U
#define DS800_CH_STICK_UD 1U
#define DS800_CH_VRA      2U
#define DS800_CH_VRB      3U
#define DS800_CH_A        4U
#define DS800_CH_B        5U
#define DS800_CH_C        6U
#define DS800_CH_D_UNUSED 7U
```

## 2. 通道数值规则

两段开关判断：

- `raw >= 1350`：认为开关为开
- `raw < 1350`：认为开关为关

适用控件：

- A/CH5
- B/CH6
- C/CH7

VRA/VRB 比例映射：

- 最小值：`192`
- 最大值：`1792`
- 映射范围：`0-100`

公式：

```text
percent = (raw - 192) * 100 / (1792 - 192)
```

带四舍五入，低于 192 按 0，高于 1792 按 100。

例子：

| raw | 映射值 |
| --- | --- |
| 192 | 0 |
| 688 | 31 |
| 696 | 32 |
| 992 | 50 |
| 1792 | 100 |

摇杆方向判断：

- `raw <= 750`：低方向
- `raw >= 1250`：高方向
- 中间：中位

适用控件：

- 摇杆上下/CH2：水压加减
- 摇杆左右/CH1：固定角度加减

## 3. A 清洗开关

通道：

- A = CH5

开关逻辑：

- A 高位：清洗工作流打开
- A 低位：清洗工作流关闭

A 打开时调用：

```text
WaterPump_SetPressurePercent(当前水压设定值)
WaterPump_On()
PwmSwing_Start()
```

状态变化：

- `clean=1`
- `fixed=0`
- 水泵/电磁阀打开
- 摆动尝试启动

注意：

- 如果当前水压设定值是 0，打开 A 时会自动恢复为默认 100。
- A 打开会默认启动摆动，但如果当前摆动幅度是 0，底层会拒绝真正摆动。

A 关闭时调用：

```text
WaterPump_Off()
PwmSwing_Stop()
```

状态变化：

- `clean=0`
- 水泵/电磁阀关闭
- 摆动停止
- 当前水压设定值不清零

## 4. CH2 摇杆上下调水压

通道：

- 摇杆上下 = CH2

控制对象：

- 水泵压力设定值 `s_pumpPressurePercent`

逻辑：

- 上拨：水压 `+5`
- 下拨：水压 `-5`
- 松手回中：保持当前水压

调用：

```text
WaterPump_SetPressurePercent(pressure)
```

范围：

- 最小 0
- 最大 100

防重复逻辑：

- 摇杆拨到上/下方向后，只触发一次。
- 必须回到中位后，下一次拨动才会再次触发。

固定模式限制：

- C 固定模式打开时，CH2 摇杆上下不调水压。
- 这是为了避免 360 度摇杆左右调固定角度时，上下分量误改水压。

持久化：

- 水压设定值会保存到 Flash。
- 保存有约 1 秒延时，避免频繁擦写。

## 5. B 摆动开关

通道：

- B = CH6

开关逻辑：

- B 高位：摆动打开
- B 低位：摆动关闭

B 打开时调用一次：

```text
PwmSwing_Start()
```

B 关闭时调用一次：

```text
PwmSwing_Stop()
```

重要修正：

- 当前 B 只在 CH6 状态变化时触发。
- 不再因为 `B=开` 且 `PwmSwing_IsRunning()==0` 每帧重复调用 `PwmSwing_Start()`。
- 如果摆动幅度为 0，底层 `PwmSwing_Start()` 会拒绝启动，B 不会反复刷调用。

固定模式限制：

- C 固定模式打开时，B 打开会被忽略。
- 因为固定模式和左右摆动互斥。

固定模式退出恢复：

- C 从开变关时，如果 B 仍然保持高位，会恢复一次摆动。
- 只恢复一次，不循环重试。

清洗中逻辑：

- 清洗中允许 B 停止/恢复摆动。
- B 不再被清洗工作流强行忽略。

## 6. VRA 摆动幅度

通道：

- VRA = CH3

控制对象：

- 摆动幅度百分比 `s_swingAmplitudePercent`
- 底层摆动角度 `ampDeg`

映射：

```text
CH3 raw 192-1792 -> amplitude 0-100
```

再换算成角度：

```text
ampDeg = amplitude * PWM_SWING_AMPLITUDE_DEG_MAX / 100
```

当前最大单边摆幅：

```c
#define PWM_SWING_AMPLITUDE_DEG_MAX 36U
```

所以：

| VRA 映射值 | 单边摆幅 | 实际摆动范围 |
| --- | --- | --- |
| 0 | 0 度 | 停在中位 |
| 50 | 18 度 | 90 +/- 18 度 |
| 100 | 36 度 | 90 +/- 36 度 |

调用：

```text
PwmSwing_SetAmplitudeDeg(ampDeg)
```

注意：

- VRA 不是 A/B/C 那种两段开关。
- 当前按通道绝对值映射，不是累计加减。
- 例如 `ch3=696` 约等于 `32%`，`ch3=688` 约等于 `31%`。
- 幅度为 0 时，摆动不会运行；此时 B 打开只会尝试启动一次。

对固定角度的影响：

- 固定角度必须限制在当前 VRA 幅度范围内。
- 如果 VRA 幅度变小，已保存的固定角度会被夹到新范围内。

例子：

- VRA=100，固定角度范围是 54.0-126.0 度。
- VRA=50，固定角度范围是 72.0-108.0 度。
- VRA=0，固定角度只能是 90.0 度。

## 7. VRB 摆动速度

通道：

- VRB = CH4

控制对象：

- 摆动速度百分比 `s_swingSpeedPercent`

映射：

```text
CH4 raw 192-1792 -> speed 0-100
```

调用：

```text
PwmSwing_SetSpeedFromUI(speed)
```

底层换算：

```text
内部速度 = PWM_SWING_SPEED_MIN + speed * (PWM_SWING_SPEED_MAX - PWM_SWING_SPEED_MIN) / 100
```

当前底层范围：

- `PWM_SWING_SPEED_MIN = 1`
- `PWM_SWING_SPEED_MAX = 100`

注意：

- VRB 也是按通道绝对值映射。
- PSDK Widget 的速度显示读取底层实时值。

## 8. C 固定模式

通道：

- C = CH7

开关逻辑：

- C 高位：固定模式打开
- C 低位：固定模式关闭

C 打开时：

```text
如果清洗正在打开：WaterPump_Off()
PwmSwing_Stop()
PwmSwing_SetAngle(保存的固定角度)
```

状态变化：

- `clean=0`
- `fixed=1`
- 摆动停止
- 水泵关闭
- 舵机到保存的固定角度

C 关闭时：

- `fixed=0`
- 如果 B 仍保持高位，恢复一次摆动。
- 如果 A 仍保持高位，协议层会恢复清洗工作流。

优先级：

- C 固定模式优先于 A 清洗和 B 摆动。
- 固定模式打开后，CH1 用于调固定角度。
- 固定模式打开后，CH2 水压调整被忽略。

## 9. CH1 摇杆左右调固定角度

通道：

- 摇杆左右 = CH1

生效条件：

- 只有 C 固定模式打开时生效。

逻辑：

- 右拨：固定角度 `+1.0 度`
- 左拨：固定角度 `-1.0 度`
- 松手回中：保持当前固定角度

内部单位：

- `angle_x10=900` 表示 90.0 度
- 每次步进 `10`，即 1.0 度

调用：

```text
PwmSwing_SetAngle(angle)
```

范围限制：

固定角度被限制在当前 VRA 摆动幅度内：

```text
min = 90.0 - 当前单边摆幅
max = 90.0 + 当前单边摆幅
```

持久化：

- 固定角度会保存到 Flash。
- 下次打开 C 固定模式时，会直接回到保存的角度。

## 10. D 未使用

通道：

- D = CH8

当前状态：

- 未绑定动作。
- 只会在 SBUS 原始日志里看到 `ch8`。

## 11. 失控保护

如果 SBUS 帧标记：

- `lost=1`
- 或 `fs=1`

立即执行：

```text
WaterPump_Off()
PwmSwing_Stop()
clean=0
fixed=0
```

用途：

- 接收机失联或 failsafe 时，关闭水泵和摆动。

## 12. 参数保存

当前保存到 Flash 的 DS800 参数：

- 水压设定值 `pressurePercent`
- 固定角度 `fixedAngleX10`

保存地址：

```c
APPLICATION_PARAM_STORE_ADDRESS + DS800_PARAM_STORE_OFFSET
```

偏移：

```c
#define DS800_PARAM_STORE_OFFSET 0x00001000UL
```

保存策略：

- 参数变化后标记 dirty。
- 约 1 秒后写入 Flash。
- 保存前会保留 OTA 状态区域，避免覆盖 OTA 信息。

当前不保存：

- VRA 摆动幅度
- VRB 摆动速度
- A/B/C 开关状态

## 13. PSDK Widget 同步

当前遥控器改动会同步给 PSDK Widget 的读取值。

Widget 读取逻辑：

| PSDK 控件 | GetValue 返回 |
| --- | --- |
| 水泵开关 | `WaterPump_GetSwitchState()` |
| 摆动开关 | `PwmSwing_IsRunning()` |
| 水压比例 | `WaterPump_GetPressurePercent()` |
| 摆动速度 | `PwmSwing_GetSpeedPercent()` |
| 摆动幅度 | `PwmSwing_GetAmplitudePercent()` |

注意：

- 固件侧是更新 `GetWidgetValue()` 返回值。
- DJI App 是否实时刷新滑块，取决于 App 是否周期查询 Widget 值。
- 打开界面或刷新后，应能读到遥控器修改后的真实状态。

## 14. 调试打印

调试宏统一在：

```c
DJI_PSDK/APP/ds800_protocol/ds800_protocol.h
```

当前宏：

```c
#define DS800_ENABLE_FLOW_LOG 1U
#define DS800_ENABLE_CHANNEL_TRACE 1U
```

每秒 SBUS 状态打印：

```text
[DS800 SBUS] raw=... err=... ok=... bad=... lost=0 fs=0
ch1=... ch2=... ch3=... ch4=... ch5=... ch6=... ch7=... ch8=...
p=... sp=... amp=... clean=... fix=... angle_x10=...
```

字段含义：

| 字段 | 含义 |
| --- | --- |
| `raw` | UART6 收到的原始字节计数 |
| `err` | UART6 行错误清除计数 |
| `ok` | 成功解析的 SBUS 帧数 |
| `bad` | 错帧计数 |
| `lost` | SBUS lost 标志 |
| `fs` | SBUS failsafe 标志 |
| `p` | 当前水压设定值 |
| `sp` | 当前摆动速度百分比 |
| `amp` | 当前摆动幅度百分比 |
| `clean` | 清洗状态 |
| `fix` | 固定模式状态 |
| `angle_x10` | 固定角度，单位 0.1 度 |

动作调用打印示例：

```text
[DS800 CTRL] A(CH5) clean_on -> WaterPump_On()
[DS800 CTRL] B(CH6) swing_on -> PwmSwing_Start()
[DS800 CTRL] VRA(CH3) amplitude=31 -> PwmSwing_SetAmplitudeDeg()
[DS800 CTRL] VRB(CH4) speed=50 -> PwmSwing_SetSpeedFromUI()
[DS800 CTRL] stickUD(CH2) pressure=75 -> WaterPump_SetPressurePercent()
[DS800 CTRL] stickLR(CH1) fixed_angle_x10=910 -> PwmSwing_SetAngle()
```

通道追踪打印示例：

```text
[DS800 CH] VRA(CH3) raw=688 dir=1 mapped=31 amplitude_set=32 control=swing_amplitude
```

注意：

- `[DS800 CH]` 是通道变化追踪，不一定等于控制函数调用。
- `[DS800 CTRL]` 才表示实际调用了控制函数。

## 15. 当前需要重点确认的问题

建议你逐项确认：

1. A 打开是否应该同时打开水泵/电磁阀、套用当前水压、启动摆动。
2. A 关闭是否应该同时关闭水泵/电磁阀并停止摆动。
3. B 在清洗中是否允许单独停止/恢复摆动。当前是允许。
4. B 打开但 VRA 幅度为 0 时，是否接受“不摆动，只调用一次启动”。
5. C 固定模式打开时，是否应该关闭清洗水泵。当前会关闭。
6. C 固定模式关闭后，如果 A/B 还保持高位，是否应该恢复清洗/摆动。当前会恢复。
7. VRA/VRB 是否确认按绝对通道值映射，而不是每按一次加减。
8. VRA 是否需要保存到 Flash。当前不保存。
9. VRB 是否需要保存到 Flash。当前不保存。
10. D 是否继续不用，还是预留成其它功能。

