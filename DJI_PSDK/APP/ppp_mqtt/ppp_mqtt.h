#ifndef PPP_MQTT_H
#define PPP_MQTT_H

#include "air780e_mqtt.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void PppMqtt_Reset(void);
int PppMqtt_Start(const char *host, uint16_t port,
                  const char *client_id,
                  const char *username, const char *password,
                  const char *will_topic, const char *will_payload,
                  int will_retain);
void PppMqtt_Service(void);
bool PppMqtt_IsConnected(void);
bool PppMqtt_IsFailed(void);
int PppMqtt_GetLastError(void);
int PppMqtt_GetLastConnackRc(void);
const char *PppMqtt_GetLastErrorString(void);

int PppMqtt_Publish(const char *topic, const char *payload, int retain);
int PppMqtt_Subscribe(const char *topic, int qos_req);
int PppMqtt_Pingreq(void);
void PppMqtt_SetMessageCallback(Air780eMqtt_MessageCb cb, void *user);

#ifdef __cplusplus
}
#endif

#endif /* PPP_MQTT_H */
