#include "ppp_mqtt.h"

#include "../common/app_string.h"

#include "air780e_ppp_config.h"
#include "cmsis_os.h"
#include "uart.h"
#include "FreeRTOS.h"
#include "task.h"

#include "lwip/dns.h"
#include "lwip/ip_addr.h"
#include "lwip/tcp.h"
#include "lwip/pbuf.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define PPP_MQTT_LOG_UART          UART_NUM_1
#define PPP_MQTT_TX_MAX            3072U
#define PPP_MQTT_RX_BODY_MAX       2048U
#define PPP_MQTT_TOPIC_MAX_LEN     96U
#define PPP_MQTT_OUT_PAYLOAD_MAX   2048U
#define PPP_MQTT_OUT_QUEUE_DEPTH   8U
#define PPP_MQTT_OUT_DRAIN_MAX     4U
#define PPP_MQTT_CONNECT_TIMEOUT   30000U
#define PPP_MQTT_KEEPALIVE_S       30U
#define PPP_MQTT_PINGRESP_TIMEOUT_MS 20000U
#define PPP_MQTT_POLL_INTERVAL     4U

#define MQTT_PACKET_CONNACK        2U
#define MQTT_PACKET_PUBLISH        3U
#define MQTT_PACKET_SUBACK         9U
#define MQTT_PACKET_PINGRESP       13U

typedef enum {
    PPP_MQTT_IDLE = 0,
    PPP_MQTT_DNS_WAIT,
    PPP_MQTT_TCP_CONNECTING,
    PPP_MQTT_WAIT_CONNACK,
    PPP_MQTT_RUNNING,
    PPP_MQTT_FAILED
} T_PppMqttState;

typedef struct {
    T_PppMqttState state;
    struct tcp_pcb *pcb;
    ip_addr_t server_ip;
    uint32_t started_ms;
    uint32_t last_tx_ms;
    uint32_t last_rx_ms;
    uint32_t last_ping_ms;
    uint8_t ping_outstanding;
    int last_error;
    int last_connack_rc;
    char last_error_text[64];

    char host[64];
    uint16_t port;
    char client_id[40];
    char username[40];
    char password[40];
    char will_topic[PPP_MQTT_TOPIC_MAX_LEN + 1U];
    char will_payload[160];
    uint8_t will_retain;

    uint8_t rx_fixed;
    uint8_t rx_stage;
    uint32_t rx_remaining;
    uint32_t rx_multiplier;
    uint8_t rx_rem_bytes;
    uint32_t rx_body_used;
    uint8_t rx_body[PPP_MQTT_RX_BODY_MAX];

    uint16_t packet_id;
    Air780eMqtt_MessageCb msg_cb;
    void *msg_cb_user;
} T_PppMqtt;

typedef struct {
    char topic[PPP_MQTT_TOPIC_MAX_LEN + 1U];
    char payload[PPP_MQTT_OUT_PAYLOAD_MAX + 1U];
    uint8_t retain;
} T_PppMqttOutMsg;

typedef void (*T_MqttPacketHandler)(uint8_t header, const uint8_t *body, uint32_t len);

typedef struct {
    uint8_t type;
    T_MqttPacketHandler handler;
} T_MqttPacketDispatch;

static T_PppMqtt s_mqtt;
static uint8_t s_tx[PPP_MQTT_TX_MAX];
static T_PppMqttOutMsg s_out_queue[PPP_MQTT_OUT_QUEUE_DEPTH];
static volatile uint8_t s_out_head;
static volatile uint8_t s_out_tail;
static volatile uint8_t s_out_count;
static volatile uint32_t s_out_dropped;

static int mqtt_publish_now(const char *topic, const char *payload, int retain);

static void mqtt_log(const char *msg)
{
    if (msg == NULL) {
        return;
    }
    UART_Write(PPP_MQTT_LOG_UART, (const uint8_t *)msg, (uint16_t)strlen(msg));
    UART_Write(PPP_MQTT_LOG_UART, (const uint8_t *)"\r\n", 2U);
}

static void mqtt_logf(const char *fmt, ...)
{
    char line[192];
    va_list ap;
    int n;

    va_start(ap, fmt);
    n = vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    if (n <= 0) {
        return;
    }
    if (n >= (int)sizeof(line)) {
        n = (int)sizeof(line) - 1;
    }
    UART_Write(PPP_MQTT_LOG_UART, (const uint8_t *)line, (uint16_t)n);
    UART_Write(PPP_MQTT_LOG_UART, (const uint8_t *)"\r\n", 2U);
}

static void mqtt_set_error(int err, const char *text)
{
    s_mqtt.last_error = err;
    if (text == NULL) {
        text = "unknown";
    }
    AppString_Copy(s_mqtt.last_error_text, sizeof(s_mqtt.last_error_text), text);
}

static void mqtt_out_queue_reset(void)
{
    taskENTER_CRITICAL();
    s_out_head = 0U;
    s_out_tail = 0U;
    s_out_count = 0U;
    taskEXIT_CRITICAL();
}

static int mqtt_out_enqueue(const char *topic, const char *payload, int retain)
{
    size_t topic_len;
    size_t payload_len;
    uint8_t slot;
    uint8_t dropped = 0U;

    if (!PppMqtt_IsConnected() || topic == NULL || payload == NULL) {
        mqtt_set_error(20, "publish-not-ready");
        return -1;
    }

    topic_len = strlen(topic);
    payload_len = strlen(payload);
    if (topic_len == 0U || topic_len > PPP_MQTT_TOPIC_MAX_LEN ||
        payload_len == 0U || payload_len > PPP_MQTT_OUT_PAYLOAD_MAX) {
        mqtt_set_error(21, "publish-too-long");
        return -1;
    }

    taskENTER_CRITICAL();
    if (s_out_count >= PPP_MQTT_OUT_QUEUE_DEPTH) {
        s_out_tail = (uint8_t)((s_out_tail + 1U) % PPP_MQTT_OUT_QUEUE_DEPTH);
        s_out_count--;
        s_out_dropped++;
        dropped = 1U;
    }
    slot = s_out_head;
    s_out_head = (uint8_t)((s_out_head + 1U) % PPP_MQTT_OUT_QUEUE_DEPTH);
    s_out_count++;

    AppString_Copy(s_out_queue[slot].topic, sizeof(s_out_queue[slot].topic), topic);
    AppString_Copy(s_out_queue[slot].payload, sizeof(s_out_queue[slot].payload), payload);
    s_out_queue[slot].retain = retain ? 1U : 0U;
    taskEXIT_CRITICAL();

    if (dropped != 0U) {
        mqtt_logf("[MQTT/PPP] publish queue full, dropped oldest total=%lu",
                  (unsigned long)s_out_dropped);
    }
    return 0;
}

static int mqtt_out_peek(T_PppMqttOutMsg *out)
{
    uint8_t slot;

    if (out == NULL) {
        return 0;
    }

    taskENTER_CRITICAL();
    if (s_out_count == 0U) {
        taskEXIT_CRITICAL();
        return 0;
    }
    slot = s_out_tail;
    *out = s_out_queue[slot];
    taskEXIT_CRITICAL();
    return 1;
}

static void mqtt_out_drop_tail(void)
{
    taskENTER_CRITICAL();
    if (s_out_count != 0U) {
        s_out_tail = (uint8_t)((s_out_tail + 1U) % PPP_MQTT_OUT_QUEUE_DEPTH);
        s_out_count--;
    }
    taskEXIT_CRITICAL();
}

static uint8_t mqtt_encode_remaining(uint32_t len, uint8_t *out)
{
    uint8_t n = 0U;

    do {
        uint8_t byte = (uint8_t)(len % 128U);
        len /= 128U;
        if (len > 0U) {
            byte |= 0x80U;
        }
        out[n++] = byte;
    } while (len > 0U && n < 4U);

    return n;
}

static int mqtt_put_string(uint8_t *buf, uint32_t *pos, uint32_t max, const char *s)
{
    uint32_t len;

    if (s == NULL) {
        s = "";
    }
    len = (uint32_t)strlen(s);
    if (len > 65535U || (*pos + 2U + len) > max) {
        return 0;
    }
    buf[(*pos)++] = (uint8_t)(len >> 8);
    buf[(*pos)++] = (uint8_t)(len & 0xFFU);
    memcpy(buf + *pos, s, len);
    *pos += len;
    return 1;
}

static err_t mqtt_send_raw(const uint8_t *data, uint32_t len)
{
    err_t err;

    if (s_mqtt.pcb == NULL || data == NULL || len == 0U || len > 65535U) {
        mqtt_set_error(1, "bad-send-arg");
        return ERR_ARG;
    }
    if (tcp_sndbuf(s_mqtt.pcb) < (u16_t)len) {
        mqtt_set_error(2, "tcp-backpressure");
        return ERR_MEM;
    }

    err = tcp_write(s_mqtt.pcb, data, (u16_t)len, TCP_WRITE_FLAG_COPY);
    if (err != ERR_OK) {
        mqtt_set_error(2, "tcp-write");
        mqtt_logf("[MQTT/PPP] tcp_write failed err=%d", (int)err);
        return err;
    }
    err = tcp_output(s_mqtt.pcb);
    if (err != ERR_OK) {
        mqtt_set_error(3, "tcp-output");
        mqtt_logf("[MQTT/PPP] tcp_output failed err=%d", (int)err);
        return err;
    }
    s_mqtt.last_tx_ms = osKernelGetTickCount();
    return ERR_OK;
}

static int mqtt_send_connect(void)
{
    uint8_t body[PPP_MQTT_TX_MAX];
    uint32_t body_len = 0U;
    uint32_t pos = 0U;
    uint8_t rem[4];
    uint8_t rem_len;
    uint8_t flags = 0x02U;

    if (s_mqtt.username[0] != '\0') {
        flags |= 0x80U;
    }
    if (s_mqtt.password[0] != '\0') {
        flags |= 0x40U;
    }
    if (s_mqtt.will_topic[0] != '\0') {
        flags |= 0x04U;
        if (s_mqtt.will_retain != 0U) {
            flags |= 0x20U;
        }
    }

    if (!mqtt_put_string(body, &body_len, sizeof(body), "MQTT")) {
        return -1;
    }
    if ((body_len + 4U) > sizeof(body)) {
        return -1;
    }
    body[body_len++] = 4U;
    body[body_len++] = flags;
    body[body_len++] = 0U;
    body[body_len++] = PPP_MQTT_KEEPALIVE_S;
    if (!mqtt_put_string(body, &body_len, sizeof(body), s_mqtt.client_id)) {
        return -1;
    }
    if (s_mqtt.will_topic[0] != '\0') {
        if (!mqtt_put_string(body, &body_len, sizeof(body), s_mqtt.will_topic) ||
            !mqtt_put_string(body, &body_len, sizeof(body), s_mqtt.will_payload)) {
            return -1;
        }
    }
    if (s_mqtt.username[0] != '\0' &&
        !mqtt_put_string(body, &body_len, sizeof(body), s_mqtt.username)) {
        return -1;
    }
    if (s_mqtt.password[0] != '\0' &&
        !mqtt_put_string(body, &body_len, sizeof(body), s_mqtt.password)) {
        return -1;
    }

    rem_len = mqtt_encode_remaining(body_len, rem);
    if ((1U + rem_len + body_len) > sizeof(s_tx)) {
        return -1;
    }
    s_tx[pos++] = 0x10U;
    memcpy(s_tx + pos, rem, rem_len);
    pos += rem_len;
    memcpy(s_tx + pos, body, body_len);
    pos += body_len;

    mqtt_logf("[MQTT/PPP] CONNECT clientId=%s", s_mqtt.client_id);
    return (mqtt_send_raw(s_tx, pos) == ERR_OK) ? 0 : -1;
}

static void mqtt_reset_parser(void)
{
    s_mqtt.rx_fixed = 0U;
    s_mqtt.rx_stage = 0U;
    s_mqtt.rx_remaining = 0U;
    s_mqtt.rx_multiplier = 1U;
    s_mqtt.rx_rem_bytes = 0U;
    s_mqtt.rx_body_used = 0U;
}

static int mqtt_send_puback(uint16_t pid)
{
    uint8_t puback[4];

    if (pid == 0U) {
        return -1;
    }

    puback[0] = 0x40U;
    puback[1] = 0x02U;
    puback[2] = (uint8_t)(pid >> 8);
    puback[3] = (uint8_t)(pid & 0xFFU);
    return (mqtt_send_raw(puback, sizeof(puback)) == ERR_OK) ? 0 : -1;
}

static void mqtt_handle_publish(uint8_t header, const uint8_t *body, uint32_t len)
{
    uint16_t topic_len;
    uint32_t pos = 0U;
    uint32_t payload_len;
    const uint8_t *payload;
    uint8_t qos = (uint8_t)((header >> 1) & 0x03U);
#if SPEAKER_LOG_MQTT_PACKET_TRACE
    uint8_t dup = (uint8_t)((header & 0x08U) ? 1U : 0U);
#endif
    uint16_t pid = 0U;

    if (body == NULL || len < 2U) {
        return;
    }
    topic_len = (uint16_t)(((uint16_t)body[0] << 8) | body[1]);
    pos = 2U;
    if ((pos + topic_len) > len || topic_len > PPP_MQTT_TOPIC_MAX_LEN) {
        mqtt_log("[MQTT/PPP] bad PUBLISH topic");
        return;
    }
    pos += topic_len;
    if (qos > 0U) {
        if ((pos + 2U) > len) {
            return;
        }
        pid = (uint16_t)(((uint16_t)body[pos] << 8) | body[pos + 1U]);
        pos += 2U;
    }
    payload = body + pos;
    payload_len = len - pos;
#if SPEAKER_LOG_MQTT_PACKET_TRACE
    mqtt_logf("[MQTT/PPP] RX PUBLISH topicLen=%u payloadLen=%lu qos=%u pid=%u dup=%u",
              (unsigned)topic_len,
              (unsigned long)payload_len,
              (unsigned)qos,
              (unsigned)pid,
              (unsigned)dup);
#endif
    if (s_mqtt.msg_cb != NULL && payload_len <= 65535U) {
        s_mqtt.msg_cb((const char *)(body + 2U), topic_len,
                      payload, (uint16_t)payload_len,
                      s_mqtt.msg_cb_user);
    }
    if (qos == 1U) {
        if (mqtt_send_puback(pid) != 0) {
            mqtt_logf("[MQTT/PPP] PUBACK failed pid=%u", (unsigned)pid);
        }
    } else if (qos > 1U) {
        mqtt_logf("[MQTT/PPP] unsupported incoming qos=%u pid=%u", (unsigned)qos, (unsigned)pid);
    }
}

static void mqtt_handle_connack(uint8_t header, const uint8_t *body, uint32_t len)
{
    (void)header;

    if (s_mqtt.state != PPP_MQTT_WAIT_CONNACK) {
        mqtt_logf("[MQTT/PPP] unexpected CONNACK state=%d len=%lu",
                  (int)s_mqtt.state, (unsigned long)len);
        return;
    }
    if (len >= 2U) {
        s_mqtt.last_connack_rc = body[1];
        if (body[1] == 0U) {
            s_mqtt.state = PPP_MQTT_RUNNING;
            s_mqtt.ping_outstanding = 0U;
            s_mqtt.last_ping_ms = 0U;
            mqtt_set_error(0, "none");
#if SPEAKER_LOG_MQTT_PACKET_TRACE
            mqtt_log("[MQTT/PPP] CONNACK OK");
#endif
        } else {
            s_mqtt.state = PPP_MQTT_FAILED;
            mqtt_set_error(10 + body[1], "connack");
            mqtt_logf("[MQTT/PPP] CONNACK failed rc=%u", (unsigned)body[1]);
        }
    }
}

static void mqtt_handle_suback(uint8_t header, const uint8_t *body, uint32_t len)
{
    (void)header;
    (void)body;
    (void)len;

#if SPEAKER_LOG_MQTT_PACKET_TRACE
    mqtt_log("[MQTT/PPP] SUBACK");
#endif
}

static void mqtt_handle_pingresp(uint8_t header, const uint8_t *body, uint32_t len)
{
    (void)header;
    (void)body;
    (void)len;

    s_mqtt.ping_outstanding = 0U;
    s_mqtt.last_ping_ms = 0U;
}

static const T_MqttPacketDispatch s_packet_dispatch[] = {
    { MQTT_PACKET_CONNACK, mqtt_handle_connack },
    { MQTT_PACKET_PUBLISH, mqtt_handle_publish },
    { MQTT_PACKET_SUBACK, mqtt_handle_suback },
    { MQTT_PACKET_PINGRESP, mqtt_handle_pingresp }
};

static T_MqttPacketHandler mqtt_find_packet_handler(uint8_t type)
{
    uint16_t i;

    for (i = 0U; i < (uint16_t)(sizeof(s_packet_dispatch) / sizeof(s_packet_dispatch[0])); i++) {
        if (s_packet_dispatch[i].type == type) {
            return s_packet_dispatch[i].handler;
        }
    }
    return NULL;
}

static void mqtt_handle_packet(uint8_t header, const uint8_t *body, uint32_t len)
{
    uint8_t type = (uint8_t)(header >> 4);
    T_MqttPacketHandler handler;

    s_mqtt.last_rx_ms = osKernelGetTickCount();
    handler = mqtt_find_packet_handler(type);
    if (handler != NULL) {
        handler(header, body, len);
    } else {
        mqtt_logf("[MQTT/PPP] RX packet type=%u len=%lu",
                  (unsigned)type, (unsigned long)len);
    }
}

static void mqtt_feed_byte(uint8_t b)
{
    if (s_mqtt.rx_stage == 0U) {
        s_mqtt.rx_fixed = b;
        s_mqtt.rx_remaining = 0U;
        s_mqtt.rx_multiplier = 1U;
        s_mqtt.rx_rem_bytes = 0U;
        s_mqtt.rx_body_used = 0U;
        s_mqtt.rx_stage = 1U;
        return;
    }

    if (s_mqtt.rx_stage == 1U) {
        s_mqtt.rx_remaining += (uint32_t)(b & 0x7FU) * s_mqtt.rx_multiplier;
        s_mqtt.rx_multiplier *= 128U;
        s_mqtt.rx_rem_bytes++;
        if ((b & 0x80U) != 0U) {
            if (s_mqtt.rx_rem_bytes >= 4U) {
                mqtt_log("[MQTT/PPP] bad remaining length");
                mqtt_reset_parser();
            }
            return;
        }
        if (s_mqtt.rx_remaining > PPP_MQTT_RX_BODY_MAX) {
            mqtt_logf("[MQTT/PPP] packet too large len=%lu",
                      (unsigned long)s_mqtt.rx_remaining);
            mqtt_reset_parser();
            return;
        }
        if (s_mqtt.rx_remaining == 0U) {
            mqtt_handle_packet(s_mqtt.rx_fixed, NULL, 0U);
            mqtt_reset_parser();
            return;
        }
        s_mqtt.rx_stage = 2U;
        return;
    }

    if (s_mqtt.rx_body_used < s_mqtt.rx_remaining &&
        s_mqtt.rx_body_used < sizeof(s_mqtt.rx_body)) {
        s_mqtt.rx_body[s_mqtt.rx_body_used++] = b;
    }
    if (s_mqtt.rx_body_used >= s_mqtt.rx_remaining) {
        mqtt_handle_packet(s_mqtt.rx_fixed, s_mqtt.rx_body, s_mqtt.rx_remaining);
        mqtt_reset_parser();
    }
}

static void mqtt_tcp_cleanup(void)
{
    if (s_mqtt.pcb != NULL) {
        tcp_arg(s_mqtt.pcb, NULL);
        tcp_recv(s_mqtt.pcb, NULL);
        tcp_poll(s_mqtt.pcb, NULL, 0U);
        tcp_err(s_mqtt.pcb, NULL);
        if (tcp_close(s_mqtt.pcb) != ERR_OK) {
            tcp_abort(s_mqtt.pcb);
        }
        s_mqtt.pcb = NULL;
    }
}

static err_t mqtt_recv_cb(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err)
{
    struct pbuf *q;

    (void)arg;
    if (err != ERR_OK) {
        s_mqtt.state = PPP_MQTT_FAILED;
        mqtt_set_error(4, "recv");
        return ERR_OK;
    }
    if (p == NULL) {
        s_mqtt.pcb = NULL;
        s_mqtt.state = PPP_MQTT_FAILED;
        mqtt_set_error(5, "closed");
        mqtt_log("[MQTT/PPP] TCP closed");
        return ERR_OK;
    }

    tcp_recved(tpcb, p->tot_len);
    for (q = p; q != NULL; q = q->next) {
        const uint8_t *data = (const uint8_t *)q->payload;
        uint16_t i;
        for (i = 0U; i < q->len; i++) {
            mqtt_feed_byte(data[i]);
        }
    }
    pbuf_free(p);
    return ERR_OK;
}

static void mqtt_err_cb(void *arg, err_t err)
{
    (void)arg;
    s_mqtt.pcb = NULL;
    s_mqtt.state = PPP_MQTT_FAILED;
    mqtt_set_error(6, "tcp-error");
    mqtt_logf("[MQTT/PPP] TCP error err=%d", (int)err);
}

static err_t mqtt_poll_cb(void *arg, struct tcp_pcb *tpcb)
{
    (void)arg;

    if ((s_mqtt.state == PPP_MQTT_TCP_CONNECTING ||
         s_mqtt.state == PPP_MQTT_WAIT_CONNACK) &&
        (osKernelGetTickCount() - s_mqtt.started_ms) > PPP_MQTT_CONNECT_TIMEOUT) {
        s_mqtt.state = PPP_MQTT_FAILED;
        if (s_mqtt.pcb == tpcb) {
            s_mqtt.pcb = NULL;
        }
        mqtt_set_error(7, "timeout");
        mqtt_log("[MQTT/PPP] connect timeout");
        tcp_abort(tpcb);
        return ERR_ABRT;
    }

    return ERR_OK;
}

static err_t mqtt_connected_cb(void *arg, struct tcp_pcb *tpcb, err_t err)
{
    (void)arg;
    (void)tpcb;

    if (err != ERR_OK) {
        s_mqtt.state = PPP_MQTT_FAILED;
        mqtt_set_error(8, "tcp-connect");
        mqtt_logf("[MQTT/PPP] TCP connect failed err=%d", (int)err);
        return ERR_OK;
    }

    s_mqtt.state = PPP_MQTT_WAIT_CONNACK;
#if SPEAKER_LOG_MQTT_PACKET_TRACE
    mqtt_log("[MQTT/PPP] TCP connected");
#endif
    if (mqtt_send_connect() != 0) {
        s_mqtt.state = PPP_MQTT_FAILED;
        mqtt_set_error(9, "connect-send");
    }
    return ERR_OK;
}

static void mqtt_connect_ip(const ip_addr_t *addr)
{
    err_t err;

    if (addr == NULL) {
        s_mqtt.state = PPP_MQTT_FAILED;
        mqtt_set_error(11, "dns-no-ip");
        return;
    }

    s_mqtt.pcb = tcp_new();
    if (s_mqtt.pcb == NULL) {
        s_mqtt.state = PPP_MQTT_FAILED;
        mqtt_set_error(12, "tcp-new");
        mqtt_log("[MQTT/PPP] tcp_new failed");
        return;
    }

    tcp_arg(s_mqtt.pcb, &s_mqtt);
    tcp_recv(s_mqtt.pcb, mqtt_recv_cb);
    tcp_poll(s_mqtt.pcb, mqtt_poll_cb, PPP_MQTT_POLL_INTERVAL);
    tcp_err(s_mqtt.pcb, mqtt_err_cb);

    s_mqtt.state = PPP_MQTT_TCP_CONNECTING;
#if SPEAKER_LOG_MQTT_PACKET_TRACE
    mqtt_logf("[MQTT/PPP] connecting port=%u", (unsigned)s_mqtt.port);
#endif
    err = tcp_connect(s_mqtt.pcb, addr, s_mqtt.port, mqtt_connected_cb);
    if (err != ERR_OK) {
        mqtt_tcp_cleanup();
        s_mqtt.state = PPP_MQTT_FAILED;
        mqtt_set_error(13, "tcp-connect-start");
        mqtt_logf("[MQTT/PPP] tcp_connect start failed err=%d", (int)err);
    }
}

static void mqtt_dns_cb(const char *name, const ip_addr_t *ipaddr, void *arg)
{
    (void)arg;
    if (ipaddr == NULL) {
        s_mqtt.state = PPP_MQTT_FAILED;
        mqtt_set_error(14, "dns");
        mqtt_logf("[MQTT/PPP] dns failed host=%s", (name != NULL) ? name : "?");
        return;
    }
#if SPEAKER_LOG_MQTT_PACKET_TRACE
    mqtt_logf("[MQTT/PPP] dns OK host=%s", (name != NULL) ? name : s_mqtt.host);
#endif
    s_mqtt.server_ip = *ipaddr;
    mqtt_connect_ip(&s_mqtt.server_ip);
}

void PppMqtt_Reset(void)
{
    Air780eMqtt_MessageCb cb = s_mqtt.msg_cb;
    void *cb_user = s_mqtt.msg_cb_user;

    mqtt_tcp_cleanup();
    mqtt_out_queue_reset();
    memset(&s_mqtt, 0, sizeof(s_mqtt));
    s_mqtt.state = PPP_MQTT_IDLE;
    s_mqtt.last_connack_rc = -1;
    s_mqtt.msg_cb = cb;
    s_mqtt.msg_cb_user = cb_user;
    mqtt_set_error(0, "none");
}

int PppMqtt_Start(const char *host, uint16_t port,
                  const char *client_id,
                  const char *username, const char *password,
                  const char *will_topic, const char *will_payload,
                  int will_retain)
{
    err_t err;
    ip_addr_t addr;

    if (host == NULL || host[0] == '\0' || client_id == NULL || client_id[0] == '\0') {
        mqtt_set_error(1, "bad-start-arg");
        return -1;
    }

    PppMqtt_Reset();
    AppString_Copy(s_mqtt.host, sizeof(s_mqtt.host), host);
    AppString_Copy(s_mqtt.client_id, sizeof(s_mqtt.client_id), client_id);
    AppString_Copy(s_mqtt.username, sizeof(s_mqtt.username), username);
    AppString_Copy(s_mqtt.password, sizeof(s_mqtt.password), password);
    AppString_Copy(s_mqtt.will_topic, sizeof(s_mqtt.will_topic), will_topic);
    AppString_Copy(s_mqtt.will_payload, sizeof(s_mqtt.will_payload), will_payload);
    s_mqtt.will_retain = will_retain ? 1U : 0U;
    s_mqtt.port = port;
    s_mqtt.started_ms = osKernelGetTickCount();
    s_mqtt.last_rx_ms = s_mqtt.started_ms;
    s_mqtt.last_tx_ms = s_mqtt.started_ms;

#if SPEAKER_LOG_MQTT_PACKET_TRACE
    mqtt_logf("[MQTT/PPP] start host=%s port=%u", s_mqtt.host, (unsigned)s_mqtt.port);
#endif
    if (ipaddr_aton(s_mqtt.host, &addr) != 0) {
        s_mqtt.server_ip = addr;
        mqtt_connect_ip(&s_mqtt.server_ip);
        return 0;
    }

    s_mqtt.state = PPP_MQTT_DNS_WAIT;
    err = dns_gethostbyname(s_mqtt.host, &addr, mqtt_dns_cb, NULL);
    if (err == ERR_OK) {
        s_mqtt.server_ip = addr;
        mqtt_connect_ip(&s_mqtt.server_ip);
    } else if (err == ERR_INPROGRESS) {
#if SPEAKER_LOG_MQTT_PACKET_TRACE
        mqtt_log("[MQTT/PPP] dns resolving");
#endif
    } else {
        s_mqtt.state = PPP_MQTT_FAILED;
        mqtt_set_error(15, "dns-start");
        mqtt_logf("[MQTT/PPP] dns start failed err=%d", (int)err);
        return -1;
    }

    return 0;
}

void PppMqtt_Service(void)
{
    uint32_t now = osKernelGetTickCount();

    if ((s_mqtt.state == PPP_MQTT_DNS_WAIT ||
         s_mqtt.state == PPP_MQTT_TCP_CONNECTING ||
         s_mqtt.state == PPP_MQTT_WAIT_CONNACK) &&
        (now - s_mqtt.started_ms) > PPP_MQTT_CONNECT_TIMEOUT) {
        mqtt_tcp_cleanup();
        s_mqtt.state = PPP_MQTT_FAILED;
        mqtt_set_error(7, "timeout");
        mqtt_log("[MQTT/PPP] connect timeout");
        return;
    }

    if (s_mqtt.state == PPP_MQTT_RUNNING &&
        s_mqtt.ping_outstanding == 0U &&
        (now - s_mqtt.last_tx_ms) > (uint32_t)(PPP_MQTT_KEEPALIVE_S * 1000U)) {
        if (PppMqtt_Pingreq() != 0) {
            mqtt_tcp_cleanup();
            s_mqtt.state = PPP_MQTT_FAILED;
            mqtt_set_error(15, "ping-send-failed");
            return;
        }
    }

    if (s_mqtt.state == PPP_MQTT_RUNNING &&
        s_mqtt.ping_outstanding != 0U &&
        (now - s_mqtt.last_ping_ms) > PPP_MQTT_PINGRESP_TIMEOUT_MS) {
        mqtt_logf("[MQTT/PPP] ping timeout lastPing=%lu now=%lu",
                  (unsigned long)s_mqtt.last_ping_ms,
                  (unsigned long)now);
        mqtt_tcp_cleanup();
        s_mqtt.state = PPP_MQTT_FAILED;
        mqtt_set_error(15, "ping-timeout");
        return;
    }

    if (s_mqtt.state == PPP_MQTT_RUNNING) {
        T_PppMqttOutMsg msg;
        uint8_t drained = 0U;
        while (drained < PPP_MQTT_OUT_DRAIN_MAX && mqtt_out_peek(&msg)) {
            if (mqtt_publish_now(msg.topic, msg.payload, msg.retain) != 0) {
                break;
            }
            mqtt_out_drop_tail();
            drained++;
        }
    }
}

bool PppMqtt_IsConnected(void)
{
    return s_mqtt.state == PPP_MQTT_RUNNING;
}

bool PppMqtt_IsFailed(void)
{
    return s_mqtt.state == PPP_MQTT_FAILED;
}

int PppMqtt_GetLastError(void)
{
    return s_mqtt.last_error;
}

int PppMqtt_GetLastConnackRc(void)
{
    return s_mqtt.last_connack_rc;
}

const char *PppMqtt_GetLastErrorString(void)
{
    return s_mqtt.last_error_text;
}

static int mqtt_publish_now(const char *topic, const char *payload, int retain)
{
    uint32_t pos = 0U;
    uint32_t body_len;
    uint32_t topic_len;
    uint32_t payload_len;
    uint8_t rem[4];
    uint8_t rem_len;

    if (!PppMqtt_IsConnected() || topic == NULL || payload == NULL) {
        mqtt_set_error(20, "publish-not-ready");
        return -1;
    }
    topic_len = (uint32_t)strlen(topic);
    payload_len = (uint32_t)strlen(payload);
    body_len = 2U + topic_len + payload_len;
    rem_len = mqtt_encode_remaining(body_len, rem);
    if (topic_len > PPP_MQTT_TOPIC_MAX_LEN ||
        (1U + rem_len + body_len) > sizeof(s_tx)) {
        mqtt_set_error(21, "publish-too-long");
        return -1;
    }

    s_tx[pos++] = retain ? 0x31U : 0x30U;
    memcpy(s_tx + pos, rem, rem_len);
    pos += rem_len;
    s_tx[pos++] = (uint8_t)(topic_len >> 8);
    s_tx[pos++] = (uint8_t)(topic_len & 0xFFU);
    memcpy(s_tx + pos, topic, topic_len);
    pos += topic_len;
    memcpy(s_tx + pos, payload, payload_len);
    pos += payload_len;

#if SPEAKER_LOG_MQTT_PACKET_TRACE
    mqtt_logf("[MQTT/PPP] PUBLISH topic=%s len=%lu retain=%d",
              topic, (unsigned long)payload_len, retain ? 1 : 0);
#endif
    return (mqtt_send_raw(s_tx, pos) == ERR_OK) ? 0 : -1;
}

int PppMqtt_Publish(const char *topic, const char *payload, int retain)
{
    return mqtt_out_enqueue(topic, payload, retain);
}

int PppMqtt_Subscribe(const char *topic, int qos_req)
{
    uint32_t pos = 0U;
    uint32_t body_len;
    uint32_t topic_len;
    uint8_t rem[4];
    uint8_t rem_len;
    uint16_t pid;

    if (!PppMqtt_IsConnected() || topic == NULL || topic[0] == '\0') {
        mqtt_set_error(30, "subscribe-not-ready");
        return -1;
    }
    topic_len = (uint32_t)strlen(topic);
    body_len = 2U + 2U + topic_len + 1U;
    rem_len = mqtt_encode_remaining(body_len, rem);
    if (topic_len > PPP_MQTT_TOPIC_MAX_LEN ||
        (1U + rem_len + body_len) > sizeof(s_tx)) {
        mqtt_set_error(31, "subscribe-too-long");
        return -1;
    }

    pid = ++s_mqtt.packet_id;
    if (pid == 0U) {
        pid = ++s_mqtt.packet_id;
    }
    s_tx[pos++] = 0x82U;
    memcpy(s_tx + pos, rem, rem_len);
    pos += rem_len;
    s_tx[pos++] = (uint8_t)(pid >> 8);
    s_tx[pos++] = (uint8_t)(pid & 0xFFU);
    s_tx[pos++] = (uint8_t)(topic_len >> 8);
    s_tx[pos++] = (uint8_t)(topic_len & 0xFFU);
    memcpy(s_tx + pos, topic, topic_len);
    pos += topic_len;
    s_tx[pos++] = (uint8_t)(qos_req ? 1U : 0U);

#if SPEAKER_LOG_MQTT_PACKET_TRACE
    mqtt_logf("[MQTT/PPP] SUBSCRIBE topic=%s qos=%d", topic, qos_req ? 1 : 0);
#endif
    return (mqtt_send_raw(s_tx, pos) == ERR_OK) ? 0 : -1;
}

int PppMqtt_Pingreq(void)
{
    static const uint8_t pingreq[] = {0xC0U, 0x00U};
    err_t err;

    if (!PppMqtt_IsConnected()) {
        return -1;
    }
    err = mqtt_send_raw(pingreq, sizeof(pingreq));
    if (err == ERR_OK) {
        s_mqtt.last_ping_ms = osKernelGetTickCount();
        s_mqtt.ping_outstanding = 1U;
        return 0;
    }
    return -1;
}

void PppMqtt_SetMessageCallback(Air780eMqtt_MessageCb cb, void *user)
{
    s_mqtt.msg_cb = cb;
    s_mqtt.msg_cb_user = user;
}
