#include "ds800_protocol.h"

#include "uart.h"
#include "pwm_swing.h"
#include "flash_if.h"
#include "solarclean_ota_state.h"

#include "stm32h7xx_hal.h"
#include "FreeRTOS.h"
#include "task.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#define DS800_UART                         UART_NUM_6
#define DS800_SBUS_FRAME_LEN               25U
#define DS800_SBUS_HEADER                  0x0FU

#define DS800_CONTROL_MIN_VALUE            192U
#define DS800_CONTROL_MAX_VALUE            1792U
#define DS800_SBUS_ZERO_FRAME_THRESHOLD    32U
#define DS800_SBUS_HIGH_THRESHOLD          1350U

#define DS800_CH_STICK_LR                  0U  /* CH1: joystick left/right, fixed angle */
#define DS800_CH_STICK_UD                  1U  /* CH2: joystick up/down, pump pressure */
#define DS800_CH_VRA                       2U  /* CH3: swing amplitude */
#define DS800_CH_VRB                       3U  /* CH4: swing speed */
#define DS800_CH_A                         4U  /* CH5: clean/pump switch */
#define DS800_CH_B                         5U  /* CH6: swing switch */
#define DS800_CH_C                         6U  /* CH7: fixed-angle switch */
#define DS800_CH_D                         7U  /* CH8: endpoint trim mode */

#define DS800_CHANNEL_TRACE_DELTA          40U
#define DS800_FIXED_ANGLE_UPDATE_DELTA_X10 10
#define DS800_LOG_PERIOD_MS                1000U
#define DS800_PRESSURE_STEP_PERCENT        5U
#define DS800_FIXED_ANGLE_STEP_X10         10
#define DS800_ENDPOINT_TRIM_STEP_X10       10
#define DS800_ENDPOINT_TRIM_RAW_DEADBAND   4U
#define DS800_ENABLE_ENDPOINT_TRIM_MODE    0U
#define DS800_STICK_LOW_THRESHOLD          750U
#define DS800_STICK_HIGH_THRESHOLD         1250U
#define DS800_STICK_SAVE_DELAY_MS          1000U
#define DS800_STICK_REPEAT_MS              1000U
#define DS800_PSDK_WIDGET_ONLINE_TIMEOUT_MS 5000U
#define DS800_LOOPBACK_TX_PERIOD_MS        500U
#define DS800_DEFAULT_PRESSURE_PERCENT     100U
#define DS800_FIXED_CENTER_ANGLE_X10       900
#define DS800_PWM_SWITCH_GPIO_PORT         GPIOC
#define DS800_PWM_SWITCH_GPIO_PIN          GPIO_PIN_7
#define DS800_PWM_SWITCH_ON_US             1500U
#define DS800_PWM_SWITCH_OFF_US            1300U
#define DS800_PWM_SWITCH_TIMEOUT_MS        300U
#define DS800_PWM_VALID_MIN_US             800U
#define DS800_PWM_VALID_MAX_US             2200U
#define DS800_PARAM_STORE_ADDRESS          (APPLICATION_PARAM_STORE_ADDRESS + DS800_PARAM_STORE_OFFSET)
#define DS800_PARAM_MAGIC                  0x44383030UL
#define DS800_PARAM_VERSION                5UL

typedef enum {
    DS800_DIR_NEUTRAL = 0,
    DS800_DIR_LOW,
    DS800_DIR_HIGH,
} E_Ds800Direction;

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t pressurePercent;
    int32_t fixedAngleX10;
    uint32_t crc32;
} T_Ds800ParamStoreV1;

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t pressurePercent;
    int32_t fixedAngleX10;
    int32_t leftEndpointTrimX10;
    int32_t rightEndpointTrimX10;
    uint32_t crc32;
} T_Ds800ParamStoreV2;

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t pressurePercent;
    int32_t fixedAngleX10;
    int32_t leftEndpointTrimX10;
    int32_t rightEndpointTrimX10;
    uint32_t failsafeHoldEnabled;
    uint32_t crc32;
} T_Ds800ParamStoreV3;

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t pressurePercent;
    int32_t fixedAngleX10;
    int32_t leftEndpointTrimX10;
    int32_t rightEndpointTrimX10;
    uint32_t failsafeHoldEnabled;
    uint32_t remoteControlEnabled;
    uint32_t fourGEnabled;
    uint32_t psdkEnabled;
    uint32_t crc32;
} T_Ds800ParamStoreV4;

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t pressurePercent;
    int32_t fixedAngleX10;
    int32_t leftEndpointTrimX10;
    int32_t rightEndpointTrimX10;
    uint32_t failsafeHoldEnabled;
    uint32_t remoteControlEnabled;
    uint32_t fourGEnabled;
    uint32_t psdkEnabled;
    uint32_t swingAmplitudePercent;
    uint32_t swingSpeedPercent;
    uint32_t crc32;
} T_Ds800ParamStore;

static uint8_t s_frame[DS800_SBUS_FRAME_LEN];
static uint32_t s_framePos;
static uint16_t s_channels[16];
#if DS800_ENABLE_CHANNEL_TRACE
static uint16_t s_lastTraceChannels[8];
#endif
static uint8_t s_aActive;
static uint8_t s_bActive;
static uint8_t s_cActive;
static uint8_t s_dActive;
static uint8_t s_switchBaselineReady;
#if DS800_ENABLE_CHANNEL_TRACE
static uint8_t s_channelTraceReady;
#endif
static uint8_t s_cleanActive;
static uint8_t s_fixedSprayActive;
static uint8_t s_endpointTrimMode;
static uint8_t s_fixedMode;
static uint8_t s_pressureIgnoreMode;
static E_Ds800Direction s_pressureStickDir;
static E_Ds800Direction s_fixedStickDir;
static uint8_t s_signalLost;
static uint8_t s_failsafe;
static uint8_t s_failsafeHoldEnabled;
static uint8_t s_remoteControlEnabled = 1U;
static uint8_t s_fourGEnabled = 1U;
static uint8_t s_psdkEnabled = 1U;
static uint8_t s_paramsLoaded;
static uint32_t s_pumpPressurePercent;
static uint32_t s_swingSpeedPercent;
static uint32_t s_swingAmplitudePercent = 100U;
static int32_t s_fixedAngleDegX10 = DS800_FIXED_CENTER_ANGLE_X10;
static uint8_t s_paramDirty;
static TickType_t s_lastParamChangeTick;
static TickType_t s_pressureStickLastStepTick;
static TickType_t s_fixedStickLastStepTick;
static TickType_t s_lastLogTick;
static uint32_t s_goodFrameCount;
static uint32_t s_badFrameCount;
static int32_t s_leftEndpointTrimX10 = (int32_t)(PWM_SWING_LEFT_ENDPOINT_TRIM_DEG * 10.0f);
static int32_t s_rightEndpointTrimX10 = (int32_t)(PWM_SWING_RIGHT_ENDPOINT_TRIM_DEG * 10.0f);
static uint16_t s_leftEndpointTrimLastRaw;
static uint16_t s_rightEndpointTrimLastRaw;
#if DS800_ENABLE_UART2_LOOPBACK_TEST
static TickType_t s_loopbackLastTxTick;
static uint32_t s_loopbackTxCount;
static uint32_t s_loopbackRxCount;
#endif
#if DS800_ENABLE_PWM_SWITCH_TEST
static volatile uint32_t s_pwmRiseCycle;
static volatile uint32_t s_pwmPulseUs;
static volatile uint32_t s_pwmBadPulseUs;
static volatile uint32_t s_pwmLastPulseMs;
static volatile uint32_t s_pwmValidPulseCount;
static volatile uint32_t s_pwmBadPulseCount;
static uint8_t s_pwmSwitchOn;
static uint32_t s_pwmLastLogMs;
#endif

static uint32_t clamp_percent_i32(int32_t value)
{
    if (value < 0) {
        return 0U;
    }
    if (value > 100) {
        return 100U;
    }
    return (uint32_t)value;
}

static uint8_t channels_look_zero_failsafe(void)
{
    uint32_t i;

    for (i = 0U; i < 8U; i++) {
        if (s_channels[i] > DS800_SBUS_ZERO_FRAME_THRESHOLD) {
            return 0U;
        }
    }

    return 1U;
}

static uint32_t channel_to_control_percent(uint16_t value)
{
    int32_t span = (int32_t)DS800_CONTROL_MAX_VALUE - (int32_t)DS800_CONTROL_MIN_VALUE;
    int32_t scaled;

    if (value <= DS800_CONTROL_MIN_VALUE) {
        return 0U;
    }
    if (value >= DS800_CONTROL_MAX_VALUE) {
        return 100U;
    }

    scaled = (((int32_t)value - (int32_t)DS800_CONTROL_MIN_VALUE) * 100 + (span / 2)) / span;
    return clamp_percent_i32(scaled);
}

static uint16_t clamp_control_raw(uint16_t value)
{
    if (value <= DS800_CONTROL_MIN_VALUE) {
        return DS800_CONTROL_MIN_VALUE;
    }
    if (value >= DS800_CONTROL_MAX_VALUE) {
        return DS800_CONTROL_MAX_VALUE;
    }
    return value;
}

#if DS800_ENABLE_CHANNEL_TRACE
static uint32_t abs_diff_u32(uint32_t a, uint32_t b)
{
    return (a > b) ? (a - b) : (b - a);
}
#endif

static uint32_t crc32_update(uint32_t crc, uint8_t data)
{
    uint32_t i;

    crc ^= data;
    for (i = 0U; i < 8U; i++) {
        if ((crc & 1U) != 0U) {
            crc = (crc >> 1) ^ 0xEDB88320UL;
        } else {
            crc >>= 1;
        }
    }
    return crc;
}

static uint32_t calc_crc32(const void *data, uint32_t len)
{
    const uint8_t *p = (const uint8_t *)data;
    uint32_t crc = 0xFFFFFFFFUL;
    uint32_t i;

    if (data == NULL) {
        return 0U;
    }

    for (i = 0U; i < len; i++) {
        crc = crc32_update(crc, p[i]);
    }
    return crc ^ 0xFFFFFFFFUL;
}

static int32_t abs_diff_i32(int32_t a, int32_t b)
{
    return (a > b) ? (a - b) : (b - a);
}

static int32_t get_endpoint_left_limit_x10(void);
static int32_t get_endpoint_right_limit_x10(void);

static void log_line(const char *msg)
{
    if (msg == NULL) {
        return;
    }
    (void)UART_Write(UART_NUM_1, (const uint8_t *)msg, (uint16_t)strlen(msg));
    (void)UART_Write(UART_NUM_1, (const uint8_t *)"\r\n", 2U);
}

static void log_state(const char *action)
{
    char msg[192];
    int32_t currentAngleX10 = s_fixedAngleDegX10 - DS800_FIXED_CENTER_ANGLE_X10;
    uint32_t currentAngleAbsX10 = (currentAngleX10 < 0) ? (uint32_t)(-currentAngleX10) : (uint32_t)currentAngleX10;
    char currentAngleSign = (currentAngleX10 < 0) ? '-' : '+';
    int n = snprintf(msg, sizeof(msg),
                     "[DS800] %s pump=%u pressure=%lu swing=%u clean=%u spray=%u fixed=%u currentAngle=%c%lu.%lu speed=%lu amp=%lu",
                     action,
                     (unsigned)WaterPump_GetSwitchState(),
                     (unsigned long)s_pumpPressurePercent,
                     (unsigned)PwmSwing_IsRunning(),
                     (unsigned)s_cleanActive,
                     (unsigned)s_fixedSprayActive,
                     (unsigned)s_fixedMode,
                     currentAngleSign,
                     (unsigned long)(currentAngleAbsX10 / 10U),
                     (unsigned long)(currentAngleAbsX10 % 10U),
                     (unsigned long)s_swingSpeedPercent,
                     (unsigned long)s_swingAmplitudePercent);
    if (n > 0 && n < (int)sizeof(msg)) {
        log_line(msg);
    }
}

static void log_fixed_current_angle(void)
{
    int32_t currentAngleX10 = s_fixedAngleDegX10 - DS800_FIXED_CENTER_ANGLE_X10;
    int32_t leftLimitX10 = get_endpoint_left_limit_x10() - DS800_FIXED_CENTER_ANGLE_X10;
    int32_t rightLimitX10 = get_endpoint_right_limit_x10() - DS800_FIXED_CENTER_ANGLE_X10;
    uint32_t currentAngleAbsX10 = (currentAngleX10 < 0) ? (uint32_t)(-currentAngleX10) : (uint32_t)currentAngleX10;
    uint32_t leftAbsX10 = (leftLimitX10 < 0) ? (uint32_t)(-leftLimitX10) : (uint32_t)leftLimitX10;
    uint32_t rightAbsX10 = (rightLimitX10 < 0) ? (uint32_t)(-rightLimitX10) : (uint32_t)rightLimitX10;
    char currentAngleSign = (currentAngleX10 < 0) ? '-' : '+';
    char leftSign = (leftLimitX10 < 0) ? '-' : '+';
    char rightSign = (rightLimitX10 < 0) ? '-' : '+';
    char msg[96];
    int n = snprintf(msg, sizeof(msg),
                     "[DS800] currentAngle=%c%lu.%lu limit=%c%lu.%lu/%c%lu.%lu",
                     currentAngleSign,
                     (unsigned long)(currentAngleAbsX10 / 10U),
                     (unsigned long)(currentAngleAbsX10 % 10U),
                     leftSign,
                     (unsigned long)(leftAbsX10 / 10U),
                     (unsigned long)(leftAbsX10 % 10U),
                     rightSign,
                     (unsigned long)(rightAbsX10 / 10U),
                     (unsigned long)(rightAbsX10 % 10U));
    if (n > 0 && n < (int)sizeof(msg)) {
        log_line(msg);
    }
}

static void log_endpoint_trim(void)
{
    char msg[128];
    int n = snprintf(msg, sizeof(msg),
                     "[DS800] endpoint trim left=%ld.%ld right=%ld.%ld",
                     (long)(s_leftEndpointTrimX10 / 10),
                     (long)((s_leftEndpointTrimX10 < 0) ? (-s_leftEndpointTrimX10 % 10) : (s_leftEndpointTrimX10 % 10)),
                     (long)(s_rightEndpointTrimX10 / 10),
                     (long)((s_rightEndpointTrimX10 < 0) ? (-s_rightEndpointTrimX10 % 10) : (s_rightEndpointTrimX10 % 10)));
    if (n > 0 && n < (int)sizeof(msg)) {
        log_line(msg);
    }
}

static void log_flow(const char *msg)
{
#if DS800_ENABLE_FLOW_LOG
    log_line(msg);
#else
    (void)msg;
#endif
}

static void log_flow_value(const char *fmt, uint32_t value)
{
#if DS800_ENABLE_FLOW_LOG
    char msg[128];
    int n = snprintf(msg, sizeof(msg), fmt, (unsigned long)value);

    if (n > 0 && n < (int)sizeof(msg)) {
        log_line(msg);
    }
#else
    (void)fmt;
    (void)value;
#endif
}

static void log_param_set(const char *name, uint8_t oldValue, uint8_t newValue,
                          uint8_t saveNow, int saveResult)
{
    char msg[128];
    int n;

    if (name == NULL) {
        name = "?";
    }
    n = snprintf(msg, sizeof(msg),
                 "[PARAM] %s old=%u new=%u save=%u result=%d",
                 name,
                 (unsigned)oldValue,
                 (unsigned)newValue,
                 (unsigned)saveNow,
                 saveResult);
    if (n > 0 && n < (int)sizeof(msg)) {
        log_line(msg);
    }
}

#if DS800_ENABLE_CHANNEL_TRACE
static void log_channel_trace(const char *msg)
{
    log_line(msg);
}
#endif

static uint8_t channel_pressed(uint16_t value)
{
    return (value >= DS800_SBUS_HIGH_THRESHOLD) ? 1U : 0U;
}

static E_Ds800Direction channel_direction(uint16_t value)
{
    if (value <= DS800_STICK_LOW_THRESHOLD) {
        return DS800_DIR_LOW;
    }
    if (value >= DS800_STICK_HIGH_THRESHOLD) {
        return DS800_DIR_HIGH;
    }
    return DS800_DIR_NEUTRAL;
}

static int32_t get_endpoint_left_limit_x10(void)
{
    return DS800_FIXED_CENTER_ANGLE_X10 -
           ((int32_t)PWM_SWING_AMPLITUDE_DEG_MAX * 10) -
           s_leftEndpointTrimX10;
}

static int32_t get_endpoint_right_limit_x10(void)
{
    return DS800_FIXED_CENTER_ANGLE_X10 +
           ((int32_t)PWM_SWING_AMPLITUDE_DEG_MAX * 10) +
           s_rightEndpointTrimX10;
}

static int32_t clamp_fixed_angle_x10(int32_t angleX10)
{
    int32_t minX10 = get_endpoint_left_limit_x10();
    int32_t maxX10 = get_endpoint_right_limit_x10();

    if (angleX10 < minX10) {
        return minX10;
    }
    if (angleX10 > maxX10) {
        return maxX10;
    }
    return angleX10;
}

static int32_t clamp_endpoint_trim_x10(int32_t trimX10)
{
    int32_t minTrimX10 = -((int32_t)PWM_SWING_AMPLITUDE_DEG_MAX * 10);
    int32_t maxTrimX10 = ((int32_t)PWM_SWING_CALIBRATION_DEG_MAX -
                          (int32_t)PWM_SWING_AMPLITUDE_DEG_MAX) * 10;

    if (trimX10 < minTrimX10) {
        return minTrimX10;
    }
    if (trimX10 > maxTrimX10) {
        return maxTrimX10;
    }
    return trimX10;
}

static void apply_endpoint_trim(void)
{
    s_leftEndpointTrimX10 = clamp_endpoint_trim_x10(s_leftEndpointTrimX10);
    s_rightEndpointTrimX10 = clamp_endpoint_trim_x10(s_rightEndpointTrimX10);
    PwmSwing_SetEndpointTrimDeg((float)s_leftEndpointTrimX10 / 10.0f,
                                (float)s_rightEndpointTrimX10 / 10.0f);
    s_fixedAngleDegX10 = clamp_fixed_angle_x10(s_fixedAngleDegX10);
}

static void apply_swing_motion_config(void)
{
    s_swingAmplitudePercent = clamp_percent_i32((int32_t)s_swingAmplitudePercent);
    s_swingSpeedPercent = clamp_percent_i32((int32_t)s_swingSpeedPercent);
    PwmSwing_SetAmplitudeDeg(((float)s_swingAmplitudePercent * PWM_SWING_AMPLITUDE_DEG_MAX_F) / 100.0f);
    PwmSwing_SetSpeedFromUI(s_swingSpeedPercent);
}

#if DS800_ENABLE_CHANNEL_TRACE
static uint8_t trace_raw_changed(uint8_t index)
{
    uint32_t oldValue = s_lastTraceChannels[index];
    uint32_t newValue = s_channels[index];
    return (abs_diff_u32(oldValue, newValue) >= DS800_CHANNEL_TRACE_DELTA) ? 1U : 0U;
}
#endif

static void trace_channel_changes(void)
{
#if DS800_ENABLE_CHANNEL_TRACE
    char msg[160];
    E_Ds800Direction dir;
    int n;
    uint32_t i;

    if (!s_channelTraceReady) {
        for (i = 0U; i < 8U; i++) {
            s_lastTraceChannels[i] = s_channels[i];
        }
        s_channelTraceReady = 1U;
        return;
    }

    if (trace_raw_changed(DS800_CH_STICK_LR)) {
        n = snprintf(msg, sizeof(msg),
                     "[DS800 CH] stickLR(CH1) raw=%u fixed=%u angle_x10=%ld %s",
                     (unsigned)s_channels[DS800_CH_STICK_LR],
                     (unsigned)s_fixedMode,
                     (long)s_fixedAngleDegX10,
                     s_fixedMode ? "control=fixed_angle" : "no_action:not_fixed");
        if (n > 0 && n < (int)sizeof(msg)) {
            log_channel_trace(msg);
        }
        s_lastTraceChannels[DS800_CH_STICK_LR] = s_channels[DS800_CH_STICK_LR];
    }

    if (trace_raw_changed(DS800_CH_STICK_UD)) {
        dir = channel_direction(s_channels[DS800_CH_STICK_UD]);
        n = snprintf(msg, sizeof(msg),
                     "[DS800 CH] stickUD(CH2) raw=%u dir=%u pressure_set=%lu %s",
                     (unsigned)s_channels[DS800_CH_STICK_UD],
                     (unsigned)dir,
                     (unsigned long)s_pumpPressurePercent,
                     s_fixedMode ? "no_action:fixed_mode" : "control=pressure");
        if (n > 0 && n < (int)sizeof(msg)) {
            log_channel_trace(msg);
        }
        s_lastTraceChannels[DS800_CH_STICK_UD] = s_channels[DS800_CH_STICK_UD];
    }

    if (trace_raw_changed(DS800_CH_VRA)) {
        dir = (s_channels[DS800_CH_VRA] > s_lastTraceChannels[DS800_CH_VRA]) ? DS800_DIR_HIGH : DS800_DIR_LOW;
        n = snprintf(msg, sizeof(msg),
                     "[DS800 CH] VRA(CH3) raw=%u dir=%u mapped=%lu amplitude_set=%lu control=swing_amplitude",
                     (unsigned)s_channels[DS800_CH_VRA],
                     (unsigned)dir,
                     (unsigned long)channel_to_control_percent(s_channels[DS800_CH_VRA]),
                     (unsigned long)s_swingAmplitudePercent);
        if (n > 0 && n < (int)sizeof(msg)) {
            log_channel_trace(msg);
        }
        s_lastTraceChannels[DS800_CH_VRA] = s_channels[DS800_CH_VRA];
    }

    if (trace_raw_changed(DS800_CH_VRB)) {
        dir = (s_channels[DS800_CH_VRB] > s_lastTraceChannels[DS800_CH_VRB]) ? DS800_DIR_HIGH : DS800_DIR_LOW;
        n = snprintf(msg, sizeof(msg),
                     "[DS800 CH] VRB(CH4) raw=%u dir=%u mapped=%lu speed_set=%lu control=swing_speed",
                     (unsigned)s_channels[DS800_CH_VRB],
                     (unsigned)dir,
                     (unsigned long)channel_to_control_percent(s_channels[DS800_CH_VRB]),
                     (unsigned long)s_swingSpeedPercent);
        if (n > 0 && n < (int)sizeof(msg)) {
            log_channel_trace(msg);
        }
        s_lastTraceChannels[DS800_CH_VRB] = s_channels[DS800_CH_VRB];
    }

    if (channel_pressed(s_lastTraceChannels[DS800_CH_A]) != channel_pressed(s_channels[DS800_CH_A]) ||
        trace_raw_changed(DS800_CH_A)) {
        n = snprintf(msg, sizeof(msg),
                     "[DS800 CH] A(CH5) raw=%u active=%u control=clean",
                     (unsigned)s_channels[DS800_CH_A],
                     (unsigned)channel_pressed(s_channels[DS800_CH_A]));
        if (n > 0 && n < (int)sizeof(msg)) {
            log_channel_trace(msg);
        }
        s_lastTraceChannels[DS800_CH_A] = s_channels[DS800_CH_A];
    }

    if (channel_pressed(s_lastTraceChannels[DS800_CH_B]) != channel_pressed(s_channels[DS800_CH_B]) ||
        trace_raw_changed(DS800_CH_B)) {
        n = snprintf(msg, sizeof(msg),
                     "[DS800 CH] B(CH6) raw=%u active=%u control=swing",
                     (unsigned)s_channels[DS800_CH_B],
                     (unsigned)channel_pressed(s_channels[DS800_CH_B]));
        if (n > 0 && n < (int)sizeof(msg)) {
            log_channel_trace(msg);
        }
        s_lastTraceChannels[DS800_CH_B] = s_channels[DS800_CH_B];
    }

    if (channel_pressed(s_lastTraceChannels[DS800_CH_C]) != channel_pressed(s_channels[DS800_CH_C]) ||
        trace_raw_changed(DS800_CH_C)) {
        n = snprintf(msg, sizeof(msg),
                     "[DS800 CH] C(CH7) raw=%u active=%u control=fixed_mode",
                     (unsigned)s_channels[DS800_CH_C],
                     (unsigned)channel_pressed(s_channels[DS800_CH_C]));
        if (n > 0 && n < (int)sizeof(msg)) {
            log_channel_trace(msg);
        }
        s_lastTraceChannels[DS800_CH_C] = s_channels[DS800_CH_C];
    }

    if (channel_pressed(s_lastTraceChannels[DS800_CH_D]) != channel_pressed(s_channels[DS800_CH_D]) ||
        trace_raw_changed(DS800_CH_D)) {
        n = snprintf(msg, sizeof(msg),
                     "[DS800 CH] D(CH8) raw=%u active=%u control=endpoint_trim",
                     (unsigned)s_channels[DS800_CH_D],
                     (unsigned)channel_pressed(s_channels[DS800_CH_D]));
        if (n > 0 && n < (int)sizeof(msg)) {
            log_channel_trace(msg);
        }
        s_lastTraceChannels[DS800_CH_D] = s_channels[DS800_CH_D];
    }
#else
    return;
#endif
}

static void mark_param_dirty(void)
{
    s_paramDirty = 1U;
    s_lastParamChangeTick = xTaskGetTickCount();
}

static void set_default_persistent_params(void)
{
    if (s_pumpPressurePercent == 0U) {
        s_pumpPressurePercent = DS800_DEFAULT_PRESSURE_PERCENT;
    }
    s_remoteControlEnabled = 1U;
    s_fourGEnabled = 1U;
    s_psdkEnabled = 1U;
    s_swingAmplitudePercent = 100U;
    s_swingSpeedPercent = 0U;
}

static void reset_remote_control_state(uint8_t stopOutput)
{
    s_framePos = 0U;
    s_aActive = 0U;
    s_bActive = 0U;
    s_cActive = 0U;
    s_dActive = 0U;
    s_switchBaselineReady = 0U;
    s_cleanActive = 0U;
    s_fixedSprayActive = 0U;
    s_endpointTrimMode = 0U;
    s_fixedMode = 0U;
    s_pressureIgnoreMode = 0U;
    s_pressureStickDir = DS800_DIR_NEUTRAL;
    s_fixedStickDir = DS800_DIR_NEUTRAL;
    s_signalLost = 0U;
    s_failsafe = 0U;
    if (stopOutput) {
        WaterPump_Off();
        PwmSwing_Stop();
    }
}

#if DS800_ENABLE_PWM_SWITCH_TEST
static void pwm_switch_gpio_init(void)
{
    GPIO_InitTypeDef gpioInit;

    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    if ((DWT->CTRL & DWT_CTRL_CYCCNTENA_Msk) != 0U && DWT->CYCCNT == 0U) {
        DWT->CYCCNT = 0U;
    }

    __HAL_RCC_GPIOC_CLK_ENABLE();
    gpioInit.Pin = DS800_PWM_SWITCH_GPIO_PIN;
    gpioInit.Mode = GPIO_MODE_IT_RISING_FALLING;
    gpioInit.Pull = GPIO_PULLDOWN;
    gpioInit.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(DS800_PWM_SWITCH_GPIO_PORT, &gpioInit);

    HAL_NVIC_SetPriority(EXTI9_5_IRQn, 6U, 0U);
    HAL_NVIC_EnableIRQ(EXTI9_5_IRQn);
}

static uint32_t pwm_cycles_to_us(uint32_t cycles)
{
    uint32_t cyclesPerUs = SystemCoreClock / 1000000U;

    if (cyclesPerUs == 0U) {
        return 0U;
    }
    return (cycles + (cyclesPerUs / 2U)) / cyclesPerUs;
}

static void pwm_switch_set_all(uint8_t on, uint32_t pulseUs, const char *reason)
{
    char msg[128];
    int n;

    if (on) {
        s_pumpPressurePercent = DS800_DEFAULT_PRESSURE_PERCENT;
        WaterPump_SetPressurePercent(DS800_DEFAULT_PRESSURE_PERCENT);
        WaterPump_On();
        PwmSwing_Start();
    } else {
        WaterPump_Off();
        PwmSwing_Stop();
    }

    n = snprintf(msg, sizeof(msg),
                 "[F08A PWM] %s pulse=%lu %s -> pump=%u swing=%u",
                 on ? "ON" : "OFF",
                 (unsigned long)pulseUs,
                 (reason != NULL) ? reason : "",
                 (unsigned)WaterPump_GetSwitchState(),
                 (unsigned)PwmSwing_IsRunning());
    if (n > 0 && n < (int)sizeof(msg)) {
        log_line(msg);
    }
}

static void pwm_switch_service(void)
{
    uint32_t nowMs = HAL_GetTick();
    uint32_t pulseUs = s_pwmPulseUs;
    uint32_t lastPulseMs = s_pwmLastPulseMs;
    char msg[128];
    int n;

    if ((nowMs - s_pwmLastLogMs) >= 1000U) {
        s_pwmLastLogMs = nowMs;
        n = snprintf(msg, sizeof(msg),
                     "[F08A PWM] pulse=%lu bad_us=%lu valid=%lu bad=%lu state=%u",
                     (unsigned long)pulseUs,
                     (unsigned long)s_pwmBadPulseUs,
                     (unsigned long)s_pwmValidPulseCount,
                     (unsigned long)s_pwmBadPulseCount,
                     (unsigned)s_pwmSwitchOn);
        if (n > 0 && n < (int)sizeof(msg)) {
            log_line(msg);
        }
    }

    if ((nowMs - lastPulseMs) > DS800_PWM_SWITCH_TIMEOUT_MS) {
        if (s_pwmSwitchOn) {
            s_pwmSwitchOn = 0U;
            pwm_switch_set_all(0U, pulseUs, "timeout");
        }
        return;
    }

    if (!s_pwmSwitchOn && pulseUs >= DS800_PWM_SWITCH_ON_US) {
        s_pwmSwitchOn = 1U;
        pwm_switch_set_all(1U, pulseUs, "ch5");
    } else if (s_pwmSwitchOn && pulseUs <= DS800_PWM_SWITCH_OFF_US) {
        s_pwmSwitchOn = 0U;
        pwm_switch_set_all(0U, pulseUs, "ch5");
    }
}

void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
    uint32_t nowCycle;
    uint32_t pulseUs;

    if (GPIO_Pin != DS800_PWM_SWITCH_GPIO_PIN) {
        return;
    }

    nowCycle = DWT->CYCCNT;
    if (HAL_GPIO_ReadPin(DS800_PWM_SWITCH_GPIO_PORT, DS800_PWM_SWITCH_GPIO_PIN) == GPIO_PIN_SET) {
        s_pwmRiseCycle = nowCycle;
        return;
    }

    pulseUs = pwm_cycles_to_us(nowCycle - s_pwmRiseCycle);
    if (pulseUs >= DS800_PWM_VALID_MIN_US && pulseUs <= DS800_PWM_VALID_MAX_US) {
        s_pwmPulseUs = pulseUs;
        s_pwmLastPulseMs = HAL_GetTick();
        s_pwmValidPulseCount++;
    } else {
        s_pwmBadPulseUs = pulseUs;
        s_pwmBadPulseCount++;
    }
}
#endif

#if !DS800_ENABLE_UART2_LOOPBACK_TEST
static void load_persistent_params(void)
{
    const T_Ds800ParamStore *stored = (const T_Ds800ParamStore *)DS800_PARAM_STORE_ADDRESS;
    const T_Ds800ParamStoreV4 *storedV4 = (const T_Ds800ParamStoreV4 *)DS800_PARAM_STORE_ADDRESS;
    const T_Ds800ParamStoreV3 *storedV3 = (const T_Ds800ParamStoreV3 *)DS800_PARAM_STORE_ADDRESS;
    const T_Ds800ParamStoreV2 *storedV2 = (const T_Ds800ParamStoreV2 *)DS800_PARAM_STORE_ADDRESS;
    const T_Ds800ParamStoreV1 *storedV1 = (const T_Ds800ParamStoreV1 *)DS800_PARAM_STORE_ADDRESS;
    T_Ds800ParamStore tmp;
    T_Ds800ParamStoreV4 tmpV4;
    T_Ds800ParamStoreV3 tmpV3;
    T_Ds800ParamStoreV2 tmpV2;
    T_Ds800ParamStoreV1 tmpV1;
    uint32_t crc;

    set_default_persistent_params();
    memcpy(&tmp, stored, sizeof(tmp));
    if (tmp.magic != DS800_PARAM_MAGIC) {
        return;
    }

    if (tmp.version == DS800_PARAM_VERSION) {
        crc = calc_crc32(&tmp, (uint32_t)offsetof(T_Ds800ParamStore, crc32));
        if (crc != tmp.crc32) {
            return;
        }

        s_pumpPressurePercent = clamp_percent_i32((int32_t)tmp.pressurePercent);
        s_fixedAngleDegX10 = clamp_fixed_angle_x10(tmp.fixedAngleX10);
        s_leftEndpointTrimX10 = clamp_endpoint_trim_x10(tmp.leftEndpointTrimX10);
        s_rightEndpointTrimX10 = clamp_endpoint_trim_x10(tmp.rightEndpointTrimX10);
        s_failsafeHoldEnabled = (tmp.failsafeHoldEnabled != 0U) ? 1U : 0U;
        s_remoteControlEnabled = (tmp.remoteControlEnabled != 0U) ? 1U : 0U;
        s_fourGEnabled = (tmp.fourGEnabled != 0U) ? 1U : 0U;
        s_psdkEnabled = (tmp.psdkEnabled != 0U) ? 1U : 0U;
        s_swingAmplitudePercent = clamp_percent_i32((int32_t)tmp.swingAmplitudePercent);
        s_swingSpeedPercent = clamp_percent_i32((int32_t)tmp.swingSpeedPercent);
        apply_endpoint_trim();
        apply_swing_motion_config();
        log_flow("[DS800 FLOW] param load ok");
    } else if (tmp.version == 4UL) {
        memcpy(&tmpV4, storedV4, sizeof(tmpV4));
        crc = calc_crc32(&tmpV4, (uint32_t)offsetof(T_Ds800ParamStoreV4, crc32));
        if (crc != tmpV4.crc32) {
            return;
        }

        s_pumpPressurePercent = clamp_percent_i32((int32_t)tmpV4.pressurePercent);
        s_fixedAngleDegX10 = clamp_fixed_angle_x10(tmpV4.fixedAngleX10);
        s_leftEndpointTrimX10 = clamp_endpoint_trim_x10(tmpV4.leftEndpointTrimX10);
        s_rightEndpointTrimX10 = clamp_endpoint_trim_x10(tmpV4.rightEndpointTrimX10);
        s_failsafeHoldEnabled = (tmpV4.failsafeHoldEnabled != 0U) ? 1U : 0U;
        s_remoteControlEnabled = (tmpV4.remoteControlEnabled != 0U) ? 1U : 0U;
        s_fourGEnabled = (tmpV4.fourGEnabled != 0U) ? 1U : 0U;
        s_psdkEnabled = (tmpV4.psdkEnabled != 0U) ? 1U : 0U;
        apply_endpoint_trim();
        apply_swing_motion_config();
        mark_param_dirty();
        log_flow("[DS800 FLOW] param v4 load ok");
    } else if (tmp.version == 3UL) {
        memcpy(&tmpV3, storedV3, sizeof(tmpV3));
        crc = calc_crc32(&tmpV3, (uint32_t)offsetof(T_Ds800ParamStoreV3, crc32));
        if (crc != tmpV3.crc32) {
            return;
        }

        s_pumpPressurePercent = clamp_percent_i32((int32_t)tmpV3.pressurePercent);
        s_fixedAngleDegX10 = clamp_fixed_angle_x10(tmpV3.fixedAngleX10);
        s_leftEndpointTrimX10 = clamp_endpoint_trim_x10(tmpV3.leftEndpointTrimX10);
        s_rightEndpointTrimX10 = clamp_endpoint_trim_x10(tmpV3.rightEndpointTrimX10);
        s_failsafeHoldEnabled = (tmpV3.failsafeHoldEnabled != 0U) ? 1U : 0U;
        s_remoteControlEnabled = 1U;
        s_fourGEnabled = 1U;
        s_psdkEnabled = 1U;
        apply_endpoint_trim();
        apply_swing_motion_config();
        mark_param_dirty();
        log_flow("[DS800 FLOW] param v3 load ok");
    } else if (tmp.version == 2UL) {
        memcpy(&tmpV2, storedV2, sizeof(tmpV2));
        crc = calc_crc32(&tmpV2, (uint32_t)offsetof(T_Ds800ParamStoreV2, crc32));
        if (crc != tmpV2.crc32) {
            return;
        }

        s_pumpPressurePercent = clamp_percent_i32((int32_t)tmpV2.pressurePercent);
        s_fixedAngleDegX10 = clamp_fixed_angle_x10(tmpV2.fixedAngleX10);
        s_leftEndpointTrimX10 = clamp_endpoint_trim_x10(tmpV2.leftEndpointTrimX10);
        s_rightEndpointTrimX10 = clamp_endpoint_trim_x10(tmpV2.rightEndpointTrimX10);
        s_failsafeHoldEnabled = 0U;
        s_remoteControlEnabled = 1U;
        s_fourGEnabled = 1U;
        s_psdkEnabled = 1U;
        apply_endpoint_trim();
        apply_swing_motion_config();
        mark_param_dirty();
        log_flow("[DS800 FLOW] param v2 load ok");
    } else if (tmp.version == 1UL) {
        memcpy(&tmpV1, storedV1, sizeof(tmpV1));
        crc = calc_crc32(&tmpV1, (uint32_t)offsetof(T_Ds800ParamStoreV1, crc32));
        if (crc != tmpV1.crc32) {
            return;
        }

        s_pumpPressurePercent = clamp_percent_i32((int32_t)tmpV1.pressurePercent);
        s_fixedAngleDegX10 = clamp_fixed_angle_x10(tmpV1.fixedAngleX10);
        s_failsafeHoldEnabled = 0U;
        s_remoteControlEnabled = 1U;
        s_fourGEnabled = 1U;
        s_psdkEnabled = 1U;
        apply_endpoint_trim();
        apply_swing_motion_config();
        mark_param_dirty();
        log_flow("[DS800 FLOW] param v1 load ok");
    }
}

static void ensure_persistent_params_loaded(void)
{
    if (s_paramsLoaded) {
        return;
    }
    s_paramsLoaded = 1U;
    load_persistent_params();
}

static int save_persistent_params(void)
{
    T_SolarCleanOtaInfo otaInfo;
    T_Ds800ParamStore store;
    int hasOtaInfo;
    uint32_t result;

    hasOtaInfo = (SolarCleanOtaState_Load(&otaInfo) == 0) ? 1 : 0;

    memset(&store, 0, sizeof(store));
    store.magic = DS800_PARAM_MAGIC;
    store.version = DS800_PARAM_VERSION;
    store.pressurePercent = s_pumpPressurePercent;
    store.fixedAngleX10 = s_fixedAngleDegX10;
    store.leftEndpointTrimX10 = s_leftEndpointTrimX10;
    store.rightEndpointTrimX10 = s_rightEndpointTrimX10;
    store.failsafeHoldEnabled = s_failsafeHoldEnabled ? 1U : 0U;
    store.remoteControlEnabled = s_remoteControlEnabled ? 1U : 0U;
    store.fourGEnabled = s_fourGEnabled ? 1U : 0U;
    store.psdkEnabled = s_psdkEnabled ? 1U : 0U;
    store.swingAmplitudePercent = PwmSwing_GetAmplitudePercent();
    store.swingSpeedPercent = PwmSwing_GetSpeedPercent();
    store.crc32 = calc_crc32(&store, (uint32_t)offsetof(T_Ds800ParamStore, crc32));

    result = FLASH_If_Erase(APPLICATION_PARAM_STORE_ADDRESS, APPLICATION_PARAM_STORE_ADDRESS_END);
    if (result != FLASHIF_OK) {
        log_flow("[DS800 FLOW] param save erase failed");
        return -1;
    }

    if (hasOtaInfo) {
        result = FLASH_If_Write(APPLICATION_PARAM_STORE_ADDRESS, (const uint8_t *)&otaInfo, (uint32_t)sizeof(otaInfo));
        if (result != FLASHIF_OK) {
            log_flow("[DS800 FLOW] param save ota preserve failed");
            return -1;
        }
    }

    result = FLASH_If_Write(DS800_PARAM_STORE_ADDRESS, (const uint8_t *)&store, (uint32_t)sizeof(store));
    if (result != FLASHIF_OK) {
        log_flow("[DS800 FLOW] param save write failed");
        return -1;
    }

    log_flow("[DS800 FLOW] param save ok");
    return 0;
}

static void service_param_save(void)
{
    TickType_t now;

    if (!s_paramDirty) {
        return;
    }

    now = xTaskGetTickCount();
    if ((now - s_lastParamChangeTick) < (TickType_t)pdMS_TO_TICKS(DS800_STICK_SAVE_DELAY_MS)) {
        return;
    }

    if (save_persistent_params() == 0) {
        s_paramDirty = 0U;
    } else {
        s_lastParamChangeTick = now;
    }
}
#endif

uint8_t Ds800Protocol_GetFailsafeHoldEnabled(void)
{
    ensure_persistent_params_loaded();
    return s_failsafeHoldEnabled ? 1U : 0U;
}

int Ds800Protocol_SetFailsafeHoldEnabled(uint8_t enabled, uint8_t saveNow)
{
    uint8_t normalized = enabled ? 1U : 0U;
    uint8_t oldValue;

    ensure_persistent_params_loaded();
    oldValue = s_failsafeHoldEnabled ? 1U : 0U;
    if (s_failsafeHoldEnabled == normalized) {
        log_param_set("failsafeHold", oldValue, normalized, saveNow, 0);
        return 0;
    }

    s_failsafeHoldEnabled = normalized;
    mark_param_dirty();

    if (saveNow) {
#if !DS800_ENABLE_UART2_LOOPBACK_TEST
        if (save_persistent_params() != 0) {
            log_param_set("failsafeHold", oldValue, normalized, saveNow, -1);
            return -1;
        }
        s_paramDirty = 0U;
#endif
    }

    log_param_set("failsafeHold", oldValue, normalized, saveNow, 0);
    return 0;
}

uint8_t Ds800Protocol_GetRemoteControlEnabled(void)
{
    ensure_persistent_params_loaded();
    return s_remoteControlEnabled ? 1U : 0U;
}

int Ds800Protocol_SetRemoteControlEnabled(uint8_t enabled, uint8_t saveNow)
{
    uint8_t normalized = enabled ? 1U : 0U;
    uint8_t oldValue;

    ensure_persistent_params_loaded();
    oldValue = s_remoteControlEnabled ? 1U : 0U;
    if (s_remoteControlEnabled == normalized) {
        log_param_set("remoteControl", oldValue, normalized, saveNow, 0);
        return 0;
    }

    s_remoteControlEnabled = normalized;
    if (!s_remoteControlEnabled) {
        reset_remote_control_state(1U);
    }
    mark_param_dirty();

    if (saveNow) {
#if !DS800_ENABLE_UART2_LOOPBACK_TEST
        if (save_persistent_params() != 0) {
            log_param_set("remoteControl", oldValue, normalized, saveNow, -1);
            return -1;
        }
        s_paramDirty = 0U;
#endif
    }

    log_param_set("remoteControl", oldValue, normalized, saveNow, 0);
    return 0;
}

uint8_t Ds800Protocol_GetFourGEnabled(void)
{
    ensure_persistent_params_loaded();
    return s_fourGEnabled ? 1U : 0U;
}

int Ds800Protocol_SetFourGEnabled(uint8_t enabled, uint8_t saveNow)
{
    uint8_t normalized = enabled ? 1U : 0U;
    uint8_t oldValue;

    ensure_persistent_params_loaded();
    oldValue = s_fourGEnabled ? 1U : 0U;
    if (s_fourGEnabled == normalized) {
        log_param_set("fourG", oldValue, normalized, saveNow, 0);
        return 0;
    }

    s_fourGEnabled = normalized;
    mark_param_dirty();

    if (saveNow) {
#if !DS800_ENABLE_UART2_LOOPBACK_TEST
        if (save_persistent_params() != 0) {
            log_param_set("fourG", oldValue, normalized, saveNow, -1);
            return -1;
        }
        s_paramDirty = 0U;
#endif
    }

    log_param_set("fourG", oldValue, normalized, saveNow, 0);
    return 0;
}

uint8_t Ds800Protocol_GetPsdkEnabled(void)
{
    ensure_persistent_params_loaded();
    return s_psdkEnabled ? 1U : 0U;
}

int Ds800Protocol_SetPsdkEnabled(uint8_t enabled, uint8_t saveNow)
{
    uint8_t normalized = enabled ? 1U : 0U;
    uint8_t oldValue;

    ensure_persistent_params_loaded();
    oldValue = s_psdkEnabled ? 1U : 0U;
    if (s_psdkEnabled == normalized) {
        log_param_set("psdk", oldValue, normalized, saveNow, 0);
        return 0;
    }

    s_psdkEnabled = normalized;
    mark_param_dirty();

    if (saveNow) {
#if !DS800_ENABLE_UART2_LOOPBACK_TEST
        if (save_persistent_params() != 0) {
            log_param_set("psdk", oldValue, normalized, saveNow, -1);
            return -1;
        }
        s_paramDirty = 0U;
#endif
    }

    log_param_set("psdk", oldValue, normalized, saveNow, 0);
    return 0;
}

int32_t Ds800Protocol_GetServoLeftLimitX10(void)
{
    ensure_persistent_params_loaded();
    return ((int32_t)PWM_SWING_AMPLITUDE_DEG_MAX * 10) + s_leftEndpointTrimX10;
}

int32_t Ds800Protocol_GetServoRightLimitX10(void)
{
    ensure_persistent_params_loaded();
    return ((int32_t)PWM_SWING_AMPLITUDE_DEG_MAX * 10) + s_rightEndpointTrimX10;
}

int Ds800Protocol_SetServoLimitsX10(int32_t leftLimitX10, int32_t rightLimitX10, uint8_t saveNow)
{
    int32_t maxLimitX10 = (int32_t)PWM_SWING_CALIBRATION_DEG_MAX * 10;

    ensure_persistent_params_loaded();
    if (leftLimitX10 < 0 || leftLimitX10 > maxLimitX10 ||
        rightLimitX10 < 0 || rightLimitX10 > maxLimitX10) {
        return -2;
    }

    s_leftEndpointTrimX10 = leftLimitX10 - ((int32_t)PWM_SWING_AMPLITUDE_DEG_MAX * 10);
    s_rightEndpointTrimX10 = rightLimitX10 - ((int32_t)PWM_SWING_AMPLITUDE_DEG_MAX * 10);
    apply_endpoint_trim();

    if (saveNow) {
#if !DS800_ENABLE_UART2_LOOPBACK_TEST
        if (save_persistent_params() != 0) {
            return -1;
        }
#endif
    }
    return 0;
}

uint32_t Ds800Protocol_GetSwingAmplitudePercent(void)
{
    ensure_persistent_params_loaded();
    return PwmSwing_GetAmplitudePercent();
}

uint32_t Ds800Protocol_GetSwingSpeedPercent(void)
{
    ensure_persistent_params_loaded();
    return PwmSwing_GetSpeedPercent();
}

int Ds800Protocol_SetSwingMotionPercent(uint32_t amplitudePercent, uint32_t speedPercent, uint8_t saveNow)
{
    ensure_persistent_params_loaded();
    if (amplitudePercent > 100U || speedPercent > 100U) {
        return -2;
    }

    s_swingAmplitudePercent = amplitudePercent;
    s_swingSpeedPercent = speedPercent;
    apply_swing_motion_config();

    if (saveNow) {
#if !DS800_ENABLE_UART2_LOOPBACK_TEST
        if (save_persistent_params() != 0) {
            return -1;
        }
#endif
    }
    return 0;
}

void Ds800Protocol_SetServoSwingTest(uint8_t enabled)
{
    if (enabled) {
        apply_swing_motion_config();
        PwmSwing_Start();
    } else {
        PwmSwing_Stop();
    }
}

uint8_t Ds800Protocol_GetServoSwingRunning(void)
{
    return PwmSwing_IsRunning() ? 1U : 0U;
}

static void apply_fixed_angle(uint8_t force)
{
    int32_t angleX10 = clamp_fixed_angle_x10(s_fixedAngleDegX10);

    if (!force && abs_diff_i32(angleX10, s_fixedAngleDegX10) < DS800_FIXED_ANGLE_UPDATE_DELTA_X10) {
        return;
    }

    s_fixedAngleDegX10 = angleX10;
    log_flow_value("[DS800 CTRL] stickLR(CH1) fixed_angle_x10=%lu -> PwmSwing_SetAngle()", (uint32_t)angleX10);
    PwmSwing_SetAngle((float)angleX10 / 10.0f);
    log_fixed_current_angle();
}

static void adjust_pressure(E_Ds800Direction dir)
{
    int32_t delta;
    uint32_t next;
    TickType_t now = xTaskGetTickCount();

    if (dir == DS800_DIR_NEUTRAL) {
        s_pressureStickDir = DS800_DIR_NEUTRAL;
        s_pressureStickLastStepTick = 0U;
        return;
    }
    if (s_pressureStickDir == dir &&
        s_pressureStickLastStepTick != 0U &&
        (now - s_pressureStickLastStepTick) < (TickType_t)pdMS_TO_TICKS(DS800_STICK_REPEAT_MS)) {
        return;
    }

    delta = (dir == DS800_DIR_HIGH) ? (int32_t)DS800_PRESSURE_STEP_PERCENT : -(int32_t)DS800_PRESSURE_STEP_PERCENT;
    next = clamp_percent_i32((int32_t)s_pumpPressurePercent + delta);
    if (next != s_pumpPressurePercent) {
        s_pumpPressurePercent = next;
        log_flow_value("[DS800 CTRL] stickUD(CH2) pressure=%lu -> WaterPump_SetPressurePercent()", s_pumpPressurePercent);
        WaterPump_SetPressurePercent(s_pumpPressurePercent);
        mark_param_dirty();
        log_state(delta > 0 ? "stick up pressure+" : "stick down pressure-");
    } else {
        log_state(delta > 0 ? "stick up pressure max" : "stick down pressure min");
    }
    s_pressureStickDir = dir;
    s_pressureStickLastStepTick = now;
}

static void adjust_fixed_angle(E_Ds800Direction dir)
{
    int32_t delta;
    int32_t next;
    TickType_t now = xTaskGetTickCount();

    if (dir == DS800_DIR_NEUTRAL) {
        s_fixedStickDir = DS800_DIR_NEUTRAL;
        s_fixedStickLastStepTick = 0U;
        return;
    }
    if (s_fixedStickDir == dir &&
        s_fixedStickLastStepTick != 0U &&
        (now - s_fixedStickLastStepTick) < (TickType_t)pdMS_TO_TICKS(DS800_STICK_REPEAT_MS)) {
        return;
    }

    delta = (dir == DS800_DIR_HIGH) ? DS800_FIXED_ANGLE_STEP_X10 : -DS800_FIXED_ANGLE_STEP_X10;
    next = clamp_fixed_angle_x10(s_fixedAngleDegX10 + delta);
    if (next != s_fixedAngleDegX10) {
        s_fixedAngleDegX10 = next;
        mark_param_dirty();
        apply_fixed_angle(1U);
    }
    s_fixedStickDir = dir;
    s_fixedStickLastStepTick = now;
}

static int32_t clamp_calibration_angle_x10(int32_t angleX10)
{
    int32_t minX10 = DS800_FIXED_CENTER_ANGLE_X10 - ((int32_t)PWM_SWING_CALIBRATION_DEG_MAX * 10);
    int32_t maxX10 = DS800_FIXED_CENTER_ANGLE_X10 + ((int32_t)PWM_SWING_CALIBRATION_DEG_MAX * 10);

    if (angleX10 < minX10) {
        return minX10;
    }
    if (angleX10 > maxX10) {
        return maxX10;
    }
    return angleX10;
}

static void apply_endpoint_trim_step(uint8_t isRight, E_Ds800Direction dir)
{
    int32_t deltaX10;
    int32_t targetAngleX10;
    int32_t *trim;
    const char *side;
    char msg[160];
    int n;

    if (dir == DS800_DIR_NEUTRAL) {
        return;
    }

    trim = isRight ? &s_rightEndpointTrimX10 : &s_leftEndpointTrimX10;
    side = isRight ? "right" : "left";
    if (isRight) {
        deltaX10 = (dir == DS800_DIR_HIGH) ? DS800_ENDPOINT_TRIM_STEP_X10 : -DS800_ENDPOINT_TRIM_STEP_X10;
    } else {
        deltaX10 = (dir == DS800_DIR_HIGH) ? -DS800_ENDPOINT_TRIM_STEP_X10 : DS800_ENDPOINT_TRIM_STEP_X10;
    }
    *trim = clamp_endpoint_trim_x10(*trim + deltaX10);
    apply_endpoint_trim();
    mark_param_dirty();

    targetAngleX10 = isRight ? get_endpoint_right_limit_x10() : get_endpoint_left_limit_x10();
    s_fixedAngleDegX10 = clamp_calibration_angle_x10(targetAngleX10);
    PwmSwing_SetAngle((float)s_fixedAngleDegX10 / 10.0f);

    n = snprintf(msg, sizeof(msg),
                 "[DS800 CTRL] D %s trim currentAngle=%ld.%ld left=%ld.%ld right=%ld.%ld",
                 side,
                 (long)((s_fixedAngleDegX10 - DS800_FIXED_CENTER_ANGLE_X10) / 10),
                 (long)(((s_fixedAngleDegX10 - DS800_FIXED_CENTER_ANGLE_X10) < 0) ?
                        (-(s_fixedAngleDegX10 - DS800_FIXED_CENTER_ANGLE_X10) % 10) :
                        ((s_fixedAngleDegX10 - DS800_FIXED_CENTER_ANGLE_X10) % 10)),
                 (long)((get_endpoint_left_limit_x10() - DS800_FIXED_CENTER_ANGLE_X10) / 10),
                 (long)(((get_endpoint_left_limit_x10() - DS800_FIXED_CENTER_ANGLE_X10) < 0) ?
                        (-(get_endpoint_left_limit_x10() - DS800_FIXED_CENTER_ANGLE_X10) % 10) :
                        ((get_endpoint_left_limit_x10() - DS800_FIXED_CENTER_ANGLE_X10) % 10)),
                 (long)((get_endpoint_right_limit_x10() - DS800_FIXED_CENTER_ANGLE_X10) / 10),
                 (long)(((get_endpoint_right_limit_x10() - DS800_FIXED_CENTER_ANGLE_X10) < 0) ?
                        (-(get_endpoint_right_limit_x10() - DS800_FIXED_CENTER_ANGLE_X10) % 10) :
                        ((get_endpoint_right_limit_x10() - DS800_FIXED_CENTER_ANGLE_X10) % 10)));
    if (n > 0 && n < (int)sizeof(msg)) {
        log_line(msg);
    }
}

static void adjust_one_endpoint_trim_raw(uint8_t isRight, uint16_t raw, uint16_t *lastRaw)
{
    uint16_t currentRaw = clamp_control_raw(raw);

    if (lastRaw == NULL) {
        return;
    }

    if ((uint32_t)currentRaw > ((uint32_t)*lastRaw + DS800_ENDPOINT_TRIM_RAW_DEADBAND)) {
        apply_endpoint_trim_step(isRight, DS800_DIR_HIGH);
        *lastRaw = currentRaw;
    } else if (((uint32_t)currentRaw + DS800_ENDPOINT_TRIM_RAW_DEADBAND) < (uint32_t)*lastRaw) {
        apply_endpoint_trim_step(isRight, DS800_DIR_LOW);
        *lastRaw = currentRaw;
    }
}

static void adjust_endpoint_trim(uint16_t ch3Raw, uint16_t ch4Raw)
{
    adjust_one_endpoint_trim_raw(0U, ch3Raw, &s_leftEndpointTrimLastRaw);
    adjust_one_endpoint_trim_raw(1U, ch4Raw, &s_rightEndpointTrimLastRaw);
}

static void update_swing_amplitude_from_raw(uint16_t raw)
{
    uint32_t percent = channel_to_control_percent(raw);
    int32_t oldFixedAngleX10;
    float ampDeg;

    if (percent == s_swingAmplitudePercent) {
        return;
    }

    s_swingAmplitudePercent = percent;
    ampDeg = ((float)s_swingAmplitudePercent * PWM_SWING_AMPLITUDE_DEG_MAX_F) / 100.0f;
    oldFixedAngleX10 = s_fixedAngleDegX10;
    s_fixedAngleDegX10 = clamp_fixed_angle_x10(s_fixedAngleDegX10);
    if (s_fixedAngleDegX10 != oldFixedAngleX10) {
        mark_param_dirty();
        if (s_fixedMode) {
            apply_fixed_angle(1U);
        }
    }
    log_flow_value("[DS800 CTRL] VRA(CH3) amplitude=%lu -> PwmSwing_SetAmplitudeDeg()", s_swingAmplitudePercent);
    PwmSwing_SetAmplitudeDeg(ampDeg);
    log_state("VRA mapped amplitude");
}

static void update_swing_speed_from_raw(uint16_t raw)
{
    uint32_t percent = channel_to_control_percent(raw);

    if (percent == s_swingSpeedPercent) {
        return;
    }

    s_swingSpeedPercent = percent;
    log_flow_value("[DS800 CTRL] VRB(CH4) speed=%lu -> PwmSwing_SetSpeedFromUI()", s_swingSpeedPercent);
    PwmSwing_SetSpeedFromUI(s_swingSpeedPercent);
    log_state("VRB mapped speed");
}

static void set_clean_switch(uint8_t on)
{
    if (on) {
        if (s_fixedMode) {
            s_cleanActive = 0U;
            s_fixedSprayActive = 1U;
            log_flow("[DS800 CTRL] A(CH5) fixed_spray_on -> WaterPump_SetPressurePercent(100)");
            WaterPump_SetPressurePercent(DS800_DEFAULT_PRESSURE_PERCENT);
            log_flow("[DS800 CTRL] A(CH5) fixed_spray_on -> WaterPump_On()");
            WaterPump_On();
            log_flow("[DS800 CTRL] A(CH5) fixed_spray_on -> PwmSwing_Stop()");
            PwmSwing_Stop();
            apply_fixed_angle(1U);
            log_state("A fixed spray on");
        } else {
            s_cleanActive = 1U;
            s_fixedSprayActive = 0U;
            if (s_pumpPressurePercent == 0U) {
                s_pumpPressurePercent = DS800_DEFAULT_PRESSURE_PERCENT;
                mark_param_dirty();
            }
            log_flow_value("[DS800 CTRL] A(CH5) clean_on -> WaterPump_SetPressurePercent(%lu)", s_pumpPressurePercent);
            WaterPump_SetPressurePercent(s_pumpPressurePercent);
            log_flow("[DS800 CTRL] A(CH5) clean_on -> WaterPump_On()");
            WaterPump_On();
            log_flow("[DS800 CTRL] A(CH5) clean_on -> PwmSwing_Start()");
            PwmSwing_Start();
            log_state("A clean on");
        }
    } else {
        s_cleanActive = 0U;
        s_fixedSprayActive = 0U;
        log_flow("[DS800 CTRL] A(CH5) off -> WaterPump_Off()");
        WaterPump_Off();
        log_flow("[DS800 CTRL] A(CH5) off -> PwmSwing_Stop()");
        PwmSwing_Stop();
        if (s_fixedMode) {
            apply_fixed_angle(1U);
        }
        log_state("A off");
    }
}

static void set_endpoint_trim_mode(uint8_t on)
{
    if (on) {
        s_endpointTrimMode = 1U;
        s_leftEndpointTrimLastRaw = clamp_control_raw(s_channels[DS800_CH_VRA]);
        s_rightEndpointTrimLastRaw = clamp_control_raw(s_channels[DS800_CH_VRB]);
        PwmSwing_Stop();
        log_line("[DS800] D manual trim on: CH3 left boundary, CH4 right boundary");
        log_endpoint_trim();
    } else {
        s_endpointTrimMode = 0U;
        mark_param_dirty();
#if !DS800_ENABLE_UART2_LOOPBACK_TEST
        (void)save_persistent_params();
        s_paramDirty = 0U;
#endif
        log_line("[DS800] D endpoint trim off/save");
        log_endpoint_trim();
    }
}

static void set_swing_switch(uint8_t on)
{
    if (on) {
        s_fixedMode = 0U;
        log_flow("[DS800 CTRL] B(CH6) swing_on -> PwmSwing_Start()");
        PwmSwing_Start();
        log_state("B swing on");
    } else {
        log_flow("[DS800 CTRL] B(CH6) swing_off -> PwmSwing_Stop()");
        PwmSwing_Stop();
        log_state("B swing off");
    }
}

static void set_fixed_switch(uint8_t on)
{
    if (on) {
        if (s_cleanActive) {
            s_cleanActive = 0U;
        }
        s_fixedMode = 1U;
        log_flow("[DS800 FLOW] fixed_on: PwmSwing_Stop()");
        PwmSwing_Stop();
        if (s_aActive || channel_pressed(s_channels[DS800_CH_A])) {
            s_fixedSprayActive = 1U;
            log_flow("[DS800 FLOW] fixed_on: A active, fixed spray");
            WaterPump_SetPressurePercent(DS800_DEFAULT_PRESSURE_PERCENT);
            WaterPump_On();
        } else {
            s_fixedSprayActive = 0U;
            WaterPump_Off();
        }
        apply_fixed_angle(1U);
        log_state("C fixed on");
    } else {
        s_fixedMode = 0U;
        s_fixedSprayActive = 0U;
        if (s_aActive || channel_pressed(s_channels[DS800_CH_A])) {
            set_clean_switch(1U);
        } else {
            WaterPump_Off();
        }
        log_state("C fixed off");
    }
}

static void update_analog_controls(void)
{
    E_Ds800Direction pressureDir = channel_direction(s_channels[DS800_CH_STICK_UD]);
    E_Ds800Direction fixedDir = channel_direction(s_channels[DS800_CH_STICK_LR]);

#if DS800_ENABLE_ENDPOINT_TRIM_MODE
    if (s_endpointTrimMode) {
        s_pressureIgnoreMode = 1U;
        s_pressureStickDir = DS800_DIR_NEUTRAL;
        s_fixedStickDir = DS800_DIR_NEUTRAL;
        adjust_endpoint_trim(s_channels[DS800_CH_VRA], s_channels[DS800_CH_VRB]);
        return;
    }
#endif

    update_swing_amplitude_from_raw(s_channels[DS800_CH_VRA]);
    update_swing_speed_from_raw(s_channels[DS800_CH_VRB]);

    if (!s_fixedMode) {
        s_pressureIgnoreMode = 0U;
        adjust_pressure(pressureDir);
    } else if (s_pressureIgnoreMode != 1U) {
        log_flow("[DS800 FLOW] pressure ignored: fixed mode active");
        s_pressureIgnoreMode = 1U;
        s_pressureStickDir = DS800_DIR_NEUTRAL;
    }

    if (s_fixedMode) {
        adjust_fixed_angle(fixedDir);
        apply_fixed_angle(0U);
    } else {
        s_fixedStickDir = DS800_DIR_NEUTRAL;
    }

}

static void decode_channels(const uint8_t *d, uint16_t *ch)
{
    ch[0]  = (uint16_t)(((d[1]     | d[2] << 8)                         ) & 0x07FF);
    ch[1]  = (uint16_t)(((d[2]>>3  | d[3] << 5)                         ) & 0x07FF);
    ch[2]  = (uint16_t)(((d[3]>>6  | d[4] << 2 | d[5] << 10)            ) & 0x07FF);
    ch[3]  = (uint16_t)(((d[5]>>1  | d[6] << 7)                         ) & 0x07FF);
    ch[4]  = (uint16_t)(((d[6]>>4  | d[7] << 4)                         ) & 0x07FF);
    ch[5]  = (uint16_t)(((d[7]>>7  | d[8] << 1 | d[9] << 9)             ) & 0x07FF);
    ch[6]  = (uint16_t)(((d[9]>>2  | d[10] << 6)                        ) & 0x07FF);
    ch[7]  = (uint16_t)(((d[10]>>5 | d[11] << 3)                        ) & 0x07FF);
    ch[8]  = (uint16_t)(((d[12]    | d[13] << 8)                        ) & 0x07FF);
    ch[9]  = (uint16_t)(((d[13]>>3 | d[14] << 5)                        ) & 0x07FF);
    ch[10] = (uint16_t)(((d[14]>>6 | d[15] << 2 | d[16] << 10)          ) & 0x07FF);
    ch[11] = (uint16_t)(((d[16]>>1 | d[17] << 7)                        ) & 0x07FF);
    ch[12] = (uint16_t)(((d[17]>>4 | d[18] << 4)                        ) & 0x07FF);
    ch[13] = (uint16_t)(((d[18]>>7 | d[19] << 1 | d[20] << 9)           ) & 0x07FF);
    ch[14] = (uint16_t)(((d[20]>>2 | d[21] << 6)                        ) & 0x07FF);
    ch[15] = (uint16_t)(((d[21]>>5 | d[22] << 3)                        ) & 0x07FF);
}

static void handle_frame(const uint8_t *frame)
{
    uint8_t aActive;
    uint8_t bActive;
    uint8_t cActive;
    uint8_t dActive;
    uint8_t zeroFrame;
    uint8_t linkInactive;
    char msg[128];
    int n;
    static uint8_t s_linkInactiveLogged;

    if (frame[0] != DS800_SBUS_HEADER) {
        s_badFrameCount++;
        return;
    }

    decode_channels(frame, s_channels);
    s_signalLost = (frame[23] & 0x04U) ? 1U : 0U;
    s_failsafe = (frame[23] & 0x08U) ? 1U : 0U;
    s_goodFrameCount++;
    zeroFrame = channels_look_zero_failsafe();
    linkInactive = (s_signalLost || s_failsafe || zeroFrame) ? 1U : 0U;

    if (linkInactive) {
        if (!s_linkInactiveLogged) {
            n = snprintf(msg, sizeof(msg),
                         "[DS800] remote offline: ignore frame, keep current control lost=%u fs=%u zero=%u",
                         (unsigned)s_signalLost,
                         (unsigned)s_failsafe,
                         (unsigned)zeroFrame);
            if (n > 0 && n < (int)sizeof(msg)) {
                log_line(msg);
            }
            s_linkInactiveLogged = 1U;
        }
        return;
    }
    s_linkInactiveLogged = 0U;

    aActive = channel_pressed(s_channels[DS800_CH_A]);
    bActive = channel_pressed(s_channels[DS800_CH_B]);
    cActive = channel_pressed(s_channels[DS800_CH_C]);
    dActive = channel_pressed(s_channels[DS800_CH_D]);

    if (!s_switchBaselineReady) {
        s_aActive = aActive;
        s_bActive = bActive;
        s_cActive = cActive;
        s_dActive = dActive;
        s_switchBaselineReady = 1U;
        n = snprintf(msg, sizeof(msg),
                     "[DS800] remote online: baseline only A=%u B=%u C=%u D=%u",
                     (unsigned)aActive,
                     (unsigned)bActive,
                     (unsigned)cActive,
                     (unsigned)dActive);
        if (n > 0 && n < (int)sizeof(msg)) {
            log_line(msg);
        }
        return;
    }

    trace_channel_changes();

    if (aActive != s_aActive) {
        set_clean_switch(aActive);
    }
    s_aActive = aActive;

    if (cActive != s_cActive) {
        set_fixed_switch(cActive);
        if (!cActive && channel_pressed(s_channels[DS800_CH_B])) {
            set_swing_switch(1U);
        }
    }
    s_cActive = cActive;

    if (cActive && !s_fixedMode) {
        set_fixed_switch(1U);
    } else if (!cActive && aActive && !s_cleanActive) {
        set_clean_switch(1U);
    } else if (!aActive && (s_cleanActive || s_fixedSprayActive)) {
        set_clean_switch(0U);
    }

    if (dActive && !s_dActive) {
#if DS800_ENABLE_ENDPOINT_TRIM_MODE
        set_endpoint_trim_mode(s_endpointTrimMode ? 0U : 1U);
#else
        log_line("[DS800] D endpoint trim disabled");
#endif
    }
    s_dActive = dActive;

    if (bActive != s_bActive) {
#if DS800_ENABLE_ENDPOINT_TRIM_MODE
        if (bActive && (s_fixedMode || s_endpointTrimMode)) {
#else
        if (bActive && s_fixedMode) {
#endif
            log_flow("[DS800 CTRL] B(CH6) ignored: fixed/trim mode owns servo");
            log_state("B swing ignored servo mode");
        } else {
            set_swing_switch(bActive);
        }
    }
    s_bActive = bActive;

    update_analog_controls();
}

static void maybe_log_channels(void)
{
    static uint8_t s_sbusLinkLogged;
    static uint8_t s_lastLoggedLost = 0xFFU;
    static uint8_t s_lastLoggedFailsafe = 0xFFU;
    static uint32_t s_lastLoggedBadFrameCount;
    static uint32_t s_lastLoggedLineErrorCount;
    TickType_t now = xTaskGetTickCount();
    uint32_t rawCount;
    uint32_t lineErrorCount;
    const char *status;
    uint8_t shouldLog = 0U;
    char msg[128];
    int n;

    if (s_lastLogTick != 0U &&
        (now - s_lastLogTick) < (TickType_t)pdMS_TO_TICKS(DS800_LOG_PERIOD_MS)) {
        return;
    }

    rawCount = UART_GetUart6RxByteCount();
    lineErrorCount = UART_GetUart6LineErrorClears();
    status = "status";

    if (s_goodFrameCount == 0U) {
#if DS800_LOG_SBUS_WAIT_STATUS
        shouldLog = 1U;
        status = (rawCount == 0U) ? "no_data" : "sync_wait";
#else
        (void)rawCount;
#endif
    } else if (!s_sbusLinkLogged) {
        shouldLog = 1U;
        status = "linked";
        s_sbusLinkLogged = 1U;
    } else if (lineErrorCount != s_lastLoggedLineErrorCount ||
               s_badFrameCount != s_lastLoggedBadFrameCount ||
               s_signalLost != s_lastLoggedLost ||
               s_failsafe != s_lastLoggedFailsafe) {
        shouldLog = 1U;
        status = "abnormal";
    }

    if (!shouldLog) {
        return;
    }

    s_lastLogTick = now;
    s_lastLoggedLineErrorCount = lineErrorCount;
    s_lastLoggedBadFrameCount = s_badFrameCount;
    s_lastLoggedLost = s_signalLost;
    s_lastLoggedFailsafe = s_failsafe;

    n = snprintf(msg, sizeof(msg),
                 "[DS800 SBUS] %s raw=%lu ok=%lu bad=%lu err=%lu lost=%u fs=%u",
                 status,
                 (unsigned long)rawCount,
                 (unsigned long)s_goodFrameCount,
                 (unsigned long)s_badFrameCount,
                 (unsigned long)lineErrorCount,
                 (unsigned)s_signalLost,
                 (unsigned)s_failsafe);
    if (n > 0 && n < (int)sizeof(msg)) {
        log_line(msg);
    }
}

static void process_byte(uint8_t b)
{
    if (s_framePos == 0U) {
        if (b != DS800_SBUS_HEADER) {
            s_badFrameCount++;
            return;
        }
    }

    s_frame[s_framePos++] = b;

    if (s_framePos >= DS800_SBUS_FRAME_LEN) {
        handle_frame(s_frame);
        s_framePos = 0U;
    }
}

#if DS800_ENABLE_UART2_LOOPBACK_TEST
static void service_uart2_loopback_test(void)
{
    uint8_t rx[64];
    char tx[32];
    char msg[256];
    TickType_t now = xTaskGetTickCount();
    int txLen;
    int rxLen;
    int pos;
    int i;

    if (s_loopbackLastTxTick == 0U ||
        (now - s_loopbackLastTxTick) >= (TickType_t)pdMS_TO_TICKS(DS800_LOOPBACK_TX_PERIOD_MS)) {
        s_loopbackLastTxTick = now;
        s_loopbackTxCount++;
        txLen = snprintf(tx, sizeof(tx), "U2TEST%lu\r\n", (unsigned long)s_loopbackTxCount);
        if (txLen > 0 && txLen < (int)sizeof(tx)) {
            (void)UART_Write(DS800_UART, (const uint8_t *)tx, (uint16_t)txLen);
        }
        pos = snprintf(msg, sizeof(msg),
                       "[UART2 LOOP] tx=%lu short PA2(TX) to PA3(RX)",
                       (unsigned long)s_loopbackTxCount);
        if (pos > 0 && pos < (int)sizeof(msg)) {
            log_line(msg);
        }
    }

    do {
        rxLen = UART_Read(DS800_UART, rx, (uint16_t)sizeof(rx));
        if (rxLen > 0) {
            s_loopbackRxCount += (uint32_t)rxLen;
            pos = snprintf(msg, sizeof(msg), "[UART2 LOOP] rx len=%d total=%lu data=",
                           rxLen, (unsigned long)s_loopbackRxCount);
            for (i = 0; i < rxLen && pos > 0 && pos < ((int)sizeof(msg) - 4); i++) {
                pos += snprintf(&msg[pos], sizeof(msg) - (size_t)pos, "%02X ", (unsigned)rx[i]);
            }
            if (pos > 0 && pos < (int)sizeof(msg)) {
                log_line(msg);
            }
        }
    } while (rxLen == (int)sizeof(rx));
}
#endif

void Ds800Protocol_Init(void)
{
    memset(s_frame, 0, sizeof(s_frame));
    memset(s_channels, 0, sizeof(s_channels));
#if DS800_ENABLE_CHANNEL_TRACE
    memset(s_lastTraceChannels, 0, sizeof(s_lastTraceChannels));
#endif
    s_framePos = 0U;
    s_aActive = 0U;
    s_bActive = 0U;
    s_cActive = 0U;
    s_dActive = 0U;
    s_switchBaselineReady = 0U;
#if DS800_ENABLE_CHANNEL_TRACE
    s_channelTraceReady = 0U;
#endif
    s_cleanActive = 0U;
    s_fixedSprayActive = 0U;
    s_endpointTrimMode = 0U;
    s_fixedMode = 0U;
    s_pressureIgnoreMode = 0U;
    s_signalLost = 0U;
    s_failsafe = 0U;
    s_failsafeHoldEnabled = 0U;
    s_pumpPressurePercent = DS800_DEFAULT_PRESSURE_PERCENT;
    s_swingSpeedPercent = 0U;
    s_swingAmplitudePercent = 100U;
    s_fixedAngleDegX10 = DS800_FIXED_CENTER_ANGLE_X10;
    s_pressureStickDir = DS800_DIR_NEUTRAL;
    s_fixedStickDir = DS800_DIR_NEUTRAL;
    s_paramDirty = 0U;
    s_lastParamChangeTick = 0U;
    s_pressureStickLastStepTick = 0U;
    s_fixedStickLastStepTick = 0U;
    s_lastLogTick = 0U;
    s_goodFrameCount = 0U;
    s_badFrameCount = 0U;
    s_leftEndpointTrimLastRaw = 0U;
    s_rightEndpointTrimLastRaw = 0U;
#if DS800_ENABLE_UART2_LOOPBACK_TEST
    s_loopbackLastTxTick = 0U;
    s_loopbackTxCount = 0U;
    s_loopbackRxCount = 0U;
    log_line("[UART2 LOOP] TEST MODE: USART2 100000 8E2 RX non-inverted");
    log_line("[UART2 LOOP] Short PA2(TX) to PA3(RX). Set DS800_ENABLE_UART2_LOOPBACK_TEST=0 for SBUS.");
#elif DS800_ENABLE_PWM_SWITCH_TEST
    s_pwmRiseCycle = 0U;
    s_pwmPulseUs = 0U;
    s_pwmBadPulseUs = 0U;
    s_pwmLastPulseMs = HAL_GetTick();
    s_pwmValidPulseCount = 0U;
    s_pwmBadPulseCount = 0U;
    s_pwmSwitchOn = 0U;
    s_pwmLastLogMs = 0U;
    pwm_switch_gpio_init();
    log_line("[F08A PWM] CH5 switch ready on PC7/USART6_RX");
#else
    load_persistent_params();
    s_paramsLoaded = 1U;
    log_line("[DS800] SBUS ready on USART6 100000 8E2 RX inverted");
#endif
}

void Ds800Protocol_Service(void)
{
#if DS800_ENABLE_UART2_LOOPBACK_TEST
    service_uart2_loopback_test();
#elif DS800_ENABLE_PWM_SWITCH_TEST
    pwm_switch_service();
#else
    uint8_t buf[64];
    int n;
    int i;

    ensure_persistent_params_loaded();
    if (!s_remoteControlEnabled) {
        service_param_save();
        return;
    }

    do {
        n = UART_Read(DS800_UART, buf, (uint16_t)sizeof(buf));
        if (n > 0) {
            for (i = 0; i < n; i++) {
                process_byte(buf[i]);
            }
        }
    } while (n == (int)sizeof(buf));

    maybe_log_channels();
    service_param_save();
#endif
}
