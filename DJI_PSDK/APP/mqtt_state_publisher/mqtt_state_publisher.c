#include "mqtt_state_publisher.h"
#include "mqtt_app_config.h"
#include "air780e_mqtt.h"
#include "custom_serial.h"
#include "ds800_protocol.h"
#include "solarclean_ota.h"

#include "cmsis_os.h"
#include "task.h"

#include "uart.h"

#include <stdio.h>
#include <string.h>

#ifndef STATE_TASK_STACK
#define STATE_TASK_STACK 2048
#endif
#define STATE_TASK_PRIO (tskIDLE_PRIORITY + 3)

static char s_topicState[MQTT_TOPIC_SOLARCLEAN_MAX];
static TaskHandle_t s_stateTask;
static uint8_t s_started;

static void state_loop(void *arg)
{
    (void)arg;
    for (;;) {
        char buf[1024];
        T_CustomSerialDroneStatus st;
        int tcp;
        unsigned long ts;

        /* HTTP 占用 modem 时不要 PublishRetain，避免与 AT 会话交错或触发重连 */
        if (SolarCleanOta_IsActive() || Air780eMqtt_IsHttpDownloadActive()) {
            vTaskDelay(pdMS_TO_TICKS(1000 / MQTT_STATE_PUBLISH_HZ));
            continue;
        }

        if (!Ds800Protocol_GetFourGEnabled()) {
            vTaskDelay(pdMS_TO_TICKS(1000 / MQTT_STATE_PUBLISH_HZ));
            continue;
        }

        CustomSerial_GetLastDroneStatus(&st);
        tcp = Air780eMqtt_IsTcpConnected() ? 1 : 0;
        ts = (unsigned long)osKernelGetTickCount();

        (void)snprintf(buf, sizeof(buf),
                       "{\"v\":%d,\"type\":\"state\",\"ts\":%lu,"
                       "\"lat\":%.6f,\"lon\":%.6f,\"alt\":%.2f,\"speed\":%.2f,"
                       "\"yaw\":%.2f,\"pitch\":%.2f,\"roll\":%.2f,"
                       "\"sat\":%u,\"battery\":%.2f,\"water\":%u,"
                       "\"mqtt\":{\"tcp\":%s,\"lastErr\":%d}}",
                       MQTT_PROTO_VERSION, ts,
                       (double)st.latitude_1e6 / 1000000.0,
                       (double)st.longitude_1e6 / 1000000.0,
                       (double)st.height_1e2 / 100.0,
                       (double)st.speed_1e2 / 100.0,
                       (double)st.yaw_1e2 / 100.0,
                       (double)st.pitch_1e2 / 100.0,
                       (double)st.roll_1e2 / 100.0,
                       (unsigned)st.satelliteCount,
                       (double)st.battery_1e2 / 100.0,
                       (unsigned)st.waterLevelStatus,
                       tcp ? "true" : "false",
                       Air780eMqtt_GetLastError());

        (void)Air780eMqtt_PublishRetain(s_topicState, buf, 1);

        vTaskDelay(pdMS_TO_TICKS(1000 / MQTT_STATE_PUBLISH_HZ));
    }
}

void MqttStatePublisher_Start(const char *deviceId)
{
    if (deviceId == NULL || deviceId[0] == '\0' || s_started)
        return;
    (void)snprintf(s_topicState, sizeof(s_topicState), MQTT_TOPIC_FMT_STATE, deviceId);
    s_started = 1;
    if (xTaskCreate(state_loop, "mqtt_state", STATE_TASK_STACK, NULL, STATE_TASK_PRIO, &s_stateTask) != pdPASS) {
        const char *m = "[mqtt_state] xTaskCreate FAILED\r\n";
        s_started = 0;
        UART_Write(UART_NUM_1, (const uint8_t *)m, (uint16_t)strlen(m));
    }
}
