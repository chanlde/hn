/**
 * @file mqtt_cmd_handler.c
 * @brief SolarClean MQTT control handler.
 *
 * The App control protocol is numeric-cmd based. cmdName is accepted only as a
 * debug hint and is never used for dispatch.
 */

#include "mqtt_cmd_handler.h"
#include "mqtt_app_config.h"
#include "air780e_mqtt.h"
#include "mqtt_task.h"

#include "pwm_swing.h"
#include "solarclean_ota.h"

#include "FreeRTOS.h"
#include "task.h"

#include "uart.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CMD_PING               0
#define CMD_GET_DEVICE_INFO    1
#define CMD_SET_PUMP           2
#define CMD_SET_PUMP_PRESSURE  3
#define CMD_SET_SPRAY_ANGLE    4
#define CMD_SET_SWING_SPEED    5
#define CMD_SET_SERVO_SWING    6
#define CMD_START_OTA          20
#define CMD_ROUTE_LIST         30
#define CMD_ROUTE_DELETE       31
#define CMD_ROUTE_DOWNLOAD     32
#define CMD_ROUTE_DL_CANCEL    33
#define CMD_EXECUTE_SLOT       34

static char s_devId[MQTT_CLIENT_ID_BUF_LEN];
static char s_topicControl[MQTT_TOPIC_SOLARCLEAN_MAX];
static char s_topicStatus[MQTT_TOPIC_SOLARCLEAN_MAX];

static void cmdh_logf(const char *fmt, ...)
{
    char buf[160];
    va_list ap;
    int n;

    va_start(ap, fmt);
    n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n <= 0)
        return;
    if (n >= (int)sizeof(buf))
        n = (int)sizeof(buf) - 1;
    UART_Write(UART_NUM_1, (const uint8_t *)buf, (uint16_t)n);
    UART_Write(UART_NUM_1, (const uint8_t *)"\r\n", 2);
}

static unsigned long mqtt_now_ms(void)
{
    return (unsigned long)xTaskGetTickCount() * (unsigned long)portTICK_PERIOD_MS;
}

static void json_sanitize_copy(const char *in, char *out, size_t outsz)
{
    size_t i = 0;

    if (out == NULL || outsz == 0U)
        return;
    if (in == NULL)
        in = "";
    while (*in != '\0' && i + 1U < outsz) {
        unsigned char c = (unsigned char)*in++;
        out[i++] = (c == '"' || c == '\\' || c < 32U) ? ' ' : (char)c;
    }
    out[i] = '\0';
}

static const char *json_after_key(const char *json, const char *key)
{
    char pat[48];
    const char *p;

    if (json == NULL || key == NULL)
        return NULL;
    (void)snprintf(pat, sizeof(pat), "\"%s\"", key);
    p = strstr(json, pat);
    if (p == NULL)
        return NULL;
    p += strlen(pat);
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')
        p++;
    if (*p != ':')
        return NULL;
    p++;
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')
        p++;
    return p;
}

static int json_copy_string_quoted(const char *p, char *out, size_t outsz)
{
    size_t i = 0;

    if (p == NULL || out == NULL || outsz == 0U || *p != '"')
        return -1;
    p++;
    while (*p != '\0' && *p != '"') {
        if (i + 1U >= outsz)
            return -1;
        if (*p == '\\' && p[1] != '\0') {
            out[i++] = p[1];
            p += 2;
        } else {
            out[i++] = *p++;
        }
    }
    if (*p != '"')
        return -1;
    out[i] = '\0';
    return 0;
}

static int json_get_string(const char *json, const char *key, char *out, size_t outsz)
{
    return json_copy_string_quoted(json_after_key(json, key), out, outsz);
}

static int json_get_number(const char *json, const char *key, double *out)
{
    const char *p = json_after_key(json, key);
    char *endp;

    if (p == NULL || out == NULL)
        return -1;
    *out = strtod(p, &endp);
    return (endp == p) ? -1 : 0;
}

static int json_get_u32(const char *json, const char *key, uint32_t *out)
{
    double v;

    if (json_get_number(json, key, &v) != 0 || out == NULL || v < 0.0)
        return -1;
    *out = (uint32_t)(v + 0.5);
    return 0;
}

static int json_get_u32_flexible(const char *json, const char *key, uint32_t *out)
{
    char value[24];
    char *endp;
    unsigned long parsed;

    if (json_get_u32(json, key, out) == 0)
        return 0;
    if (json_get_string(json, key, value, sizeof(value)) != 0 || out == NULL)
        return -1;

    parsed = strtoul(value, &endp, 10);
    if (endp == value || *endp != '\0')
        return -1;
    *out = (uint32_t)parsed;
    return 0;
}

static int json_get_bool(const char *json, const char *key, int *out)
{
    const char *p = json_after_key(json, key);

    if (p == NULL || out == NULL)
        return -1;
    if (strncmp(p, "true", 4) == 0) {
        *out = 1;
        return 0;
    }
    if (strncmp(p, "false", 5) == 0) {
        *out = 0;
        return 0;
    }
    return -1;
}

static const char *cmd_name_from_code(int cmd)
{
    switch (cmd) {
        case CMD_PING:
            return "PING";
        case CMD_GET_DEVICE_INFO:
            return "GET_DEVICE_INFO";
        case CMD_SET_PUMP:
            return "SET_PUMP";
        case CMD_SET_PUMP_PRESSURE:
            return "SET_PUMP_PRESSURE";
        case CMD_SET_SPRAY_ANGLE:
            return "SET_SPRAY_ANGLE";
        case CMD_SET_SWING_SPEED:
            return "SET_SWING_SPEED";
        case CMD_SET_SERVO_SWING:
            return "SET_SERVO_SWING";
        case CMD_START_OTA:
            return "START_OTA";
        case CMD_ROUTE_LIST:
            return "ROUTE_LIST";
        case CMD_ROUTE_DELETE:
            return "ROUTE_DELETE";
        case CMD_ROUTE_DOWNLOAD:
            return "ROUTE_DOWNLOAD";
        case CMD_ROUTE_DL_CANCEL:
            return "ROUTE_DOWNLOAD_CANCEL";
        case CMD_EXECUTE_SLOT:
            return "EXECUTE_SLOT";
        default:
            return "UNKNOWN";
    }
}

static void publish_status(const char *payload)
{
    int rc;

    if (payload == NULL || s_topicStatus[0] == '\0')
        return;
    rc = MqttTask_EnqueueText(s_topicStatus, payload);
    if (rc != 0)
        cmdh_logf("[CMDH] status enqueue failed rc=%d", rc);
}

static void publish_status_cb(const char *payload, void *user)
{
    (void)user;
    publish_status(payload);
}

static void publish_ack(const char *msgId, int ofCmd, int ok, int code, const char *msg)
{
    char buf[512];
    char idsafe[64];
    char msafe[128];

    json_sanitize_copy(msgId, idsafe, sizeof(idsafe));
    json_sanitize_copy(msg, msafe, sizeof(msafe));

    (void)snprintf(buf, sizeof(buf),
                   "{\"v\":%d,\"type\":\"ack\",\"msgId\":\"%s\",\"ofType\":\"%s\","
                   "\"ofCmd\":%d,\"ok\":%s,\"code\":%d,\"msg\":\"%s\",\"ts\":%lu}",
                   MQTT_PROTO_VERSION, idsafe, cmd_name_from_code(ofCmd), ofCmd,
                   ok ? "true" : "false", code, msafe, mqtt_now_ms());
    publish_status(buf);
}

static void publish_device_info(void)
{
    char buf[512];

    (void)snprintf(buf, sizeof(buf),
                   "{\"v\":%d,\"type\":\"deviceInfo\",\"ts\":%lu,"
                   "\"hardware_version\":\"%s\",\"firmware_version\":\"%s\","
                   "\"inner_version\":%lu,"
                   "\"slot\":\"%s\",\"ota_status\":\"%s\",\"last_ota_result\":\"%s\","
                   "\"last_fail_reason\":\"%s\",\"network\":\"%s\"}",
                   MQTT_PROTO_VERSION, mqtt_now_ms(), SolarCleanOta_GetHardwareVersion(),
                   SolarCleanOta_GetFirmwareVersion(), (unsigned long)SolarCleanOta_GetInnerVersion(),
                   SolarCleanOta_GetSlot(), SolarCleanOta_GetStatus(),
                   SolarCleanOta_GetLastResult(), SolarCleanOta_GetLastFailReason(),
                   SolarCleanOta_GetNetwork());
    publish_status(buf);
}

static int handle_set_pump(const char *json, const char *msgId)
{
    int onv;

    cmdh_logf("[MQTT CMD] SET_PUMP msgId=%s", (msgId != NULL && msgId[0] != '\0') ? msgId : "-");
    if (json_get_bool(json, "on", &onv) != 0) {
        cmdh_logf("[MQTT CMD] SET_PUMP reject: missing on");
        publish_ack(msgId, CMD_SET_PUMP, 0, 100, "missing on");
        return -1;
    }
    if (onv) {
        cmdh_logf("[MQTT CMD] SET_PUMP on=1 -> WaterPump_On()");
        WaterPump_On();
    } else {
        cmdh_logf("[MQTT CMD] SET_PUMP on=0 -> WaterPump_Off()");
        WaterPump_Off();
    }
    cmdh_logf("[MQTT CMD] SET_PUMP done state=%u", (unsigned)WaterPump_GetSwitchState());
    publish_ack(msgId, CMD_SET_PUMP, 1, 0, "accepted");
    return 0;
}

static int handle_set_pump_pressure(const char *json, const char *msgId)
{
    uint32_t percent;

    cmdh_logf("[MQTT CMD] SET_PUMP_PRESSURE msgId=%s", (msgId != NULL && msgId[0] != '\0') ? msgId : "-");
    if (json_get_u32(json, "percent", &percent) != 0) {
        cmdh_logf("[MQTT CMD] SET_PUMP_PRESSURE reject: missing percent");
        publish_ack(msgId, CMD_SET_PUMP_PRESSURE, 0, 100, "missing percent");
        return -1;
    }
    if (percent > 100U) {
        cmdh_logf("[MQTT CMD] SET_PUMP_PRESSURE reject: percent=%lu out of range",
                  (unsigned long)percent);
        publish_ack(msgId, CMD_SET_PUMP_PRESSURE, 0, 100, "percent out of range");
        return -1;
    }
    cmdh_logf("[MQTT CMD] SET_PUMP_PRESSURE percent=%lu -> WaterPump_SetPressurePercent()",
              (unsigned long)percent);
    WaterPump_SetPressurePercent(percent);
    cmdh_logf("[MQTT CMD] SET_PUMP_PRESSURE done percent=%lu",
              (unsigned long)WaterPump_GetPressurePercent());
    publish_ack(msgId, CMD_SET_PUMP_PRESSURE, 1, 0, "accepted");
    return 0;
}

static int handle_set_spray_angle(const char *json, const char *msgId)
{
    double deg;

    cmdh_logf("[MQTT CMD] SET_SPRAY_ANGLE msgId=%s", (msgId != NULL && msgId[0] != '\0') ? msgId : "-");
    if (json_get_number(json, "amplitudeDeg", &deg) != 0) {
        cmdh_logf("[MQTT CMD] SET_SPRAY_ANGLE reject: missing amplitudeDeg");
        publish_ack(msgId, CMD_SET_SPRAY_ANGLE, 0, 100, "missing amplitudeDeg");
        return -1;
    }
    if (deg < 0.0 || deg > 40.0) {
        cmdh_logf("[MQTT CMD] SET_SPRAY_ANGLE reject: amplitudeDeg out of range");
        publish_ack(msgId, CMD_SET_SPRAY_ANGLE, 0, 100, "amplitudeDeg out of range");
        return -1;
    }
    if (deg > (double)PWM_SWING_AMPLITUDE_DEG_MAX_F)
        deg = (double)PWM_SWING_AMPLITUDE_DEG_MAX_F;
    {
        uint32_t degX10 = (uint32_t)(deg * 10.0 + 0.5);
        cmdh_logf("[MQTT CMD] SET_SPRAY_ANGLE deg=%lu.%lu -> PwmSwing_SetAmplitudeDeg()",
                  (unsigned long)(degX10 / 10U), (unsigned long)(degX10 % 10U));
    }
    PwmSwing_SetAmplitudeDeg((float)deg);
    publish_ack(msgId, CMD_SET_SPRAY_ANGLE, 1, 0, "accepted");
    return 0;
}

static int handle_set_swing_speed(const char *json, const char *msgId)
{
    uint32_t speed;

    cmdh_logf("[MQTT CMD] SET_SWING_SPEED msgId=%s", (msgId != NULL && msgId[0] != '\0') ? msgId : "-");
    if (json_get_u32(json, "speedPercent", &speed) != 0) {
        cmdh_logf("[MQTT CMD] SET_SWING_SPEED reject: missing speedPercent");
        publish_ack(msgId, CMD_SET_SWING_SPEED, 0, 100, "missing speedPercent");
        return -1;
    }
    if (speed > 100U) {
        cmdh_logf("[MQTT CMD] SET_SWING_SPEED reject: speedPercent=%lu out of range",
                  (unsigned long)speed);
        publish_ack(msgId, CMD_SET_SWING_SPEED, 0, 100, "speedPercent out of range");
        return -1;
    }
    cmdh_logf("[MQTT CMD] SET_SWING_SPEED speed=%lu -> PwmSwing_SetSpeedFromUI()",
              (unsigned long)speed);
    PwmSwing_SetSpeedFromUI(speed);
    cmdh_logf("[MQTT CMD] SET_SWING_SPEED done internal=%lu percent=%lu",
              (unsigned long)PwmSwing_GetSpeed(),
              (unsigned long)PwmSwing_GetSpeedPercent());
    publish_ack(msgId, CMD_SET_SWING_SPEED, 1, 0, "accepted");
    return 0;
}

static int handle_set_servo_swing(const char *json, const char *msgId)
{
    int onv;
    uint32_t speed;
    double amp;

    cmdh_logf("[MQTT CMD] SET_SERVO_SWING msgId=%s", (msgId != NULL && msgId[0] != '\0') ? msgId : "-");
    if (json_get_bool(json, "on", &onv) != 0) {
        cmdh_logf("[MQTT CMD] SET_SERVO_SWING reject: missing on");
        publish_ack(msgId, CMD_SET_SERVO_SWING, 0, 100, "missing on");
        return -1;
    }
    if (json_get_u32(json, "speedPercent", &speed) == 0 && speed <= 100U) {
        cmdh_logf("[MQTT CMD] SET_SERVO_SWING speed=%lu -> PwmSwing_SetSpeedFromUI()",
                  (unsigned long)speed);
        PwmSwing_SetSpeedFromUI(speed);
    }
    if (json_get_number(json, "amplitude", &amp) == 0) {
        if (amp < 0.0 || amp > (double)PWM_SWING_AMPLITUDE_DEG_MAX_F) {
            cmdh_logf("[MQTT CMD] SET_SERVO_SWING reject: amplitude out of range");
            publish_ack(msgId, CMD_SET_SERVO_SWING, 0, 100, "amplitude out of range");
            return -1;
        }
        cmdh_logf("[MQTT CMD] SET_SERVO_SWING amplitude -> PwmSwing_SetAmplitudeDeg()");
        PwmSwing_SetAmplitudeDeg((float)amp);
    }

    if (onv) {
        cmdh_logf("[MQTT CMD] SET_SERVO_SWING on=1 -> PwmSwing_Start()");
        PwmSwing_Start();
    } else {
        cmdh_logf("[MQTT CMD] SET_SERVO_SWING on=0 -> PwmSwing_Stop()");
        PwmSwing_Stop();
    }
    cmdh_logf("[MQTT CMD] SET_SERVO_SWING done running=%u",
              (unsigned)PwmSwing_IsRunning());
    publish_ack(msgId, CMD_SET_SERVO_SWING, 1, 0, "accepted");
    return 0;
}

static int handle_start_ota(const char *json, const char *msgId)
{
    T_SolarCleanOtaStartRequest req;
    T_SolarCleanOtaAck ack;

    memset(&req, 0, sizeof(req));
    memset(&ack, 0, sizeof(ack));
    cmdh_logf("[MQTT OTA] START_OTA msgId=%s", (msgId != NULL && msgId[0] != '\0') ? msgId : "-");

    if (json_get_string(json, "target_version", req.targetVersion, sizeof(req.targetVersion)) != 0)
        (void)json_get_string(json, "version", req.targetVersion, sizeof(req.targetVersion));
    if (json_get_string(json, "download_url", req.downloadUrl, sizeof(req.downloadUrl)) != 0)
        (void)json_get_string(json, "downloadUrl", req.downloadUrl, sizeof(req.downloadUrl));

    if (req.targetVersion[0] == '\0' ||
        (json_get_u32_flexible(json, "target_inner_version", &req.targetInnerVersion) != 0 &&
         json_get_u32_flexible(json, "targetInnerVersion", &req.targetInnerVersion) != 0 &&
         json_get_u32_flexible(json, "innerVersion", &req.targetInnerVersion) != 0) ||
        req.downloadUrl[0] == '\0') {
        publish_ack(msgId, CMD_START_OTA, 0, 100, "missing ota field");
        cmdh_logf("[MQTT OTA] reject missing field target=%s inner=%lu url=%s",
                  req.targetVersion,
                  (unsigned long)req.targetInnerVersion,
                  req.downloadUrl);
        return -1;
    }
    (void)json_get_string(json, "hardware_version", req.hardwareVersion, sizeof(req.hardwareVersion));
    if (json_get_u32_flexible(json, "file_size", &req.fileSize) != 0)
        (void)json_get_u32_flexible(json, "fileSize", &req.fileSize);
    (void)json_get_string(json, "sha256", req.sha256, sizeof(req.sha256));
    cmdh_logf("[MQTT OTA] parsed target=%s inner=%lu size=%lu hw=%s sha=%s url=%s",
              req.targetVersion,
              (unsigned long)req.targetInnerVersion,
              (unsigned long)req.fileSize,
              req.hardwareVersion,
              req.sha256[0] ? "yes" : "no",
              req.downloadUrl);

    SolarCleanOta_HandleStart(&req, &ack, publish_status_cb, NULL);
    publish_ack(msgId, CMD_START_OTA, ack.ok, ack.code, ack.msg);
    cmdh_logf("[MQTT OTA] ack ok=%d code=%d msg=%s", ack.ok, ack.code, ack.msg);
    return ack.ok ? 0 : -1;
}

static int dispatch_cmd(const char *json, int cmd, const char *msgId)
{
    switch (cmd) {
        case CMD_PING:
            publish_ack(msgId, cmd, 1, 0, "pong");
            return 0;
        case CMD_GET_DEVICE_INFO:
            publish_ack(msgId, cmd, 1, 0, "accepted");
            publish_device_info();
            return 0;
        case CMD_SET_PUMP:
            return handle_set_pump(json, msgId);
        case CMD_SET_PUMP_PRESSURE:
            return handle_set_pump_pressure(json, msgId);
        case CMD_SET_SPRAY_ANGLE:
            return handle_set_spray_angle(json, msgId);
        case CMD_SET_SWING_SPEED:
            return handle_set_swing_speed(json, msgId);
        case CMD_SET_SERVO_SWING:
            return handle_set_servo_swing(json, msgId);
        case CMD_START_OTA:
            return handle_start_ota(json, msgId);
        case CMD_ROUTE_LIST:
        case CMD_ROUTE_DELETE:
        case CMD_ROUTE_DOWNLOAD:
        case CMD_ROUTE_DL_CANCEL:
        case CMD_EXECUTE_SLOT:
            publish_ack(msgId, cmd, 0, 200, "route feature disabled");
            return -1;
        default:
            publish_ack(msgId, cmd, 0, 100, "unknown cmd");
            return -1;
    }
}

void MqttCmdHandler_Init(const char *deviceId)
{
    if (deviceId == NULL || deviceId[0] == '\0')
        return;

    strncpy(s_devId, deviceId, sizeof(s_devId) - 1U);
    s_devId[sizeof(s_devId) - 1U] = '\0';

    (void)snprintf(s_topicControl, sizeof(s_topicControl), MQTT_TOPIC_FMT_CONTROL, s_devId);
    (void)snprintf(s_topicStatus, sizeof(s_topicStatus), MQTT_TOPIC_FMT_STATUS, s_devId);
}

void MqttCmdHandler_OnTcpConnected(void)
{
    int rc;

    Air780eMqtt_SetMessageCallback(MqttCmdHandler_OnPublish, NULL);
    rc = Air780eMqtt_Subscribe(s_topicControl, 1);
    if (rc != 0)
        cmdh_logf("[CMDH] SUB FAIL topic=%s rc=%d", s_topicControl, rc);
    publish_device_info();
}

void MqttCmdHandler_OnPublish(const char *topic, uint16_t topicLen, const uint8_t *payload,
                              uint16_t payloadLen, void *user)
{
    char tmpTopic[MQTT_TOPIC_SOLARCLEAN_MAX];
    char msgId[64] = "";
    int cmd;
    double cmdDouble;

    (void)user;
    if (topic == NULL || payload == NULL || payloadLen == 0U)
        return;

    cmdh_logf("[MQTT RX] topicLen=%u payloadLen=%u", (unsigned)topicLen, (unsigned)payloadLen);

    memcpy(tmpTopic, topic, topicLen < sizeof(tmpTopic) - 1U ? topicLen : sizeof(tmpTopic) - 1U);
    tmpTopic[topicLen < sizeof(tmpTopic) - 1U ? topicLen : sizeof(tmpTopic) - 1U] = '\0';
    if (strcmp(tmpTopic, s_topicControl) != 0)
        return;

    {
        char *jsonbuf = (char *)pvPortMalloc((size_t)payloadLen + 1U);
        if (jsonbuf == NULL) {
            cmdh_logf("[CMDH] malloc failed payloadLen=%u", (unsigned)payloadLen);
            return;
        }
        memcpy(jsonbuf, payload, payloadLen);
        jsonbuf[payloadLen] = '\0';

        (void)json_get_string(jsonbuf, "msgId", msgId, sizeof(msgId));
        if (json_get_number(jsonbuf, "cmd", &cmdDouble) != 0) {
            publish_ack(msgId, -1, 0, 100, "missing cmd");
            vPortFree(jsonbuf);
            return;
        }
        cmd = (int)cmdDouble;
        cmdh_logf("[MQTT RX] cmd=%d msgId=%s", cmd, msgId[0] ? msgId : "-");
        (void)dispatch_cmd(jsonbuf, cmd, msgId);
        vPortFree(jsonbuf);
    }
}

int MqttCmdHandler_AfterHttpSession(void)
{
    if (Air780eMqtt_Connect() != 0)
        return -1;
    if (Air780eMqtt_Subscribe(s_topicControl, 1) != 0)
        return -1;
    publish_device_info();
    return 0;
}

void MqttCmd_PublishDownloadProgress(uint8_t slot, uint32_t got, uint32_t total, unsigned pct)
{
    (void)slot;
    (void)got;
    (void)total;
    (void)pct;
}

void MqttCmd_PublishDownloadDone(uint8_t slot, uint32_t size, int crcOk, int flashOk)
{
    (void)slot;
    (void)size;
    (void)crcOk;
    (void)flashOk;
}

void MqttCmd_PublishDownloadError(uint8_t slot, int code, const char *msg, int retries)
{
    (void)slot;
    (void)code;
    (void)msg;
    (void)retries;
}
