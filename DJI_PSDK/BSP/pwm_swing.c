/**
 ********************************************************************
 * @file    pwm_swing.c
 * @version V2.0.0
 * @date    2024/12/XX
 * @brief   PWM servo swing implementation for STM32H743 - Signal count based control
 *
 * @copyright (c) 2021 DJI. All rights reserved.
 *
 * All information contained herein is, and remains, the property of DJI.
 * The intellectual and technical concepts contained herein are proprietary
 * to DJI and may be covered by U.S. and foreign patents, patents in process,
 * and protected by trade secret or copyright law.  Dissemination of this
 * information, including but not limited to data and other proprietary
 * material(s) incorporated within the information, in any form, is strictly
 * prohibited without the express written consent of DJI.
 *
 * If you receive this source code without DJI's authorization, you may not
 * further disseminate the information, and you must immediately remove the
 * source code and notify DJI of its removal. DJI reserves the right to pursue
 * legal actions against you for any loss(es) or damage(s) caused by your
 * failure to do so.
 *
 *********************************************************************
 */

/* Includes ------------------------------------------------------------------*/
#include "pwm_swing.h"
#include "pwm.h"
#include "gpio.h"
#include "servo_current.h"
#include "mqtt_app_config.h"
#include "uart.h"
#include "FreeRTOS.h"
#include "task.h"
#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

/* Private constants ---------------------------------------------------------*/
/* PWM摆动基本参数 */
#define PWM_SWING_SPEED_MIN            1       /* 最小速度 */
#define PWM_SWING_SPEED_MAX            100     /* 最大速度 */
#define PWM_SWING_SPEED_DEFAULT        50       /* 默认速度 */
#define PWM_SWING_CENTER_VALUE          PWM_SWING_CENTER_VALUE_US
#define PWM_SWING_OFFSET_NEG_MAX_DEG    PWM_SWING_CALIBRATION_DEG_MAX_F
#define PWM_SWING_OFFSET_POS_MAX_DEG    PWM_SWING_CALIBRATION_DEG_MAX_F
#define PWM_SWING_MIN_VALUE             PWM_SWING_MIN_VALUE_US
#define PWM_SWING_MAX_VALUE             PWM_SWING_MAX_VALUE_US

/* 舵机控制参数 */
#define PWM_SERVO_FREQ_HZ              50      /* 舵机频率：50Hz（20ms周期） */
#define PWM_MIN_SIGNALS                100     /* 最快时的信号数（约 2 秒一个摆动周期） */
#define PWM_MAX_SIGNALS_SLOW           500     /* 最慢时的信号数（10秒一个周期） */

/* 速度曲线参数（梯形曲线） */
#define PWM_ACCEL_RATIO_MIN            0.3f    /* 最慢时加速段比例：30% */
#define PWM_ACCEL_RATIO_MAX            0.2f    /* 最快时加速段比例：20% */
/* 减速段与加速段对称，匀速段 = 1 - 2×加速段比例 */

#define PWM_SMOOTH_PERIOD_MS           20U
#define PWM_SMOOTH_TRANSITION_MS       500U
#define PWM_SMOOTH_STEPS               (PWM_SMOOTH_TRANSITION_MS / PWM_SMOOTH_PERIOD_MS)
#define PWM_SPEED_STEP_PER_TICK        4U
#define PWM_RANGE_STEP_PER_TICK        80U

/* 运动阶段定义 */
typedef enum {
    PWM_STATE_IDLE = 0,
    PWM_STATE_ACCEL_RISE,      /* 从最小脉宽加速上升 */
    PWM_STATE_CONSTANT_RISE,    /* 匀速上升 */
    PWM_STATE_DECEL_RISE,       /* 减速上升到最大脉宽 */
    PWM_STATE_ACCEL_FALL,       /* 从最大脉宽加速下降 */
    PWM_STATE_CONSTANT_FALL,    /* 匀速下降 */
    PWM_STATE_DECEL_FALL        /* 减速下降到最小脉宽 */
} PwmSwingState_t;

/* Private variables ---------------------------------------------------------*/
static volatile uint32_t s_swingSpeed = PWM_SWING_SPEED_DEFAULT;
static volatile uint32_t s_swingSpeedUiPercent = 0U;
static volatile uint32_t s_swingAmplitude = (PWM_SWING_MAX_VALUE - PWM_SWING_MIN_VALUE);
static volatile uint32_t s_swingAmplitudeUiPercent = 100U;
static uint32_t s_lastPwmValue = PWM_SWING_CENTER_VALUE;
static volatile uint8_t s_isEnabled = 0;

/* PC9 舵机电源使能：首次进入摆动控制时拉高一次即可上电（硬件/电源策略保留由用户决定） */
static uint8_t s_servoPowerPrimed = 0U;

/* 摆动未运行时输出的舵机脉宽（串口/协议设角度时写入，避免与 PwmSwing_Task 强制回中位冲突） */
static volatile uint32_t s_holdPwmUs = PWM_SWING_CENTER_VALUE;

/* 状态机变量 */
static PwmSwingState_t s_currentState = PWM_STATE_IDLE;
static uint32_t s_currentPwm = PWM_SWING_CENTER_VALUE;
static float s_currentPwmFloat = (float)PWM_SWING_CENTER_VALUE;
static uint16_t s_signalsPerCycle = 0;       /* 每个摆动周期的 PWM 节拍数（50Hz） */
static uint16_t s_signalsPerMovement = 0;   /* 每个运动阶段的信号数（最小↔最大脉宽） */
static uint16_t s_currentMovementSignals = 0; /* 当前运动阶段已用信号数 */

/* 速度曲线参数 */
static uint16_t s_accelSignals = 0;          /* 加速段信号数 */
static uint16_t s_constantSignals = 0;       /* 匀速段信号数 */
static uint16_t s_decelSignals = 0;          /* 减速段信号数 */
static float s_maxStep = 0.0f;               /* 最大步进值 */
static uint16_t s_currentStageIndex = 0;     /* 当前阶段内的信号索引 */
static volatile uint32_t s_targetSpeed = PWM_SWING_SPEED_DEFAULT;
static uint32_t s_currentSpeed = PWM_SWING_SPEED_DEFAULT;
static volatile uint32_t s_targetMinPwm = PWM_SWING_MIN_VALUE;
static volatile uint32_t s_targetMaxPwm = PWM_SWING_MAX_VALUE;
static uint32_t s_currentMinPwm = PWM_SWING_MIN_VALUE;
static uint32_t s_currentMaxPwm = PWM_SWING_MAX_VALUE;
static volatile uint8_t s_waterPumpEnabled = 0U;
static volatile uint32_t s_waterPumpPressureSetpoint = 0U;
static volatile uint8_t s_waterPumpAutoOffTimerActive = 0U;
static volatile TickType_t s_waterPumpOnTick = 0U;
static float s_leftEndpointTrimDeg = PWM_SWING_LEFT_ENDPOINT_TRIM_DEG;
static float s_rightEndpointTrimDeg = PWM_SWING_RIGHT_ENDPOINT_TRIM_DEG;

/* Private functions declaration ---------------------------------------------*/
static uint16_t CalculateSignalsPerCycle(uint32_t speed);
static void CalculateVelocityProfile(uint32_t speed, uint16_t signalsPerCycle);
static uint32_t StepTowardsU32(uint32_t current, uint32_t target, uint32_t maxStep);
static void PwmSwing_UpdateSmoothParams(void);
static void PwmSwing_ApplyEndpointTrim(float *minAngle, float *maxAngle);
static uint32_t WaterPump_MapPressureToDutyPercent(uint32_t pressure);
static void WaterPump_ApplyOutput(void);
static void WaterPump_CheckAutoOff(void);

/* BSP_DEVCTRL_ACTION_LOG：见 mqtt_app_config.h，量产建议 0 */
#if BSP_DEVCTRL_ACTION_LOG
static void DevCtrl_Logf(const char *fmt, ...)
{
    char buf[120];
    va_list ap;

    va_start(ap, fmt);
    (void)vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    buf[sizeof(buf) - 1U] = '\0';

    UART_Write(DJI_CONSOLE_UART_NUM, (const uint8_t *)"[DEV] ", 6);
    UART_Write(DJI_CONSOLE_UART_NUM, (const uint8_t *)buf, (uint16_t)strlen(buf));
    UART_Write(DJI_CONSOLE_UART_NUM, (const uint8_t *)"\r\n", 2);
}
#else
#define DevCtrl_Logf(...) ((void)0)
#endif

/** 相对中点角度偏移（°）→ 脉宽（µs）：范围由 PWM_SWING_AMPLITUDE_DEG_MAX 决定 */
static uint32_t PwmSwing_OffsetDegToPulseUs(float offset_deg)
{
    float o = offset_deg;
    if (o < -PWM_SWING_OFFSET_NEG_MAX_DEG) {
        o = -PWM_SWING_OFFSET_NEG_MAX_DEG;
    }
    if (o > PWM_SWING_OFFSET_POS_MAX_DEG) {
        o = PWM_SWING_OFFSET_POS_MAX_DEG;
    }
    float us;
    if (o <= 0.0f) {
        us = (float)PWM_SWING_CENTER_VALUE + o * (float)PWM_SWING_NEG_US_PER_DEG;
    } else {
        us = (float)PWM_SWING_CENTER_VALUE + o * (float)PWM_SWING_POS_US_PER_DEG;
    }
    if (us < (float)PWM_SWING_CALIBRATION_MIN_VALUE_US) {
        us = (float)PWM_SWING_CALIBRATION_MIN_VALUE_US;
    }
    if (us > (float)PWM_SWING_CALIBRATION_MAX_VALUE_US) {
        us = (float)PWM_SWING_CALIBRATION_MAX_VALUE_US;
    }
    return (uint32_t)(us + 0.5f);
}

/** 脉宽（µs）→ 相对中点角度偏移（°），与 PwmSwing_OffsetDegToPulseUs 互逆（分段） */
static float PwmSwing_PulseUsToOffsetDeg(uint32_t pulse_us)
{
    float pu = (float)pulse_us;
    if (pu <= (float)PWM_SWING_CENTER_VALUE) {
        return (pu - (float)PWM_SWING_CENTER_VALUE) / (float)PWM_SWING_NEG_US_PER_DEG;
    }
    return (pu - (float)PWM_SWING_CENTER_VALUE) / (float)PWM_SWING_POS_US_PER_DEG;
}

/** 对左右摆动端点做机械安装误差微调；只影响摆动范围，不影响单点 SetAngle。 */
static void PwmSwing_ApplyEndpointTrim(float *minAngle, float *maxAngle)
{
    if (minAngle == NULL || maxAngle == NULL) {
        return;
    }

    *minAngle -= s_leftEndpointTrimDeg;
    *maxAngle += s_rightEndpointTrimDeg;

    if (*minAngle < 0.0f) {
        *minAngle = 0.0f;
    }
    if (*maxAngle > 180.0f) {
        *maxAngle = 180.0f;
    }
    if (*minAngle > *maxAngle) {
        float mid = (*minAngle + *maxAngle) * 0.5f;
        *minAngle = mid;
        *maxAngle = mid;
    }
}

/** 绝对角（°）→ 脉宽（µs）：相对 90° 偏移限制由 PWM_SWING_AMPLITUDE_DEG_MAX 决定 */
static uint32_t PwmSwing_AbsoluteAngleToPulseUs(float angle_deg)
{
    float offset = angle_deg - PWM_SWING_CENTER_ANGLE_DEG;
    if (offset < -PWM_SWING_OFFSET_NEG_MAX_DEG) {
        offset = -PWM_SWING_OFFSET_NEG_MAX_DEG;
    }
    if (offset > PWM_SWING_OFFSET_POS_MAX_DEG) {
        offset = PWM_SWING_OFFSET_POS_MAX_DEG;
    }
    return PwmSwing_OffsetDegToPulseUs(offset);
}

/* Exported functions definition ---------------------------------------------*/

/**
 * @brief PWM摆动任务函数
 * @param pvParameters 任务参数（未使用）
 * @note 使用vTaskDelayUntil实现绝对周期，20ms周期（50Hz）
 */
void PwmSwing_Task(void *pvParameters)
{
    float currentStep;
    uint32_t pwmValue;
    TickType_t lastWakeTime;

    (void)pvParameters;

    /* 初始化绝对延时 */
    lastWakeTime = xTaskGetTickCount();

    /* 任务一直运行 */
    for (;;)
    {
        PwmSwing_UpdateSmoothParams();
        WaterPump_CheckAutoOff();

        if (s_isEnabled)
        {
            /* 根据当前状态计算步进并更新PWM值 */
            switch (s_currentState)
            {
                case PWM_STATE_ACCEL_RISE:
                    /* 加速上升：步进逐渐增大 */
                    currentStep = s_maxStep * ((float)s_currentStageIndex / (float)s_accelSignals);
                    s_currentPwmFloat += currentStep;
                    s_currentStageIndex++;
                    if (s_currentStageIndex >= s_accelSignals) {
                        s_currentState = PWM_STATE_CONSTANT_RISE;
                        s_currentStageIndex = 0;
                    }
                    break;

                case PWM_STATE_CONSTANT_RISE:
                    /* 匀速上升：步进固定 */
                    currentStep = s_maxStep;
                    s_currentPwmFloat += currentStep;
                    s_currentStageIndex++;
                    if (s_currentStageIndex >= s_constantSignals) {
                        s_currentState = PWM_STATE_DECEL_RISE;
                        s_currentStageIndex = 0;
                    }
                    break;

                case PWM_STATE_DECEL_RISE:
                    /* 减速上升：步进逐渐减小 */
                    currentStep = s_maxStep * ((float)(s_decelSignals - s_currentStageIndex) / (float)s_decelSignals);
                    s_currentPwmFloat += currentStep;
                    s_currentStageIndex++;
                    if (s_currentStageIndex >= s_decelSignals) {
                        /* 到达摆动上限脉宽，开始下降 */
                        s_currentPwmFloat = (float)s_currentMaxPwm;
                        s_currentState = PWM_STATE_ACCEL_FALL;
                        s_currentStageIndex = 0;
                        s_currentMovementSignals = 0;
                    }
                    break;

                case PWM_STATE_ACCEL_FALL:
                    /* 加速下降：步进逐渐增大 */
                    currentStep = s_maxStep * ((float)s_currentStageIndex / (float)s_accelSignals);
                    s_currentPwmFloat -= currentStep;
                    s_currentStageIndex++;
                    if (s_currentStageIndex >= s_accelSignals) {
                        s_currentState = PWM_STATE_CONSTANT_FALL;
                        s_currentStageIndex = 0;
                    }
                    break;

                case PWM_STATE_CONSTANT_FALL:
                    /* 匀速下降：步进固定 */
                    currentStep = s_maxStep;
                    s_currentPwmFloat -= currentStep;
                    s_currentStageIndex++;
                    if (s_currentStageIndex >= s_constantSignals) {
                        s_currentState = PWM_STATE_DECEL_FALL;
                        s_currentStageIndex = 0;
                    }
                    break;

                case PWM_STATE_DECEL_FALL:
                    /* 减速下降：步进逐渐减小 */
                    currentStep = s_maxStep * ((float)(s_decelSignals - s_currentStageIndex) / (float)s_decelSignals);
                    s_currentPwmFloat -= currentStep;
                    s_currentStageIndex++;
                    if (s_currentStageIndex >= s_decelSignals) {
                        /* 到达摆动下限脉宽，完成一个运动阶段；两阶段为一完整来回，持续循环直至 PwmSwing_Stop */
                        s_currentPwmFloat = (float)s_currentMinPwm;
                        s_currentMovementSignals++;
                        if (s_currentMovementSignals >= 2) {
                            s_currentMovementSignals = 0;
                        }
                        s_currentState = PWM_STATE_ACCEL_RISE;
                        s_currentStageIndex = 0;
                    }
                    break;

                case PWM_STATE_IDLE:
                default:
                    /* 空闲状态，保持中心值 */
                    s_currentPwmFloat = (float)PWM_SWING_CENTER_VALUE;
                    break;
            }

            /* 限制PWM值在有效范围内 */
            if (s_currentPwmFloat > (float)s_currentMaxPwm) {
                s_currentPwmFloat = (float)s_currentMaxPwm;
            } else if (s_currentPwmFloat < (float)s_currentMinPwm) {
                s_currentPwmFloat = (float)s_currentMinPwm;
            }

            pwmValue = (uint32_t)(s_currentPwmFloat + 0.5f);
            s_currentPwm = pwmValue;
            s_lastPwmValue = pwmValue;
            Pwm_SetDutyEx(PWM_FTS2, pwmValue);
        }
        else
        {
            /* 摆动停止时：保持上次设定的静态脉宽（默认中位，可由 PwmSwing_SetAngle 更新） */
            Pwm_SetDutyEx(PWM_FTS2, s_holdPwmUs);
            s_lastPwmValue = s_holdPwmUs;
            s_currentPwm = s_holdPwmUs;
            s_currentPwmFloat = (float)s_holdPwmUs;
            s_currentState = PWM_STATE_IDLE;
        }

        /* 绝对周期延时：确保20ms周期稳定（50Hz） */
        vTaskDelayUntil(&lastWakeTime, pdMS_TO_TICKS(PWM_SMOOTH_PERIOD_MS));  /* 20ms = 50Hz */
    }
}

/**
 * @brief 启动PWM摆动
 */
void PwmSwing_Start(void)
{
    if (s_servoPowerPrimed == 0U) {
        ServoEnable_On(); /* GPIOC PIN9 高：舵机上电 */
        s_servoPowerPrimed = 1U;
    }

    taskENTER_CRITICAL();
    if (s_swingAmplitude == 0U || s_targetMinPwm == s_targetMaxPwm) {
        s_isEnabled = 0;
        s_holdPwmUs = PWM_SWING_CENTER_VALUE;
        s_currentPwm = PWM_SWING_CENTER_VALUE;
        s_currentPwmFloat = (float)PWM_SWING_CENTER_VALUE;
        s_currentState = PWM_STATE_IDLE;
        s_currentStageIndex = 0;
        s_currentMovementSignals = 0;
        s_lastPwmValue = PWM_SWING_CENTER_VALUE;
        taskEXIT_CRITICAL();
        Pwm_SetDutyEx(PWM_FTS2, PWM_SWING_CENTER_VALUE);
        DevCtrl_Logf("PwmSwing_Start ignored: amplitude=0");
        return;
    }

    s_signalsPerCycle = CalculateSignalsPerCycle(s_currentSpeed);
    CalculateVelocityProfile(s_currentSpeed, s_signalsPerCycle);

    s_currentPwm = PWM_SWING_CENTER_VALUE;
    s_currentPwmFloat = (float)PWM_SWING_CENTER_VALUE;
    s_currentState = PWM_STATE_ACCEL_RISE;
    s_currentStageIndex = 0;
    s_currentMovementSignals = 0;

    s_isEnabled = 1;
    taskEXIT_CRITICAL();
    DevCtrl_Logf("PwmSwing_Start speed=%lu amp=%lu", (unsigned long)s_swingSpeed,
                 (unsigned long)s_swingAmplitude);
}

/**
 * @brief 停止PWM正弦波摆动
 */
void PwmSwing_Stop(void)
{
    taskENTER_CRITICAL();
    s_isEnabled = 0;
    s_holdPwmUs = PWM_SWING_CENTER_VALUE;
    s_currentPwmFloat = (float)PWM_SWING_CENTER_VALUE;
    taskEXIT_CRITICAL();
    DevCtrl_Logf("PwmSwing_Stop");
}

/**
 * @brief 设置PWM摆动速度
 * @param speed 摆动速度，范围：PWM_SWING_SPEED_MIN 到 PWM_SWING_SPEED_MAX
 * @note 速度变化时重新计算信号数量和速度曲线参数
 */
void PwmSwing_SetSpeed(uint32_t speed)
{
    uint32_t span = PWM_SWING_SPEED_MAX - PWM_SWING_SPEED_MIN;
    uint32_t uiPercent;

    if (speed < PWM_SWING_SPEED_MIN) {
        speed = PWM_SWING_SPEED_MIN;
    } else if (speed > PWM_SWING_SPEED_MAX) {
        speed = PWM_SWING_SPEED_MAX;
    }
    uiPercent = ((speed - PWM_SWING_SPEED_MIN) * 100U + (span / 2U)) / span;
    taskENTER_CRITICAL();
    s_targetSpeed = speed;
    s_swingSpeed = speed;
    s_swingSpeedUiPercent = uiPercent;
    taskEXIT_CRITICAL();
    DevCtrl_Logf("PwmSwing_SetSpeed=%lu", (unsigned long)speed);
}

/**
 * @brief 获取当前摆动速度
 */
uint32_t PwmSwing_GetSpeed(void)
{
    return s_swingSpeed;
}

/**
 * @brief 获取当前摆动幅度
 */
uint32_t PwmSwing_GetAmplitude(void)
{
    return s_swingAmplitude;
}

uint32_t PwmSwing_GetSpeedPercent(void)
{
    return s_swingSpeedUiPercent;
}

uint32_t PwmSwing_GetAmplitudePercent(void)
{
    return s_swingAmplitudeUiPercent;
}

/**
 * @brief 检查摆动是否正在运行
 */
uint8_t PwmSwing_IsRunning(void)
{
    return s_isEnabled;
}

void PwmSwing_SetSpeedFromUI(uint32_t uiSpeed)
{
    uint32_t speed;
    if (uiSpeed > 100U) {
        uiSpeed = 100U;
    }
    speed = PWM_SWING_SPEED_MIN +
            (uiSpeed * (PWM_SWING_SPEED_MAX - PWM_SWING_SPEED_MIN)) / 100U;
    PwmSwing_SetSpeed(speed);
    taskENTER_CRITICAL();
    s_swingSpeedUiPercent = uiSpeed;
    taskEXIT_CRITICAL();
}

void PwmSwing_SetPwmRange(uint32_t minPwm, uint32_t maxPwm)
{
    if (minPwm < PWM_SWING_CALIBRATION_MIN_VALUE_US) {
        minPwm = PWM_SWING_CALIBRATION_MIN_VALUE_US;
    }
    if (maxPwm > PWM_SWING_CALIBRATION_MAX_VALUE_US) {
        maxPwm = PWM_SWING_CALIBRATION_MAX_VALUE_US;
    }
    if (minPwm > maxPwm) {
        uint32_t tmp = minPwm;
        minPwm = maxPwm;
        maxPwm = tmp;
    }

    taskENTER_CRITICAL();
    s_targetMinPwm = minPwm;
    s_targetMaxPwm = maxPwm;
    s_swingAmplitude = maxPwm - minPwm;
    taskEXIT_CRITICAL();
}

/**
 * @brief 获取当前PWM输出值（用于调试）
 */
uint32_t PwmSwing_GetCurrentPwm(void)
{
    return s_lastPwmValue;
}

/**
 * @brief 协议喷洒绝对角（°，0~180）→ PWM_FTS2；内部按 PWM_SWING_AMPLITUDE_DEG_MAX 限幅
 */
void PwmSwing_SetAngle(float angle_deg)
{
    float minAngle = PWM_SWING_CENTER_ANGLE_DEG - PWM_SWING_AMPLITUDE_DEG_MAX_F;
    float maxAngle = PWM_SWING_CENTER_ANGLE_DEG + PWM_SWING_AMPLITUDE_DEG_MAX_F;
    PwmSwing_ApplyEndpointTrim(&minAngle, &maxAngle);
    if (angle_deg < minAngle) {
        angle_deg = minAngle;
    } else if (angle_deg > maxAngle) {
        angle_deg = maxAngle;
    }
    uint32_t us = PwmSwing_AbsoluteAngleToPulseUs(angle_deg);

    taskENTER_CRITICAL();
    s_isEnabled = 0;
    s_currentState = PWM_STATE_IDLE;
    s_holdPwmUs = us;
    s_currentPwm = us;
    s_currentPwmFloat = (float)us;
    s_currentStageIndex = 0;
    s_lastPwmValue = us;
    taskEXIT_CRITICAL();
    Pwm_SetDutyEx(PWM_FTS2, us);
    DevCtrl_Logf("PwmSwing_SetAngle %.1f deg -> %lu us", (double)angle_deg, (unsigned long)us);
}

void PwmSwing_SetAngleOffset(float offset_deg)
{
    PwmSwing_SetAngle(offset_deg + 90.0f);
}

float PwmSwing_GetAngle(void)
{
    return PwmSwing_PulseUsToOffsetDeg(s_lastPwmValue);
}

uint16_t PwmSwing_GetPwm(void)
{
    return (uint16_t)s_lastPwmValue;
}

/**
 * @brief 按绝对角（0~180°）映射到脉宽并调用 SetPwmRange（与 SetAngle 同一套曲线）
 */
void PwmSwing_SetRange(float minAngle, float maxAngle)
{
    float a0 = minAngle;
    float a1 = maxAngle;
    if (a0 > a1) {
        float t = a0;
        a0 = a1;
        a1 = t;
    }
    if (a0 < 0.0f) {
        a0 = 0.0f;
    }
    if (a1 > 180.0f) {
        a1 = 180.0f;
    }
    PwmSwing_ApplyEndpointTrim(&a0, &a1);
    uint32_t minPwm = PwmSwing_AbsoluteAngleToPulseUs(a0);
    uint32_t maxPwm = PwmSwing_AbsoluteAngleToPulseUs(a1);
    PwmSwing_SetPwmRange(minPwm, maxPwm);
}

void PwmSwing_SetEndpointTrimDeg(float leftTrimDeg, float rightTrimDeg)
{
    if (leftTrimDeg < -PWM_SWING_CALIBRATION_DEG_MAX_F) {
        leftTrimDeg = -PWM_SWING_CALIBRATION_DEG_MAX_F;
    } else if (leftTrimDeg > PWM_SWING_CALIBRATION_DEG_MAX_F) {
        leftTrimDeg = PWM_SWING_CALIBRATION_DEG_MAX_F;
    }

    if (rightTrimDeg < -PWM_SWING_CALIBRATION_DEG_MAX_F) {
        rightTrimDeg = -PWM_SWING_CALIBRATION_DEG_MAX_F;
    } else if (rightTrimDeg > PWM_SWING_CALIBRATION_DEG_MAX_F) {
        rightTrimDeg = PWM_SWING_CALIBRATION_DEG_MAX_F;
    }

    taskENTER_CRITICAL();
    s_leftEndpointTrimDeg = leftTrimDeg;
    s_rightEndpointTrimDeg = rightTrimDeg;
    taskEXIT_CRITICAL();

    PwmSwing_SetAmplitudeDeg((float)s_swingAmplitudeUiPercent * PWM_SWING_AMPLITUDE_DEG_MAX_F / 100.0f);
}

void PwmSwing_GetEndpointTrimDeg(float *leftTrimDeg, float *rightTrimDeg)
{
    if (leftTrimDeg != NULL) {
        *leftTrimDeg = s_leftEndpointTrimDeg;
    }
    if (rightTrimDeg != NULL) {
        *rightTrimDeg = s_rightEndpointTrimDeg;
    }
}

void PwmSwing_SetAmplitudeDeg(float amplitudeDeg)
{
    float a = amplitudeDeg;
    float scale;
    float leftLimitDeg;
    float rightLimitDeg;
    uint32_t uiPercent;

    if (a < 0.0f) {
        a = 0.0f;
    } else if (a > PWM_SWING_AMPLITUDE_DEG_MAX) {
        a = PWM_SWING_AMPLITUDE_DEG_MAX;
    }

    uiPercent = (uint32_t)((a * 100.0f / PWM_SWING_AMPLITUDE_DEG_MAX_F) + 0.5f);
    if (uiPercent > 100U) {
        uiPercent = 100U;
    }

    if (a <= 0.0f) {
        taskENTER_CRITICAL();
        s_targetMinPwm = PWM_SWING_CENTER_VALUE;
        s_targetMaxPwm = PWM_SWING_CENTER_VALUE;
        s_currentMinPwm = PWM_SWING_CENTER_VALUE;
        s_currentMaxPwm = PWM_SWING_CENTER_VALUE;
        s_swingAmplitude = 0U;
        s_isEnabled = 0;
        s_holdPwmUs = PWM_SWING_CENTER_VALUE;
        s_currentPwm = PWM_SWING_CENTER_VALUE;
        s_currentPwmFloat = (float)PWM_SWING_CENTER_VALUE;
        s_currentState = PWM_STATE_IDLE;
        s_currentStageIndex = 0;
        s_currentMovementSignals = 0;
        s_lastPwmValue = PWM_SWING_CENTER_VALUE;
        s_swingAmplitudeUiPercent = 0U;
        taskEXIT_CRITICAL();
        Pwm_SetDutyEx(PWM_FTS2, PWM_SWING_CENTER_VALUE);
        DevCtrl_Logf("PwmSwing_SetAmplitudeDeg=0, hold center");
        return;
    }

    scale = a / PWM_SWING_AMPLITUDE_DEG_MAX_F;
    leftLimitDeg = (PWM_SWING_AMPLITUDE_DEG_MAX_F + s_leftEndpointTrimDeg) * scale;
    rightLimitDeg = (PWM_SWING_AMPLITUDE_DEG_MAX_F + s_rightEndpointTrimDeg) * scale;
    if (leftLimitDeg < 0.0f) {
        leftLimitDeg = 0.0f;
    }
    if (rightLimitDeg < 0.0f) {
        rightLimitDeg = 0.0f;
    }
    PwmSwing_SetPwmRange(PwmSwing_AbsoluteAngleToPulseUs(PWM_SWING_CENTER_ANGLE_DEG - leftLimitDeg),
                         PwmSwing_AbsoluteAngleToPulseUs(PWM_SWING_CENTER_ANGLE_DEG + rightLimitDeg));
    taskENTER_CRITICAL();
    s_swingAmplitudeUiPercent = uiPercent;
    taskEXIT_CRITICAL();
}

void DevCtrl_Init(void)
{
    Pwm_Init(PWM_FTS2, 50, 1500);
    if (WATER_PUMP_PRESSURE_CTRL_MODE == WATER_PUMP_PRESSURE_CTRL_GPIO_LEVEL_TEST) {
        WaterPumpPressurePin_InitGpioOutput();
        WaterPumpPressurePin_Write(0U);
    } else {
        Pwm_Init(PWM_PUMP, WATER_PUMP_PRESSURE_PWM_FREQ_HZ, 0);
    }
    PwmSwing_SetAmplitudeDeg(PWM_SWING_AMPLITUDE_DEG_MAX_F);
    PwmSwing_SetSpeed(1);
    PwmSwing_Stop();
    ServoCurrent_Init();
}

void WaterPump_On(void)
{
    s_waterPumpEnabled = 1U;
    s_waterPumpOnTick = xTaskGetTickCount();
    s_waterPumpAutoOffTimerActive = 1U;
    WaterPump_ApplyOutput();
    DevCtrl_Logf("WaterPump_On setpoint=%lu timeout=%lu ms",
                 (unsigned long)s_waterPumpPressureSetpoint,
                 (unsigned long)WATER_PUMP_AUTO_OFF_TIMEOUT_MS);
}

void WaterPump_Off(void)
{
    s_waterPumpEnabled = 0U;
    s_waterPumpAutoOffTimerActive = 0U;
    s_waterPumpOnTick = 0U;
    WaterPump_ApplyOutput();
    DevCtrl_Logf("WaterPump_Off: relay off, pump PWM stop");
    DevCtrl_Logf("WaterPump_Off");
}

uint8_t WaterPump_GetState(void)
{
    return (s_waterPumpEnabled != 0U && s_waterPumpPressureSetpoint != 0U) ? 1U : 0U;
}

uint8_t WaterPump_GetSwitchState(void)
{
    return (s_waterPumpEnabled != 0U) ? 1U : 0U;
}

void WaterPump_SetPressurePercent(uint32_t percent)
{
    if (percent > WATER_PUMP_PRESSURE_INPUT_MAX) {
        percent = WATER_PUMP_PRESSURE_INPUT_MAX;
    }

    s_waterPumpPressureSetpoint = percent;

    WaterPump_ApplyOutput();
    DevCtrl_Logf("WaterPump_SetPressurePercent setpoint=%lu enabled=%u",
                 (unsigned long)s_waterPumpPressureSetpoint, (unsigned)s_waterPumpEnabled);
}

uint32_t WaterPump_GetPressurePercent(void)
{
    return s_waterPumpPressureSetpoint;
}

void ServoEnable_On(void)
{
    ServoEnable_Write(1U);
    DevCtrl_Logf("ServoEnable_On");
}

void ServoEnable_Off(void)
{
    ServoEnable_Write(0U);
    DevCtrl_Logf("ServoEnable_Off");
}

uint8_t ServoEnable_GetState(void)
{
    return ServoEnable_Read();
}


/* Private functions definition-----------------------------------------------*/

/**
 * @brief 计算每个周期的信号数（根据速度计算周期时间）
 * @param speed 速度值（1-100）
 * @return 每个周期的信号数
 */
static uint16_t CalculateSignalsPerCycle(uint32_t speed)
{
    /* 速度1=10秒/周期，速度100=2秒/周期，线性映射 */
    if (speed < PWM_SWING_SPEED_MIN) {
        speed = PWM_SWING_SPEED_MIN;
    } else if (speed > PWM_SWING_SPEED_MAX) {
        speed = PWM_SWING_SPEED_MAX;
    }
    
    /* 计算周期时间：速度1=10秒，速度100=2秒 */
    float cycleTime = 10.0f - (speed - 1) * 9.5f / 99.0f;  /* 10秒到2秒线性映射 */
    
    /* 每个周期的信号数 = 周期时间 × 频率 */
    uint16_t signalsPerCycle = (uint16_t)(cycleTime * PWM_SERVO_FREQ_HZ + 0.5f);
    if (signalsPerCycle < PWM_MIN_SIGNALS) signalsPerCycle = PWM_MIN_SIGNALS;  /* 最小100（2秒） */
    if (signalsPerCycle > PWM_MAX_SIGNALS_SLOW) signalsPerCycle = PWM_MAX_SIGNALS_SLOW;  /* 最大500（10秒） */
    
    return signalsPerCycle;
}

/**
 * @brief 计算速度曲线参数
 * @param speed 速度值（1-100）
 * @param signalsPerCycle 每个周期的信号数
 */
static void CalculateVelocityProfile(uint32_t speed, uint16_t signalsPerCycle)
{
    int32_t constantSignals;
    float totalDistance;
    float effectiveSignals;

    /* 根据速度计算加速段比例（线性变化） */
    /* 慢速时加减速段更长（30%），快速时匀速段更长（20%） */
    float accelRatio = PWM_ACCEL_RATIO_MAX + 
                      (speed - 1) * (PWM_ACCEL_RATIO_MIN - PWM_ACCEL_RATIO_MAX) / 99.0f;
    
    /* 计算各阶段信号数（每个周期内每个运动阶段的信号数） */
    /* 注意：每个周期包含两个运动阶段（下限脉宽↔上限脉宽） */
    /* 所以每个运动阶段的信号数 = signalsPerCycle / 2 */
    s_signalsPerMovement = signalsPerCycle / 2;
    if (s_signalsPerMovement == 0) s_signalsPerMovement = 1;
    
    s_accelSignals = (uint16_t)(s_signalsPerMovement * accelRatio);
    if (s_accelSignals == 0) s_accelSignals = 1;
    
    s_decelSignals = s_accelSignals;  /* 与加速段对称 */
    constantSignals = (int32_t)s_signalsPerMovement - (int32_t)s_accelSignals - (int32_t)s_decelSignals;
    if (constantSignals < 0) {
        constantSignals = 0;
    }
    s_constantSignals = (uint16_t)constantSignals;
    
    /* 计算最大步进（基于总距离和信号数） */
    /* 梯形曲线：距离 = 0.5 * accel * t1^2 + v_max * t2 + 0.5 * decel * t3^2 */
    /* 简化：假设匀加速和匀减速，最大步进 = 总距离 / (0.5 * accel + constant + 0.5 * decel) */
    totalDistance = (float)(s_currentMaxPwm - s_currentMinPwm);
    if (totalDistance < 1.0f) {
        totalDistance = 1.0f;
    }
    effectiveSignals = s_accelSignals * 0.5f + s_constantSignals + s_decelSignals * 0.5f;
    if (effectiveSignals > 0) {
        s_maxStep = totalDistance / effectiveSignals;
    } else {
        s_maxStep = 0.0f;
    }
}

static uint32_t StepTowardsU32(uint32_t current, uint32_t target, uint32_t maxStep)
{
    if (current < target) {
        uint32_t delta = target - current;
        if (delta > maxStep) {
            return current + maxStep;
        }
        return target;
    }
    if (current > target) {
        uint32_t delta = current - target;
        if (delta > maxStep) {
            return current - maxStep;
        }
        return target;
    }
    return current;
}

static uint32_t WaterPump_MapPressureToDutyPercent(uint32_t pressure)
{
    uint32_t span;

    if (pressure > WATER_PUMP_PRESSURE_INPUT_MAX) {
        pressure = WATER_PUMP_PRESSURE_INPUT_MAX;
    }
    if (pressure == 0U) {
        return WATER_PUMP_PRESSURE_DUTY_STOP;
    }

    span = WATER_PUMP_PRESSURE_DUTY_MAX_RUN - WATER_PUMP_PRESSURE_DUTY_MIN_RUN;
    return WATER_PUMP_PRESSURE_DUTY_MIN_RUN +
           ((span * pressure) + (WATER_PUMP_PRESSURE_INPUT_MAX / 2U)) / WATER_PUMP_PRESSURE_INPUT_MAX;
}

static void WaterPump_ApplyOutput(void)
{
    uint32_t pressure = s_waterPumpPressureSetpoint;
    uint8_t valveOpen = (s_waterPumpEnabled != 0U) ? 1U : 0U;
    uint8_t pwmRun = (valveOpen != 0U && pressure != 0U) ? 1U : 0U;

    if (pressure > WATER_PUMP_PRESSURE_INPUT_MAX) {
        pressure = WATER_PUMP_PRESSURE_INPUT_MAX;
    }

    if (valveOpen != 0U) {
        WaterPumpPin_Write(1U);
    }

    if (WATER_PUMP_PRESSURE_CTRL_MODE == WATER_PUMP_PRESSURE_CTRL_GPIO_LEVEL_TEST) {
        WaterPumpPressurePin_Write(pwmRun);
        DevCtrl_Logf("WaterPump_ApplyOutput test valve=%u pwm=%u level=%s",
                     (unsigned)valveOpen, (unsigned)pwmRun, pwmRun ? "HIGH" : "LOW");
    } else {
        uint32_t dutyPercent = pwmRun ? WaterPump_MapPressureToDutyPercent(pressure) : WATER_PUMP_PRESSURE_DUTY_STOP;
        Pwm_SetDutyPercent(PWM_PUMP, dutyPercent);
        DevCtrl_Logf("WaterPump_ApplyOutput valve=%u pwm=%u pressure=%lu duty=%lu%%",
                     (unsigned)valveOpen, (unsigned)pwmRun, (unsigned long)pressure, (unsigned long)dutyPercent);
    }

    if (valveOpen == 0U) {
        WaterPumpPin_Write(0U);
    }
}

static void WaterPump_CheckAutoOff(void)
{
    TickType_t startTick;
    TickType_t elapsedTicks;
    TickType_t timeoutTicks;

    if (s_waterPumpEnabled == 0U) {
        return;
    }

    if (s_waterPumpAutoOffTimerActive == 0U) {
        s_waterPumpOnTick = xTaskGetTickCount();
        s_waterPumpAutoOffTimerActive = 1U;
        return;
    }

    startTick = s_waterPumpOnTick;
    elapsedTicks = xTaskGetTickCount() - startTick;
    timeoutTicks = pdMS_TO_TICKS(WATER_PUMP_AUTO_OFF_TIMEOUT_MS);

    if (elapsedTicks >= timeoutTicks) {
        DevCtrl_Logf("WaterPump auto off after %lu ms",
                     (unsigned long)WATER_PUMP_AUTO_OFF_TIMEOUT_MS);
        WaterPump_Off();
    }
}

static void PwmSwing_UpdateSmoothParams(void)
{
    uint32_t newSpeed;
    uint32_t newMin;
    uint32_t newMax;
    uint8_t isChanged = 0U;

    taskENTER_CRITICAL();
    newSpeed = StepTowardsU32(s_currentSpeed, s_targetSpeed, PWM_SPEED_STEP_PER_TICK);
    newMin = StepTowardsU32(s_currentMinPwm, s_targetMinPwm, PWM_RANGE_STEP_PER_TICK);
    newMax = StepTowardsU32(s_currentMaxPwm, s_targetMaxPwm, PWM_RANGE_STEP_PER_TICK);
    if (s_targetMinPwm == s_targetMaxPwm) {
        newMin = s_targetMinPwm;
        newMax = s_targetMaxPwm;
    }
    taskEXIT_CRITICAL();

    if (newSpeed != s_currentSpeed) {
        s_currentSpeed = newSpeed;
        isChanged = 1U;
    }
    if (newMin != s_currentMinPwm || newMax != s_currentMaxPwm) {
        s_currentMinPwm = newMin;
        s_currentMaxPwm = newMax;
        isChanged = 1U;
    }

    if (isChanged) {
        s_signalsPerCycle = CalculateSignalsPerCycle(s_currentSpeed);
        CalculateVelocityProfile(s_currentSpeed, s_signalsPerCycle);
    }
}


/****************** (C) COPYRIGHT DJI Innovations *****END OF FILE****/
