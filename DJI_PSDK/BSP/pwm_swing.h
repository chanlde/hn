/**
 ********************************************************************
 * @file    pwm_swing.h
 * @version V8.0.0
 * @brief   PWM servo swing - Industrial grade
 ********************************************************************/

#ifndef PWM_SWING_H
#define PWM_SWING_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/* 单边最大摆幅唯一配置点：改这里即可同步底层 PWM、MQTT 校验和 PSDK Widget 默认范围。 */
#define PWM_SWING_AMPLITUDE_DEG_MAX     40U
#define PWM_SWING_AMPLITUDE_DEG_MAX_F   ((float)PWM_SWING_AMPLITUDE_DEG_MAX)
#define PWM_SWING_CALIBRATION_DEG_MAX   90U
#define PWM_SWING_CALIBRATION_DEG_MAX_F ((float)PWM_SWING_CALIBRATION_DEG_MAX)
#define PWM_SWING_CENTER_ANGLE_DEG      90.0f
#define PWM_SWING_CENTER_VALUE_US       1500U
#define PWM_SWING_NEG_US_PER_DEG        11U
#define PWM_SWING_POS_US_PER_DEG        9U

/* 左右端点微调（单位：度）。
 * LEFT 为小于 90° 的一侧：+1.0f 表示左边多摆 1°，-1.0f 表示左边少摆 1°。
 * RIGHT 为大于 90° 的一侧：+1.0f 表示右边多摆 1°，-1.0f 表示右边少摆 1°。
 * 例：左边偏大 1° 就填 -1.0f；右边偏小 1° 就填 +1.0f。
 */
#define PWM_SWING_LEFT_ENDPOINT_TRIM_DEG  -6.0f
#define PWM_SWING_RIGHT_ENDPOINT_TRIM_DEG  6.0f

typedef enum {
    WATER_PUMP_PRESSURE_CTRL_PWM = 0,
    WATER_PUMP_PRESSURE_CTRL_GPIO_LEVEL_TEST = 1,
} E_WaterPumpPressureCtrlMode;

/* Temporary pump-pressure test mode:
 * PWM mode: percent controls PB14/TIM12 duty.
 * GPIO level mode: percent == 0 drives PB14 low, percent != 0 drives PB14 high.
 */
#define WATER_PUMP_PRESSURE_CTRL_MODE  WATER_PUMP_PRESSURE_CTRL_PWM

/* Pump pressure PWM per BLTS46 spec:
 * 50~1000Hz accepted, 200Hz recommended.
 * 0~10% = stop, 10~90% = linear speed, 90~100% = max speed.
 */
#define WATER_PUMP_PRESSURE_PWM_FREQ_HZ      200U
#define WATER_PUMP_PRESSURE_INPUT_MAX        100U
#define WATER_PUMP_PRESSURE_DUTY_STOP        0U
#define WATER_PUMP_PRESSURE_DUTY_MIN_RUN     10U
#define WATER_PUMP_PRESSURE_DUTY_MAX_RUN     90U

/* Pump switch safety timeout: auto close pump PWM and relay/valve after switch stays on. */
#define WATER_PUMP_AUTO_OFF_TIMEOUT_MS       (5U * 60U * 1000U)

#define PWM_SWING_MIN_VALUE_US          (PWM_SWING_CENTER_VALUE_US - (PWM_SWING_AMPLITUDE_DEG_MAX * PWM_SWING_NEG_US_PER_DEG))
#define PWM_SWING_MAX_VALUE_US          (PWM_SWING_CENTER_VALUE_US + (PWM_SWING_AMPLITUDE_DEG_MAX * PWM_SWING_POS_US_PER_DEG))
#define PWM_SWING_CALIBRATION_MIN_VALUE_US (PWM_SWING_CENTER_VALUE_US - (PWM_SWING_CALIBRATION_DEG_MAX * PWM_SWING_NEG_US_PER_DEG))
#define PWM_SWING_CALIBRATION_MAX_VALUE_US (PWM_SWING_CENTER_VALUE_US + (PWM_SWING_CALIBRATION_DEG_MAX * PWM_SWING_POS_US_PER_DEG))

/* 函数声明 */
void PwmSwing_Task(void *pvParameters);
void PwmSwing_Start(void);
void PwmSwing_Stop(void);
void PwmSwing_SetSpeed(uint32_t speed);
void PwmSwing_SetAmplitude(uint32_t amplitude);
uint32_t PwmSwing_GetSpeed(void);
uint32_t PwmSwing_GetAmplitude(void);
uint32_t PwmSwing_GetSpeedPercent(void);
uint32_t PwmSwing_GetAmplitudePercent(void);
uint8_t PwmSwing_IsRunning(void);
/** 返回相对中点的角度偏移（°），范围由 PWM_SWING_AMPLITUDE_DEG_MAX 决定 */
float PwmSwing_GetAngle(void);
uint16_t PwmSwing_GetPwm(void);
uint32_t PwmSwing_GetCurrentPwm(void);

/* UI接口 */
void PwmSwing_SetSpeedFromUI(uint32_t uiSpeed);
void PwmSwing_SetAmplitudeFromUI(uint32_t uiValue);

/** min/maxAngle：绝对角度 0~180°，内部会按 PWM_SWING_AMPLITUDE_DEG_MAX 限幅 */
void PwmSwing_SetRange(float minAngle, float maxAngle);

/** 单边摆幅（°，0~PWM_SWING_AMPLITUDE_DEG_MAX）：内部等价为 SetRange(90-amplitudeDeg, 90+amplitudeDeg) */
void PwmSwing_SetAmplitudeDeg(float amplitudeDeg);
void PwmSwing_SetEndpointTrimDeg(float leftTrimDeg, float rightTrimDeg);
void PwmSwing_GetEndpointTrimDeg(float *leftTrimDeg, float *rightTrimDeg);

/** angle：绝对喷洒角 0~180°（与协议一致）；内部 offset = angle - 90°，clamp 后映射脉宽 */
void PwmSwing_SetAngle(float angle);

/** 相对中点角度偏移（°），范围由 PWM_SWING_AMPLITUDE_DEG_MAX 决定；等价于 SetAngle(offset + 90°) */
void PwmSwing_SetAngleOffset(float offset_deg);
void PwmSwing_SetPwmRange(uint32_t minPwm, uint32_t maxPwm);

/* 统一初始化（舵机 PWM_FTS2@PA8 + 水泵压力 PWM_PUMP@PB14/TIM12 + PwmSwing + 舵机电流 ADC） */
void DevCtrl_Init(void);

/* 水泵继电器：PE11；水泵压力 PWM：PB15 / TIM12_CH2（见 DevCtrl_Init） */
void WaterPump_On(void);
void WaterPump_Off(void);
uint8_t WaterPump_GetState(void);
uint8_t WaterPump_GetSwitchState(void);
void WaterPump_SetPressurePercent(uint32_t percent);
uint32_t WaterPump_GetPressurePercent(void);

/* 舵机使能：PC9 */
void ServoEnable_On(void);
void ServoEnable_Off(void);
uint8_t ServoEnable_GetState(void);

#ifdef __cplusplus
}
#endif

#endif
