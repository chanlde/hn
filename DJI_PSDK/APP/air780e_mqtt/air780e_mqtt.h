#ifndef AIR780E_MQTT_H
#define AIR780E_MQTT_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Historical MQTT facade used by the App protocol modules.
 *
 * In the PPP firmware this API does not send Air780E MQTT AT commands. It maps
 * to the MCU-side MQTT client implemented on top of lwIP PPP.
 */

#define AIR780E_MQTT_BROKER_IP_DEFAULT    "129.211.180.25"
#define AIR780E_MQTT_BROKER_PORT_DEFAULT  1883
#define AIR780E_MQTT_CLIENT_ID_DEFAULT    "FC100_1"

#define AIR780E_MQTT_PAYLOAD_MAX          3072
#define AIR780E_MQTT_TOPIC_MAX            96

void Air780eMqtt_SetBroker(const char *broker_ip, uint16_t port);
void Air780eMqtt_SetClientId(const char *client_id);
void Air780eMqtt_SetAuth(const char *username, const char *password);
void Air780eMqtt_SetWill(const char *topic, const char *payload, int qos, int retain);
void Air780eMqtt_EnableLog(int enable);

/* Kept for old startup code; PPP mode performs AT probing in air780e_ppp.c. */
int Air780eMqtt_AtSelfTest(uint32_t retry_count, uint32_t timeout_ms, uint32_t interval_ms);

int Air780eMqtt_Connect(void);
void Air780eMqtt_Disconnect(void);
int Air780eMqtt_Pingreq(void);
bool Air780eMqtt_IsTcpConnected(void);
int Air780eMqtt_GetLastError(void);
const char *Air780eMqtt_GetLastErrorString(void);
int Air780eMqtt_GetLastConnackRc(void);

int Air780eMqtt_Subscribe(const char *topic, int qos_req);
int Air780eMqtt_Publish(const char *topic, const char *payload);
int Air780eMqtt_PublishRetain(const char *topic, const char *payload, int retain);

typedef void (*Air780eMqtt_MessageCb)(const char *topic, uint16_t topic_len,
                                      const uint8_t *payload, uint16_t payload_len,
                                      void *user);
void Air780eMqtt_SetMessageCallback(Air780eMqtt_MessageCb cb, void *user);
void Air780eMqtt_Service(void);
uint32_t Air780eMqtt_GetServiceLockMissCount(void);

void Air780eMqtt_SetHttpDownloadActive(uint8_t active);
uint8_t Air780eMqtt_IsHttpDownloadActive(void);
void Air780eMqtt_ModemSessionLock(void);
void Air780eMqtt_ModemSessionUnlock(void);

#ifdef __cplusplus
}
#endif

#endif /* AIR780E_MQTT_H */
