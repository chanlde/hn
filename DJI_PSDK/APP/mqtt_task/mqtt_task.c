#include "mqtt_task.h"

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"
#include "air780e_mqtt.h"
#include "mqtt_app_config.h"
#include "ds800_protocol.h"
#include "uart.h"

#include <string.h>
#include <stdio.h>

/* MQTT 任务 hex 缓冲：整帧 hex 最长 2048 字符 + '\\0'（与 1024 字节原始帧上限对齐） */
#ifndef MQTT_HEX_RAW_MAX
#define MQTT_HEX_RAW_MAX     1024
#endif
/* T_MqttMsg + Air780eMqtt_Publish 调用链需留足余量；单位为 FreeRTOS words。 */
#ifndef MQTT_TASK_STACK_SIZE
#define MQTT_TASK_STACK_SIZE 2048
#endif
/* 高于 defaultTask(prio=3)，队列有待发消息时尽快取队、调用 Publish，减轻 xQueueSend 丢包 */
#define MQTT_TASK_PRIORITY   (tskIDLE_PRIORITY + 5)
/* 控制堆占用：每个 T_MqttMsg 约 2KB。 */
#ifndef MQTT_QUEUE_DEPTH
#define MQTT_QUEUE_DEPTH     4
#endif
#define MQTT_TOPIC_MAX_LEN   64
#define MQTT_PAYLOAD_MAX_LEN (MQTT_HEX_RAW_MAX * 2)

typedef struct {
    char topic[MQTT_TOPIC_MAX_LEN + 1];
    char payload[MQTT_PAYLOAD_MAX_LEN + 1];
} T_MqttMsg;

static QueueHandle_t s_mqttQueue = NULL;
static TaskHandle_t s_mqttTask = NULL;
static SemaphoreHandle_t s_enqueueMutex = NULL;
static char s_topic[MQTT_TOPIC_MAX_LEN + 1] = "M400/4Gtest";
/* 入队 scratch 受 s_enqueueMutex 保护，避免多生产者同时写静态缓冲。 */
static T_MqttMsg s_enqueueScratch;
static T_MqttMsg s_dropOldestScratch;

static int enqueue_prepared_msg_locked(void)
{
    if (xQueueSend(s_mqttQueue, &s_enqueueScratch, 0) == pdPASS) {
        return 0;
    }
    /* 队列满：丢最旧一条再试，优先保留最新控制 ack / 状态帧。 */
    if (xQueueReceive(s_mqttQueue, &s_dropOldestScratch, 0) == pdTRUE) {
        static const char w[] = "[mqtt_task] queue full, dropped oldest\r\n";
        (void)UART_Write(UART_NUM_1, (const uint8_t *)w, (uint16_t)(sizeof(w) - 1U));
        if (xQueueSend(s_mqttQueue, &s_enqueueScratch, 0) == pdPASS) {
            return 0;
        }
    }
    {
        static const char e[] = "[mqtt_task] queue full, enqueue fail after drop\r\n";
        (void)UART_Write(UART_NUM_1, (const uint8_t *)e, (uint16_t)(sizeof(e) - 1U));
    }
    return -1;
}

static void MqttTask_Run(void *arg)
{
    T_MqttMsg msg;
    (void)arg;
    for (;;) {
        if (xQueueReceive(s_mqttQueue, &msg, portMAX_DELAY) == pdPASS) {
            if (!Ds800Protocol_GetFourGEnabled()) {
                continue;
            }
            int rc = Air780eMqtt_Publish(msg.topic, msg.payload);
            if (rc != 0) {
                char line[160];
                int n = snprintf(line, sizeof(line), "[mqtt_task] publish FAIL rc=%d err=%d (%s)\r\n",
                                 rc, Air780eMqtt_GetLastError(), Air780eMqtt_GetLastErrorString());
                if (n > 0) {
                    if (n >= (int)sizeof(line))
                        n = (int)sizeof(line) - 1;
                    (void)UART_Write(UART_NUM_1, (const uint8_t *)line, (uint16_t)n);
                }
            }
        }
    }
}

void MqttTask_Start(void)
{
    if (s_mqttQueue == NULL) {
        s_mqttQueue = xQueueCreate(MQTT_QUEUE_DEPTH, sizeof(T_MqttMsg));
    }
    if (s_enqueueMutex == NULL) {
        s_enqueueMutex = xSemaphoreCreateMutex();
    }
    if (s_mqttQueue != NULL && s_mqttTask == NULL) {
        if (xTaskCreate(MqttTask_Run, "mqtt_task", MQTT_TASK_STACK_SIZE, NULL, MQTT_TASK_PRIORITY, &s_mqttTask) !=
            pdPASS) {
            const char *m = "[mqtt_task] xTaskCreate FAILED\r\n";
            UART_Write(UART_NUM_1, (const uint8_t *)m, (uint16_t)strlen(m));
        }
    }
}

void MqttTask_SetTopic(const char *topic)
{
    size_t len;
    if (topic == NULL || topic[0] == '\0')
        return;
    len = strlen(topic);
    if (len > MQTT_TOPIC_MAX_LEN)
        len = MQTT_TOPIC_MAX_LEN;
    memcpy(s_topic, topic, len);
    s_topic[len] = '\0';
}

int MqttTask_EnqueueHex(const uint8_t *data, uint16_t len)
{
    static const char hex[] = "0123456789ABCDEF";
    uint16_t i;
    int rc;

    if (s_mqttQueue == NULL || s_enqueueMutex == NULL || data == NULL || len == 0 || len > MQTT_HEX_RAW_MAX)
        return -1;
    if (!Ds800Protocol_GetFourGEnabled())
        return -1;
    if (xSemaphoreTake(s_enqueueMutex, pdMS_TO_TICKS(20)) != pdTRUE)
        return -1;

    {
        size_t tlen = strlen(s_topic);
        memcpy(s_enqueueScratch.topic, s_topic, tlen);
        s_enqueueScratch.topic[tlen] = '\0';
    }

    if ((uint32_t)len * 2U > MQTT_PAYLOAD_MAX_LEN)
    {
        (void)xSemaphoreGive(s_enqueueMutex);
        return -1;
    }

    for (i = 0; i < len; i++) {
        s_enqueueScratch.payload[i * 2] = hex[(data[i] >> 4) & 0x0F];
        s_enqueueScratch.payload[i * 2 + 1] = hex[data[i] & 0x0F];
    }
    s_enqueueScratch.payload[len * 2] = '\0';

    rc = enqueue_prepared_msg_locked();
    (void)xSemaphoreGive(s_enqueueMutex);
    return rc;
}

int MqttTask_EnqueueText(const char *topic, const char *payload)
{
    size_t topicLen;
    size_t payloadLen;
    int rc;

    if (s_mqttQueue == NULL || s_enqueueMutex == NULL || topic == NULL || payload == NULL)
        return -1;
    if (!Ds800Protocol_GetFourGEnabled())
        return -1;
    topicLen = strlen(topic);
    payloadLen = strlen(payload);
    if (topicLen == 0U || topicLen > MQTT_TOPIC_MAX_LEN || payloadLen == 0U || payloadLen > MQTT_PAYLOAD_MAX_LEN)
        return -1;
    if (xSemaphoreTake(s_enqueueMutex, pdMS_TO_TICKS(20)) != pdTRUE)
        return -1;

    memcpy(s_enqueueScratch.topic, topic, topicLen);
    s_enqueueScratch.topic[topicLen] = '\0';
    memcpy(s_enqueueScratch.payload, payload, payloadLen);
    s_enqueueScratch.payload[payloadLen] = '\0';

    rc = enqueue_prepared_msg_locked();
    (void)xSemaphoreGive(s_enqueueMutex);
    return rc;
}
