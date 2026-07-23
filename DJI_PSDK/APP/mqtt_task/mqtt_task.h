#ifndef MQTT_TASK_H
#define MQTT_TASK_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void MqttTask_Start(void);
void MqttTask_SetTopic(const char *topic);
int MqttTask_EnqueueHex(const uint8_t *data, uint16_t len);
int MqttTask_EnqueueText(const char *topic, const char *payload);

#ifdef __cplusplus
}
#endif

#endif /* MQTT_TASK_H */
