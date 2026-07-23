# HOTRC DS800 SBUS 控制说明

## 1. 接线

HOTRC v1.03 SBUS 接收板对频后会持续输出 SBUS 帧。

| 接收板 | MCU |
| --- | --- |
| S / SBUS | USART6_RX / PC7 |
| - / GND | GND |
| + / VCC | 按接收板规格供电 |

固件配置：
- 串口：USART6
- 波特率：100000
- 格式：8E2
- RX 反相：开启
- 服务任务：`ds800_task`

如果接收板输出的是非反相 SBUS，把 `APP/Core/Src/freertos.c` 里的：

```c
UART_InitSbus(UART_NUM_6, 1U);
```

改为：

```c
UART_InitSbus(UART_NUM_6, 0U);
```

## 2. 遥控器设置

在 DS800 触摸屏里进入：

```text
通道定义
```

把 `A/B/C/D` 设置成：

```text
两段
```

这样 A/B/C/D 会保持在高位或低位，固件按电平直接控制：
- 高位 = 开
- 低位 = 关

## 3. 当前功能映射

| 功能 | 遥控器控件 | 默认 SBUS 通道 | 固件动作 |
| --- | --- | --- | --- |
| 清洗开关 | A | CH5 | 高=清洗工作流开，低=清洗工作流关 |
| 水泵压力 | 摇杆上下 | CH2 | 上拨加 5%，下拨减 5%，松手保持 |
| 摇摆开关 | B | CH6 | 高=摆动开，低=摆动关 |
| 摇摆角度 | VRA | CH3 | 连续映射到 0-最大摆幅 |
| 摇摆速度 | VRB | CH4 | 连续映射到 0-100% 速度 |
| 固定开关 | C | CH7 | 高=固定模式开，低=固定模式关 |
| 固定角度 | 摇杆左右 | CH1 | 固定模式下左/右拨动调角度，松手保持 |
| 备用 | D | 不使用 | 未绑定动作 |

代码位置：

```c
DJI_PSDK/APP/ds800_protocol/ds800_protocol.c
```

通道宏：

```c
#define DS800_CH_STICK_LR  0U
#define DS800_CH_STICK_UD  1U
#define DS800_CH_VRA       2U
#define DS800_CH_VRB       3U
#define DS800_CH_A         4U
#define DS800_CH_B         5U
#define DS800_CH_C         6U
#define DS800_CH_D_UNUSED  7U
```

注意：代码通道从 0 开始，`0U` 表示打印里的 SBUS CH1，`2U` 表示打印里的 SBUS CH3。

## 4. 控制规则

- A 两段：高位打开清洗工作流，低位关闭清洗工作流。
- 清洗工作流打开时：使用记忆水压，打开水泵/电磁阀，启动摆动。
- 清洗工作流关闭时：关闭水泵/电磁阀，停止摆动；记忆水压不清零。
- B 两段：高位摆动开，低位摆动关；清洗过程中也允许用 B 停止/恢复摇摆。
- C 两段：高位固定模式开，低位固定模式关。
- C 固定模式优先于 A 清洗和 B 摆动；固定模式打开时会停止清洗并停止摆动。
- C 关掉后，如果 B 仍保持高位，固件会恢复摆动。
- 摇杆上下：上拨水压加 5%，下拨水压减 5%，回中后保持当前水压；固定模式下摇杆上下会被忽略，避免 360 度摇杆左右调角度时误改水压。
- VRA：直接控制摆动角度/摆幅。
- VRB：直接控制摆动速度。
- 摇杆左右：固定模式打开时，左/右拨动调固定角度，回中后保持；固定角度会限制在当前 VRA 摇摆角度范围内。
- 水压和固定角度会保存到 Flash，断电后不消失；参数保存延时约 1 秒，避免频繁擦写。
- SBUS lost 或 failsafe 置位时，立即关水泵、停摆动、退出固定模式。

## 5. 打印

详细工作流打印开关统一在：

```c
DJI_PSDK/APP/ds800_protocol/ds800_protocol.h
```

```c
#define DS800_ENABLE_FLOW_LOG 1U
```

`1U` 表示打开 `[DS800 FLOW]` 详细调用日志，`0U` 表示关闭。基础 SBUS 状态和动作状态打印不受这个宏影响。

启动后 UART1 会打印：

```text
[DS800] SBUS ready on USART6 100000 8E2 RX inverted
```

每秒打印一次关键通道值和当前状态：

```text
[DS800 SBUS] raw=1000 err=0 ok=40 bad=0 lost=0 fs=0 ch1=992 ch2=992 ch3=172 ch4=172 ch5=172 ch6=172 ch7=992 ch8=992 p=50 sp=50 amp=50 clean=0 fix=0 angle_x10=900
```

执行动作时会打印：

```text
[DS800] A clean on pump=1 pressure=75 swing=0 clean=1 fixed=0 angle_x10=900 speed=50 amp=50
[DS800] A clean swing on pump=1 pressure=75 swing=1 clean=1 fixed=0 angle_x10=900 speed=50 amp=50
[DS800] C fixed on pump=0 pressure=75 swing=0 clean=0 fixed=1 angle_x10=900 speed=50 amp=50
```

打开详细工作流日志后，还会看到关键调用链：

```text
[DS800 CTRL] A(CH5) clean_on -> WaterPump_SetPressurePercent(75)
[DS800 CTRL] A(CH5) clean_on -> WaterPump_On()
[DS800 CTRL] A(CH5) clean_on -> PwmSwing_Start()
[DS800 CTRL] B(CH6) swing_off -> PwmSwing_Stop()
[DS800 CTRL] VRA(CH3) amplitude=40 -> PwmSwing_SetAmplitudeDeg()
[DS800 CTRL] VRB(CH4) speed=60 -> PwmSwing_SetSpeedFromUI()
[DS800 CTRL] stickUD(CH2) pressure=80 -> WaterPump_SetPressurePercent()
[DS800 CTRL] stickLR(CH1) fixed_angle_x10=910 -> PwmSwing_SetAngle()
[DS800 FLOW] pressure ignored: fixed mode active
[DS800 FLOW] param save ok
```

`angle_x10=900` 表示 90.0 度，`angle_x10=720` 表示 72.0 度。

## 6. 联调方法

1. 烧录后先看 `ok` 是否持续增加，`lost=0 fs=0`。
2. 在遥控器 `通道定义` 里确认 A/B/C/D 都是 `两段`。
3. 分别动作 A、B、C、D、摇杆上下、摇杆左右、VRA、VRB。
4. 看打印里的 `ch1/ch2/ch3/ch4/ch5/ch6/ch7/ch8` 哪个变化。
5. 如果某个控件不是默认通道，只改 `DS800_CH_*` 宏即可。
6. 如果方向反了，优先在遥控器 `通道正反` 里改；也可以在固件里把百分比做 `100 - percent`。
