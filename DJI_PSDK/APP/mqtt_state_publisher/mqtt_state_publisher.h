#ifndef MQTT_STATE_PUBLISHER_H
#define MQTT_STATE_PUBLISHER_H

#ifdef __cplusplus
extern "C" {
#endif

/** 创建任务，周期性 retain 发布 type=state 到 .../status（1 Hz） */
void MqttStatePublisher_Start(const char *deviceId);

#ifdef __cplusplus
}
#endif

#endif /* MQTT_STATE_PUBLISHER_H */
