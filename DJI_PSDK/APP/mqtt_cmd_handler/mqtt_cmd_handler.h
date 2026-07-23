#ifndef MQTT_CMD_HANDLER_H
#define MQTT_CMD_HANDLER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** SolarClean/devices/<deviceSn>/control|status|lifecycle, deviceSn equals MQTT ClientId. */
void MqttCmdHandler_Init(const char *deviceId);

/** Register MQTT callback, subscribe .../control, and publish deviceInfo. */
void MqttCmdHandler_OnTcpConnected(void);

/** MQTT downlink callback registered with Air780eMqtt_SetMessageCallback. */
void MqttCmdHandler_OnPublish(const char *topic, uint16_t topicLen, const uint8_t *payload,
                              uint16_t payloadLen, void *user);

/** Reconnect MQTT after a modem HTTP session and re-subscribe control topic. */
int MqttCmdHandler_AfterHttpSession(void);

/* Compatibility no-op hooks retained for disabled legacy download code paths. */
void MqttCmd_PublishDownloadProgress(uint8_t slot, uint32_t got, uint32_t total, unsigned pct);
void MqttCmd_PublishDownloadDone(uint8_t slot, uint32_t size, int crcOk, int flashOk);
void MqttCmd_PublishDownloadError(uint8_t slot, int code, const char *msg, int retries);

#ifdef __cplusplus
}
#endif

#endif /* MQTT_CMD_HANDLER_H */
