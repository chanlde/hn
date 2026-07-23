#ifndef AIR780E_PPP_H
#define AIR780E_PPP_H

#include "air780e_mqtt.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void Air780ePpp_Run(void);
bool Air780ePpp_IsLinkUp(void);
void Air780ePpp_RequestReconnect(const char *reason);

int Air780ePpp_Publish(const char *topic, const char *payload, int retain);
int Air780ePpp_Subscribe(const char *topic, int qos);
void Air780ePpp_SetMessageCallback(Air780eMqtt_MessageCb cb, void *user);

#ifdef __cplusplus
}
#endif

#endif /* AIR780E_PPP_H */
