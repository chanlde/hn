/**
 * @file custom_serial.c
 * @brief MCU status snapshot and servo wrappers. Route/KMZ support is retired.
 */
#include "custom_serial.h"
#include "dji_logger.h"
#include "dji_platform.h"
#include "dji_fc_subscription.h"
#include "../../BSP/pwm_swing.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

/** 供 MQTT state 读取；由 PSDK FC subscription 周期刷新。 */
static T_CustomSerialDroneStatus s_lastDroneStatusSnap;
static uint8_t s_fcSubInited;
static uint8_t s_subQuaternion;
static uint8_t s_subPositionFused;
static uint8_t s_subVelocity;
static uint8_t s_subHeightFusion;
static uint8_t s_subBatteryInfo;

static uint8_t                 s_storageInited;

static int32_t clamp_i32(double v, int32_t minv, int32_t maxv)
{
    if (v < (double)minv)
        return minv;
    if (v > (double)maxv)
        return maxv;
    return (int32_t)((v >= 0.0) ? (v + 0.5) : (v - 0.5));
}

static uint16_t clamp_u16(double v, uint16_t maxv)
{
    if (v <= 0.0)
        return 0U;
    if (v > (double)maxv)
        return maxv;
    return (uint16_t)(v + 0.5);
}

static int16_t clamp_i16(double v, int16_t minv, int16_t maxv)
{
    if (v < (double)minv)
        return minv;
    if (v > (double)maxv)
        return maxv;
    return (int16_t)((v >= 0.0) ? (v + 0.5) : (v - 0.5));
}

static void quat_to_euler_deg(const T_DjiFcSubscriptionQuaternion *q,
                              double *yawDeg, double *pitchDeg, double *rollDeg)
{
    const double rad2deg = 57.29577951308232;
    double sinp;

    if (q == NULL || yawDeg == NULL || pitchDeg == NULL || rollDeg == NULL)
        return;

    sinp = -2.0 * (double)q->q1 * (double)q->q3 + 2.0 * (double)q->q0 * (double)q->q2;
    if (sinp > 1.0)
        sinp = 1.0;
    else if (sinp < -1.0)
        sinp = -1.0;

    *pitchDeg = asin(sinp) * rad2deg;
    *rollDeg = atan2(2.0 * (double)q->q2 * (double)q->q3 + 2.0 * (double)q->q0 * (double)q->q1,
                     -2.0 * (double)q->q1 * (double)q->q1 - 2.0 * (double)q->q2 * (double)q->q2 + 1.0) * rad2deg;
    *yawDeg = atan2(2.0 * (double)q->q1 * (double)q->q2 + 2.0 * (double)q->q0 * (double)q->q3,
                    -2.0 * (double)q->q2 * (double)q->q2 - 2.0 * (double)q->q3 * (double)q->q3 + 1.0) * rad2deg;
}

static void CustomSerial_StartFcSubscription(void)
{
    T_DjiReturnCode rc;

    if (s_fcSubInited)
        return;

    rc = DjiFcSubscription_Init();
    if (rc != DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
        USER_LOG_WARN("[State] FC subscription init failed 0x%08llX", (unsigned long long)rc);
        return;
    }

    s_fcSubInited = 1U;

    rc = DjiFcSubscription_SubscribeTopic(DJI_FC_SUBSCRIPTION_TOPIC_QUATERNION,
                                          DJI_DATA_SUBSCRIPTION_TOPIC_10_HZ, NULL);
    if (rc == DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
        s_subQuaternion = 1U;
    } else {
        USER_LOG_WARN("[State] subscribe quaternion failed 0x%08llX", (unsigned long long)rc);
    }

    rc = DjiFcSubscription_SubscribeTopic(DJI_FC_SUBSCRIPTION_TOPIC_POSITION_FUSED,
                                          DJI_DATA_SUBSCRIPTION_TOPIC_1_HZ, NULL);
    if (rc == DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
        s_subPositionFused = 1U;
    } else {
        USER_LOG_WARN("[State] subscribe position_fused failed 0x%08llX", (unsigned long long)rc);
    }

    rc = DjiFcSubscription_SubscribeTopic(DJI_FC_SUBSCRIPTION_TOPIC_VELOCITY,
                                          DJI_DATA_SUBSCRIPTION_TOPIC_1_HZ, NULL);
    if (rc == DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
        s_subVelocity = 1U;
    } else {
        USER_LOG_WARN("[State] subscribe velocity failed 0x%08llX", (unsigned long long)rc);
    }

    rc = DjiFcSubscription_SubscribeTopic(DJI_FC_SUBSCRIPTION_TOPIC_HEIGHT_FUSION,
                                          DJI_DATA_SUBSCRIPTION_TOPIC_1_HZ, NULL);
    if (rc == DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
        s_subHeightFusion = 1U;
    } else {
        USER_LOG_WARN("[State] subscribe height_fusion failed 0x%08llX", (unsigned long long)rc);
    }

    rc = DjiFcSubscription_SubscribeTopic(DJI_FC_SUBSCRIPTION_TOPIC_BATTERY_INFO,
                                          DJI_DATA_SUBSCRIPTION_TOPIC_1_HZ, NULL);
    if (rc == DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
        s_subBatteryInfo = 1U;
    } else {
        USER_LOG_WARN("[State] subscribe battery_info failed 0x%08llX", (unsigned long long)rc);
    }

    USER_LOG_INFO("[State] FC subscription ready q=%u pos=%u vel=%u h=%u bat=%u",
                  (unsigned)s_subQuaternion, (unsigned)s_subPositionFused, (unsigned)s_subVelocity,
                  (unsigned)s_subHeightFusion, (unsigned)s_subBatteryInfo);
}

static void CustomSerial_UpdateDroneStatusFromFc(void)
{
    T_DjiDataTimestamp timestamp;
    T_DjiReturnCode rc;

    if (!s_fcSubInited)
        return;

    if (s_subQuaternion) {
        T_DjiFcSubscriptionQuaternion q = {0};
        double yaw = 0.0;
        double pitch = 0.0;
        double roll = 0.0;
        rc = DjiFcSubscription_GetLatestValueOfTopic(DJI_FC_SUBSCRIPTION_TOPIC_QUATERNION,
                                                     (uint8_t *)&q, sizeof(q), &timestamp);
        if (rc == DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
            quat_to_euler_deg(&q, &yaw, &pitch, &roll);
            s_lastDroneStatusSnap.yaw_1e2 = clamp_i16(yaw * 100.0, -18000, 18000);
            s_lastDroneStatusSnap.pitch_1e2 = clamp_i16(pitch * 100.0, -18000, 18000);
            s_lastDroneStatusSnap.roll_1e2 = clamp_i16(roll * 100.0, -18000, 18000);
        }
    }

    if (s_subPositionFused) {
        T_DjiFcSubscriptionPositionFused pos = {0};
        rc = DjiFcSubscription_GetLatestValueOfTopic(DJI_FC_SUBSCRIPTION_TOPIC_POSITION_FUSED,
                                                     (uint8_t *)&pos, sizeof(pos), &timestamp);
        if (rc == DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
            const double rad2deg = 57.29577951308232;
            s_lastDroneStatusSnap.latitude_1e6 = clamp_i32(pos.latitude * rad2deg * 1000000.0,
                                                           -90000000, 90000000);
            s_lastDroneStatusSnap.longitude_1e6 = clamp_i32(pos.longitude * rad2deg * 1000000.0,
                                                            -180000000, 180000000);
            s_lastDroneStatusSnap.satelliteCount = (pos.visibleSatelliteNumber > 255U) ?
                                                   255U : (uint8_t)pos.visibleSatelliteNumber;
            if (!s_subHeightFusion)
                s_lastDroneStatusSnap.height_1e2 = clamp_u16((double)pos.altitude * 100.0, 65535U);
        }
    }

    if (s_subVelocity) {
        T_DjiFcSubscriptionVelocity vel = {0};
        rc = DjiFcSubscription_GetLatestValueOfTopic(DJI_FC_SUBSCRIPTION_TOPIC_VELOCITY,
                                                     (uint8_t *)&vel, sizeof(vel), &timestamp);
        if (rc == DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS && vel.health) {
            double speed = sqrt((double)vel.data.x * (double)vel.data.x +
                                (double)vel.data.y * (double)vel.data.y +
                                (double)vel.data.z * (double)vel.data.z);
            s_lastDroneStatusSnap.speed_1e2 = clamp_u16(speed * 100.0, 65535U);
        }
    }

    if (s_subHeightFusion) {
        T_DjiFcSubscriptionHeightFusion height = 0.0f;
        rc = DjiFcSubscription_GetLatestValueOfTopic(DJI_FC_SUBSCRIPTION_TOPIC_HEIGHT_FUSION,
                                                     (uint8_t *)&height, sizeof(height), &timestamp);
        if (rc == DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS)
            s_lastDroneStatusSnap.height_1e2 = clamp_u16((double)height * 100.0, 65535U);
    }

    if (s_subBatteryInfo) {
        T_DjiFcSubscriptionWholeBatteryInfo bat = {0};
        rc = DjiFcSubscription_GetLatestValueOfTopic(DJI_FC_SUBSCRIPTION_TOPIC_BATTERY_INFO,
                                                     (uint8_t *)&bat, sizeof(bat), &timestamp);
        if (rc == DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS)
            s_lastDroneStatusSnap.battery_1e2 = clamp_u16((double)bat.percentage * 100.0, 10000U);
    }
}

T_DjiReturnCode CustomSerial_StorageInit(void)
{
    if (s_storageInited) {
        return DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS;
    }

    memset(&s_lastDroneStatusSnap, 0, sizeof(s_lastDroneStatusSnap));
    s_storageInited = 1U;
    CustomSerial_StartFcSubscription();
    USER_LOG_INFO("[Route] route storage/mission feature disabled");
    return DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS;
}

T_DjiReturnCode CustomSerial_WriteKmzToSlot(uint8_t slotIdx, const uint8_t *data, uint32_t len)
{
    (void)slotIdx;
    (void)data;
    (void)len;
    USER_LOG_WARN("[Route] KMZ write ignored: route feature disabled");
    return DJI_ERROR_SYSTEM_MODULE_CODE_NONSUPPORT;
}

T_DjiReturnCode CustomSerial_EraseRouteSlotForMqtt(uint8_t slotIdx)
{
    (void)slotIdx;
    USER_LOG_WARN("[Route] erase ignored: route feature disabled");
    return DJI_ERROR_SYSTEM_MODULE_CODE_NONSUPPORT;
}

uint32_t CustomSerial_GetRouteSlotLen(uint8_t slotIdx)
{
    (void)slotIdx;
    return 0U;
}

void CustomSerial_GetRouteSlotsSummary(char *buf, uint32_t bufSize)
{
    if (buf == NULL || bufSize == 0U) {
        return;
    }
    (void)snprintf(buf, bufSize, "Routes: disabled");
}

T_DjiReturnCode RouteSlot_Execute(uint8_t slotIdx)
{
    (void)slotIdx;
    USER_LOG_WARN("[Route] execute ignored: route feature disabled");
    return DJI_ERROR_SYSTEM_MODULE_CODE_NONSUPPORT;
}

T_DjiReturnCode ServoSwing_Start(void)
{
    PwmSwing_Start();
    return DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS;
}

T_DjiReturnCode ServoSwing_Stop(void)
{
    PwmSwing_Stop();
    return DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS;
}

void CustomSerial_GetLastDroneStatus(T_CustomSerialDroneStatus *out)
{
    if (out == NULL) {
        return;
    }
    CustomSerial_UpdateDroneStatusFromFc();
    memcpy(out, &s_lastDroneStatusSnap, sizeof(T_CustomSerialDroneStatus));
}
