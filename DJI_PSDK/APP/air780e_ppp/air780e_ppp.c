#include "air780e_ppp.h"

#include "air780e_ppp_config.h"
#include "air780e_power.h"
#include "ds800_protocol.h"
#include "mqtt_app_config.h"
#include "mqtt_cmd_handler.h"
#include "mqtt_state_publisher.h"
#include "mqtt_task.h"
#include "ppp_mqtt.h"
#include "solarclean_ota.h"
#include "uart.h"

#include "cmsis_os.h"
#include "FreeRTOS.h"
#include "task.h"
#include "stm32h7xx_hal.h"

#include "lwip/init.h"
#include "lwip/timeouts.h"
#include "lwip/dns.h"
#include "lwip/ip4_addr.h"
#include "lwip/tcp.h"
#include "netif/ppp/ppp.h"
#include "netif/ppp/pppos.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define PPP_UART_AT                    UART_NUM_7
#define PPP_UART_LOG                   UART_NUM_1
#define PPP_AT_RX_WINDOW_SIZE          512U
#define PPP_AT_READ_CHUNK_SIZE         128U
#define PPP_AT_POLL_MS                 10U
#define PPP_AT_DEFAULT_TIMEOUT         3000U
#define PPP_DIAL_TIMEOUT_MS            45000U
#define PPP_RETRY_PAUSE_MS             5000U
#define PPP_ONLINE_STATS_MS            5000U
#define PPP_NEGOTIATE_TIMEOUT_MS       90000U
#define PPP_POWER_CYCLE_OFF_MS         1500U
#define PPP_UART_LINE_ERR_WARN_DELTA   10U
#define PPP_UART_LINE_ERR_RESET_STREAK 3U

static struct netif s_ppp_netif;
static ppp_pcb *s_ppp_pcb;
static volatile uint8_t s_lwip_inited;
static volatile uint8_t s_ppp_link_up;
static volatile int s_ppp_last_error = PPPERR_NONE;
static volatile uint8_t s_ppp_last_phase = 0xFFU;
static volatile uint8_t s_ppp_reconnect_requested;
static uint32_t s_ppp_uart_baud = AIR780E_PPP_UART_BOOT_BAUD;

static uint8_t s_mqtt_started;
static uint8_t s_mqtt_announced;
static uint8_t s_mqtt_fail_count;
static uint32_t s_mqtt_retry_after_ms;
static char s_mqtt_device_id[MQTT_CLIENT_ID_BUF_LEN];
static char s_lifecycle_topic[MQTT_TOPIC_SOLARCLEAN_MAX];

static void ppp_log(const char *msg)
{
    if (msg == NULL) {
        return;
    }
    (void)UART_Write(PPP_UART_LOG, (const uint8_t *)msg, (uint16_t)strlen(msg));
    (void)UART_Write(PPP_UART_LOG, (const uint8_t *)"\r\n", 2U);
}

static void ppp_logf(const char *fmt, ...)
{
    char line[192];
    va_list ap;
    int n;

    if (fmt == NULL) {
        return;
    }

    va_start(ap, fmt);
    n = vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    if (n <= 0) {
        return;
    }
    if (n >= (int)sizeof(line)) {
        n = (int)sizeof(line) - 1;
    }
    (void)UART_Write(PPP_UART_LOG, (const uint8_t *)line, (uint16_t)n);
    (void)UART_Write(PPP_UART_LOG, (const uint8_t *)"\r\n", 2U);
}

static uint8_t ppp_timeout_expired(uint32_t start_ms, uint32_t timeout_ms)
{
    return ((int32_t)((uint32_t)osKernelGetTickCount() - start_ms - timeout_ms) >= 0) ? 1U : 0U;
}

bool Air780ePpp_IsLinkUp(void)
{
    return s_ppp_link_up != 0U;
}

void Air780ePpp_RequestReconnect(const char *reason)
{
    uint8_t first_request;

    taskENTER_CRITICAL();
    first_request = (s_ppp_reconnect_requested == 0U) ? 1U : 0U;
    s_ppp_reconnect_requested = 1U;
    taskEXIT_CRITICAL();

    if (first_request != 0U) {
        ppp_logf("[PPP] reconnect requested reason=%s", (reason != NULL) ? reason : "unknown");
    }
}

int Air780ePpp_Publish(const char *topic, const char *payload, int retain)
{
    return PppMqtt_Publish(topic, payload, retain);
}

int Air780ePpp_Subscribe(const char *topic, int qos)
{
    return PppMqtt_Subscribe(topic, qos);
}

void Air780ePpp_SetMessageCallback(Air780eMqtt_MessageCb cb, void *user)
{
    PppMqtt_SetMessageCallback(cb, user);
}

static void ppp_uart_drain(uint32_t duration_ms)
{
    uint8_t tmp[PPP_AT_READ_CHUNK_SIZE];
    uint32_t t0 = (uint32_t)osKernelGetTickCount();

    while (((uint32_t)osKernelGetTickCount() - t0) < duration_ms) {
        int r = UART_Read(PPP_UART_AT, tmp, (uint16_t)sizeof(tmp));
        if (r <= 0) {
            osDelay(PPP_AT_POLL_MS);
        }
    }
}

static int ppp_uart_set_baud(uint32_t baud)
{
    if (s_ppp_uart_baud == baud) {
        return 1;
    }

    if (UART_SetBaudRate(PPP_UART_AT, baud) != 0) {
        ppp_logf("[PPP] modem UART baud switch failed baud=%lu", (unsigned long)baud);
        return 0;
    }

    s_ppp_uart_baud = baud;
    osDelay(100U);
    ppp_uart_drain(100U);
    ppp_logf("[PPP] modem UART baud=%lu", (unsigned long)baud);
    return 1;
}

static void ppp_window_append(char *window, uint32_t *used, const uint8_t *data, uint32_t len)
{
    uint32_t copy_len = len;

    if (copy_len >= PPP_AT_RX_WINDOW_SIZE) {
        data += copy_len - (PPP_AT_RX_WINDOW_SIZE - 1U);
        copy_len = PPP_AT_RX_WINDOW_SIZE - 1U;
        *used = 0U;
    } else if ((*used + copy_len) >= PPP_AT_RX_WINDOW_SIZE) {
        uint32_t keep = (*used > 128U) ? 128U : *used;
        memmove(window, window + (*used - keep), keep);
        *used = keep;
    }

    memcpy(window + *used, data, copy_len);
    *used += copy_len;
    window[*used] = '\0';
}

static int ppp_wait_for(const char *expect1, const char *expect2,
                        const char *fail1, const char *fail2,
                        uint32_t timeout_ms)
{
    uint8_t tmp[PPP_AT_READ_CHUNK_SIZE];
    char window[PPP_AT_RX_WINDOW_SIZE];
    uint32_t used = 0U;
    uint32_t t0 = (uint32_t)osKernelGetTickCount();

    window[0] = '\0';
    while (((uint32_t)osKernelGetTickCount() - t0) < timeout_ms) {
        int r = UART_Read(PPP_UART_AT, tmp, (uint16_t)sizeof(tmp));
        if (r > 0) {
#if SPEAKER_LOG_PPP_AT_RX
            char rx_log[96];
            uint32_t i;
            uint32_t n = ((uint32_t)r > (sizeof(rx_log) - 1U)) ? (sizeof(rx_log) - 1U) : (uint32_t)r;
            for (i = 0U; i < n; i++) {
                uint8_t ch = tmp[i];
                rx_log[i] = (ch >= 0x20U && ch <= 0x7EU) ? (char)ch : '.';
            }
            rx_log[n] = '\0';
            ppp_logf("[PPP][AT<<] len=%d data=%s", r, rx_log);
#endif
            ppp_window_append(window, &used, tmp, (uint32_t)r);
            if (expect1 != NULL && strstr(window, expect1) != NULL) {
                return 1;
            }
            if (expect2 != NULL && strstr(window, expect2) != NULL) {
                return 1;
            }
            if (fail1 != NULL && strstr(window, fail1) != NULL) {
                return 0;
            }
            if (fail2 != NULL && strstr(window, fail2) != NULL) {
                return 0;
            }
            if (strstr(window, "\r\nERROR\r\n") != NULL ||
                strstr(window, "\r\nFAIL") != NULL ||
                strstr(window, "NO CARRIER") != NULL) {
                return 0;
            }
        } else {
            osDelay(PPP_AT_POLL_MS);
        }
    }

    return 0;
}

static int ppp_send_cmd_wait(const char *cmd, const char *expect1,
                             const char *expect2, uint32_t timeout_ms)
{
    if (cmd != NULL && cmd[0] != '\0') {
#if SPEAKER_LOG_PPP_AT_CMD
        ppp_logf("[PPP][AT>>] %s", cmd);
#endif
        (void)UART_Write(PPP_UART_AT, (const uint8_t *)cmd, (uint16_t)strlen(cmd));
        (void)UART_Write(PPP_UART_AT, (const uint8_t *)"\r\n", 2U);
    }

    if (ppp_wait_for(expect1, expect2, "ERROR", "NO CARRIER", timeout_ms)) {
        return 1;
    }

    ppp_logf("[PPP][AT FAIL] %s", (cmd != NULL) ? cmd : "(wait)");
    return 0;
}

static int ppp_wait_modem_ready(void)
{
    uint32_t i;
    uint32_t pass;
    const uint32_t baud_list[] = {
        AIR780E_PPP_UART_BOOT_BAUD,
        AIR780E_PPP_UART_DATA_BAUD,
        921600U,
        460800U,
        230400U,
        57600U,
        9600U,
    };

    for (pass = 0U; pass < (uint32_t)(sizeof(baud_list) / sizeof(baud_list[0])); pass++) {
        uint32_t prev;
        uint8_t duplicate = 0U;

        for (prev = 0U; prev < pass; prev++) {
            if (baud_list[prev] == baud_list[pass]) {
                duplicate = 1U;
                break;
            }
        }
        if (duplicate != 0U) {
            continue;
        }
        if (!ppp_uart_set_baud(baud_list[pass])) {
            continue;
        }

        for (i = 0U; i < 5U; i++) {
            if (ppp_send_cmd_wait("AT", "OK", NULL, 1000U)) {
                (void)ppp_send_cmd_wait("ATE0", "OK", NULL, PPP_AT_DEFAULT_TIMEOUT);
                (void)ppp_send_cmd_wait("AT+CMEE=2", "OK", NULL, PPP_AT_DEFAULT_TIMEOUT);
                return 1;
            }
            osDelay(AIR780E_PPP_AT_RETRY_DELAY_MS);
        }
    }

    return 0;
}

static int ppp_switch_module_baud(uint32_t baud)
{
#if AIR780E_PPP_UART_HIGH_BAUD_ENABLE
    char cmd[32];
    uint32_t i;

    if (s_ppp_uart_baud == baud) {
        return 1;
    }

    (void)snprintf(cmd, sizeof(cmd), "AT+IPR=%lu", (unsigned long)baud);
    if (!ppp_send_cmd_wait(cmd, "OK", NULL, 3000U)) {
        return 0;
    }

    osDelay(200U);
    if (!ppp_uart_set_baud(baud)) {
        return 0;
    }

    for (i = 0U; i < 5U; i++) {
        if (ppp_send_cmd_wait("AT", "OK", NULL, 1000U)) {
            return 1;
        }
        osDelay(300U);
    }

    return 0;
#else
    (void)baud;
    return 1;
#endif
}

static int ppp_wait_network_registered(void)
{
    uint32_t t0 = (uint32_t)osKernelGetTickCount();

    while (((uint32_t)osKernelGetTickCount() - t0) < AIR780E_PPP_REG_WAIT_MS) {
        if (ppp_send_cmd_wait("AT+CPIN?", "READY", NULL, 5000U)) {
            break;
        }
        ppp_log("[PPP] SIM not ready; waiting");
        osDelay(AIR780E_PPP_AT_RETRY_DELAY_MS);
    }

    if (((uint32_t)osKernelGetTickCount() - t0) >= AIR780E_PPP_REG_WAIT_MS) {
        ppp_log("[PPP] SIM not ready timeout");
        return 0;
    }

    while (((uint32_t)osKernelGetTickCount() - t0) < AIR780E_PPP_REG_WAIT_MS) {
        (void)ppp_send_cmd_wait("AT+CSQ", "OK", NULL, PPP_AT_DEFAULT_TIMEOUT);
        if (ppp_send_cmd_wait("AT+CGATT?", "+CGATT: 1", NULL, 5000U)) {
            return 1;
        }
        (void)ppp_send_cmd_wait("AT+CEREG?", ",1", ",5", PPP_AT_DEFAULT_TIMEOUT);
        (void)ppp_send_cmd_wait("AT+CGREG?", ",1", ",5", PPP_AT_DEFAULT_TIMEOUT);
        osDelay(AIR780E_PPP_AT_RETRY_DELAY_MS);
    }

    ppp_log("[PPP] network registration timeout");
    return 0;
}

static int ppp_configure_pdp(void)
{
    char cmd[96];

    (void)snprintf(cmd, sizeof(cmd), "AT+CGDCONT=1,\"IP\",\"%s\"", AIR780E_PPP_APN);
    return ppp_send_cmd_wait(cmd, "OK", NULL, 5000U);
}

static int ppp_dial_connect(void)
{
    char cmd[64];

    (void)snprintf(cmd, sizeof(cmd), "ATD%s", AIR780E_PPP_DIAL_NUMBER);
    ppp_logf("[PPP] dialing apn=%s", AIR780E_PPP_APN);
    return ppp_send_cmd_wait(cmd, "CONNECT", NULL, PPP_DIAL_TIMEOUT_MS);
}

#if SPEAKER_LOG_PPP_VERBOSE || SPEAKER_LOG_PPP_ONLINE_STATS
typedef struct {
    uint8_t phase;
    const char *name;
} T_PppPhaseName;

static const char *ppp_phase_name(uint8_t phase)
{
    static const T_PppPhaseName phase_names[] = {
        { PPP_PHASE_DEAD, "PPP_DEAD" },
        { PPP_PHASE_INITIALIZE, "PPP_INITIALIZE" },
        { PPP_PHASE_SERIALCONN, "PPP_SERIALCONN" },
        { PPP_PHASE_ESTABLISH, "PPP_ESTABLISH" },
        { PPP_PHASE_AUTHENTICATE, "PPP_AUTHENTICATE" },
        { PPP_PHASE_NETWORK, "PPP_NETWORK" },
        { PPP_PHASE_RUNNING, "PPP_RUNNING" },
        { PPP_PHASE_TERMINATE, "PPP_TERMINATE" },
        { PPP_PHASE_DISCONNECT, "PPP_DISCONNECT" }
    };
    uint16_t i;

    for (i = 0U; i < (uint16_t)(sizeof(phase_names) / sizeof(phase_names[0])); i++) {
        if (phase_names[i].phase == phase) {
            return phase_names[i].name;
        }
    }
    return "PPP_UNKNOWN";
}
#endif

static void ppp_format_ip4(const ip4_addr_t *addr, char *buf, size_t buf_size)
{
    if (buf == NULL || buf_size == 0U) {
        return;
    }
    if (addr == NULL) {
        (void)snprintf(buf, buf_size, "0.0.0.0");
        return;
    }
    (void)snprintf(buf, buf_size, "%lu.%lu.%lu.%lu",
                   (unsigned long)ip4_addr1(addr),
                   (unsigned long)ip4_addr2(addr),
                   (unsigned long)ip4_addr3(addr),
                   (unsigned long)ip4_addr4(addr));
}

static void ppp_lwip_stop(void)
{
    if (s_ppp_pcb != NULL) {
        uint32_t i;
        err_t err = ppp_close(s_ppp_pcb, 1U);
        if (err != ERR_OK) {
            ppp_logf("[PPP] ppp_close err=%d", (int)err);
        }
        for (i = 0U; i < 140U && s_ppp_pcb->phase != PPP_PHASE_DEAD; i++) {
            sys_check_timeouts();
            osDelay(100U);
        }
        if (s_ppp_pcb->phase == PPP_PHASE_DEAD) {
            (void)ppp_free(s_ppp_pcb);
        }
        s_ppp_pcb = NULL;
    }
    s_ppp_link_up = 0U;
    s_ppp_last_phase = 0xFFU;
}

static u32_t ppp_output_cb(ppp_pcb *pcb, const void *data, u32_t len, void *ctx)
{
    const uint8_t *p = (const uint8_t *)data;
    u32_t sent = 0U;

    (void)pcb;
    (void)ctx;

    while (sent < len) {
        u32_t remain = len - sent;
        uint16_t chunk = (remain > 512U) ? 512U : (uint16_t)remain;
        int written = UART_Write(PPP_UART_AT, p + sent, chunk);
        if (written > 0) {
            sent += (u32_t)written;
        } else {
            osDelay(1U);
        }
    }

    return sent;
}

static void ppp_phase_cb(ppp_pcb *pcb, uint8_t phase, void *ctx)
{
    (void)pcb;
    (void)ctx;
    if (s_ppp_last_phase == phase) {
        return;
    }
    s_ppp_last_phase = phase;
#if SPEAKER_LOG_PPP_VERBOSE
    ppp_logf("[PPP] phase=%s(%u)", ppp_phase_name(phase), (unsigned)phase);
#endif
}

static void ppp_link_status_cb(ppp_pcb *pcb, int err_code, void *ctx)
{
    struct netif *netif;
    const ip_addr_t *dns1;
    const ip_addr_t *dns2;
    char ip[24];
    char gw[24];
    char mask[24];
    char dns1_buf[24];
    char dns2_buf[24];

    (void)ctx;

    s_ppp_last_error = err_code;
    if (err_code != PPPERR_NONE) {
        s_ppp_link_up = 0U;
        ppp_logf("[PPP] link down err=%d", err_code);
        Air780ePpp_RequestReconnect("ppp-link-down");
        return;
    }

    netif = ppp_netif(pcb);
    ppp_format_ip4(netif_ip4_addr(netif), ip, sizeof(ip));
    ppp_format_ip4(netif_ip4_gw(netif), gw, sizeof(gw));
    ppp_format_ip4(netif_ip4_netmask(netif), mask, sizeof(mask));
    dns1 = dns_getserver(0);
    dns2 = dns_getserver(1);
    ppp_format_ip4((dns1 != NULL && IP_IS_V4(dns1)) ? ip_2_ip4(dns1) : NULL,
                   dns1_buf, sizeof(dns1_buf));
    ppp_format_ip4((dns2 != NULL && IP_IS_V4(dns2)) ? ip_2_ip4(dns2) : NULL,
                   dns2_buf, sizeof(dns2_buf));

    s_ppp_link_up = 1U;
    ppp_logf("[PPP] IPCP up ip=%s gw=%s mask=%s", ip, gw, mask);
    ppp_logf("[PPP] DNS dns1=%s dns2=%s", dns1_buf, dns2_buf);
}

static int ppp_lwip_start(void)
{
    err_t err;

    if (s_lwip_inited == 0U) {
        lwip_init();
        s_lwip_inited = 1U;
    }

    memset(&s_ppp_netif, 0, sizeof(s_ppp_netif));
    s_ppp_link_up = 0U;
    s_ppp_last_error = PPPERR_NONE;
    s_ppp_last_phase = 0xFFU;
    s_ppp_reconnect_requested = 0U;
    PppMqtt_Reset();
    s_mqtt_started = 0U;
    s_mqtt_announced = 0U;
    s_mqtt_fail_count = 0U;
    s_mqtt_retry_after_ms = 0U;

    s_ppp_pcb = pppos_create(&s_ppp_netif, ppp_output_cb, ppp_link_status_cb, NULL);
    if (s_ppp_pcb == NULL) {
        ppp_log("[PPP] pppos_create failed");
        return 0;
    }

    ppp_set_default(s_ppp_pcb);
    ppp_set_usepeerdns(s_ppp_pcb, 1);
    ppp_set_notify_phase_callback(s_ppp_pcb, ppp_phase_cb);

    err = ppp_connect(s_ppp_pcb, 0U);
    if (err != ERR_OK) {
        ppp_logf("[PPP] ppp_connect failed err=%d", (int)err);
        return 0;
    }

    return 1;
}

static void ppp_prepare_mqtt_identity(void)
{
    char topic[MQTT_TOPIC_BUF_LEN];

    MqttAppConfig_BuildDeviceId(s_mqtt_device_id, sizeof(s_mqtt_device_id),
                                HAL_GetUIDw0(), HAL_GetUIDw1(), HAL_GetUIDw2());
    (void)snprintf(s_lifecycle_topic, sizeof(s_lifecycle_topic),
                   MQTT_TOPIC_FMT_LIFECYCLE, s_mqtt_device_id);
    (void)snprintf(topic, sizeof(topic), "%s%s", MQTT_TOPIC_PREFIX, s_mqtt_device_id);
    MqttTask_SetTopic(topic);
}

static void ppp_start_local_mqtt_services(void)
{
    static uint8_t started;

    if (started != 0U) {
        return;
    }
    MqttCmdHandler_Init(s_mqtt_device_id);
    MqttTask_Start();
    started = 1U;
}

static void ppp_mqtt_service(void)
{
    if (s_ppp_link_up == 0U) {
        return;
    }

    PppMqtt_Service();

    if (s_mqtt_started == 2U) {
        if ((int32_t)((uint32_t)osKernelGetTickCount() - s_mqtt_retry_after_ms) >= 0) {
            s_mqtt_started = 0U;
            s_mqtt_announced = 0U;
        } else {
            return;
        }
    }

    if (s_mqtt_started == 0U) {
        ppp_prepare_mqtt_identity();
        ppp_start_local_mqtt_services();
        ppp_logf("[MQTT/PPP] start clientId=%s broker=%s:%u",
                 s_mqtt_device_id, AIR780E_MQTT_BROKER_HOST, (unsigned)AIR780E_MQTT_BROKER_PORT);
        if (PppMqtt_Start(AIR780E_MQTT_BROKER_HOST,
                          AIR780E_MQTT_BROKER_PORT,
                          s_mqtt_device_id,
                          AIR780E_MQTT_USERNAME,
                          AIR780E_MQTT_PASSWORD,
                          s_lifecycle_topic,
                          "offline",
                          1) == 0) {
            s_mqtt_started = 1U;
        } else {
            s_mqtt_started = 2U;
            s_mqtt_retry_after_ms = (uint32_t)osKernelGetTickCount() + AIR780E_PPP_MQTT_RETRY_DELAY_MS;
            if (s_mqtt_fail_count < 255U) {
                s_mqtt_fail_count++;
            }
            if (s_mqtt_fail_count >= AIR780E_PPP_MQTT_FAIL_RESET_MAX) {
                Air780ePpp_RequestReconnect("mqtt-start-failure-limit");
            }
            ppp_logf("[MQTT/PPP] start failed err=%d(%s)",
                     PppMqtt_GetLastError(), PppMqtt_GetLastErrorString());
        }
        return;
    }

    if (s_mqtt_started == 1U && PppMqtt_IsConnected() && s_mqtt_announced == 0U) {
        char onlineMsg[64];

        s_mqtt_announced = 1U;
        s_mqtt_fail_count = 0U;
        (void)snprintf(onlineMsg, sizeof(onlineMsg), "{\"v\":1,\"type\":\"online\",\"ts\":%lu}",
                       (unsigned long)osKernelGetTickCount());
        ppp_log("[MQTT/PPP] online");
        MqttCmdHandler_OnTcpConnected();
        (void)PppMqtt_Publish(s_lifecycle_topic, onlineMsg, 1);
        MqttStatePublisher_Start(s_mqtt_device_id);
        return;
    }

    if (s_mqtt_started == 1U && PppMqtt_IsFailed()) {
        ppp_logf("[MQTT/PPP] failed err=%d connack=%d (%s)",
                 PppMqtt_GetLastError(),
                 PppMqtt_GetLastConnackRc(),
                 PppMqtt_GetLastErrorString());
        PppMqtt_Reset();
        if (s_mqtt_fail_count < 255U) {
            s_mqtt_fail_count++;
        }
        if (s_mqtt_fail_count >= AIR780E_PPP_MQTT_FAIL_RESET_MAX) {
            Air780ePpp_RequestReconnect("mqtt-runtime-failure-limit");
        }
        s_mqtt_started = 2U;
        s_mqtt_announced = 0U;
        s_mqtt_retry_after_ms = (uint32_t)osKernelGetTickCount() + AIR780E_PPP_MQTT_RETRY_DELAY_MS;
    }
}

static void ppp_session_cleanup_for_reconnect(const char *reason)
{
    ppp_logf("[PPP] cleanup session reason=%s", (reason != NULL) ? reason : "unknown");
    PppMqtt_Reset();
    s_mqtt_started = 0U;
    s_mqtt_announced = 0U;
    ppp_lwip_stop();
    ppp_uart_drain(200U);
    s_ppp_reconnect_requested = 0U;
}

static void ppp_check_uart_health(const T_UartBufferState *rx_state,
                                  uint32_t rx_bytes,
                                  uint32_t *last_lost_rx,
                                  uint32_t *last_line_err,
                                  uint8_t *line_err_streak)
{
    uint32_t line_err = UART_GetUart7LineErrorClears();
    uint32_t line_err_delta = line_err - *last_line_err;
    T_UartDmaPerfStats dma_stats;
    uint32_t avg_drain = 0U;

    memset(&dma_stats, 0, sizeof(dma_stats));
    UART_GetUart7DmaPerfStats(&dma_stats);
    if (dma_stats.drainCount > 0U) {
        avg_drain = dma_stats.drainBytes / dma_stats.drainCount;
    }

#if SPEAKER_LOG_PPP_ONLINE_STATS
    ppp_logf("[PPP] data_mode link=%u phase=%s err=%d rx=%lu lost_rx=%lu max_rx=%u line_err=%lu heap=%lu stack=%lu",
             (unsigned)s_ppp_link_up,
             ppp_phase_name(s_ppp_last_phase),
             s_ppp_last_error,
             (unsigned long)rx_bytes,
             (unsigned long)rx_state->countOfLostData,
             (unsigned)rx_state->maxUsedCapacityOfBuffer,
             (unsigned long)line_err,
             (unsigned long)xPortGetFreeHeapSize(),
             (unsigned long)uxTaskGetStackHighWaterMark(NULL));

    ppp_logf("[PPP] uart_dma active=%u dma_irq=%lu idle=%lu drain=%lu bytes=%lu avg=%lu max=%u dma_err=%lu",
             (unsigned)dma_stats.rxDmaActive,
             (unsigned long)dma_stats.dmaIrqCount,
             (unsigned long)dma_stats.idleIrqCount,
             (unsigned long)dma_stats.drainCount,
             (unsigned long)dma_stats.drainBytes,
             (unsigned long)avg_drain,
             (unsigned)dma_stats.maxDrainBytes,
             (unsigned long)dma_stats.dmaErrorCount);
#else
    (void)rx_bytes;
    (void)avg_drain;
#endif

    if (rx_state->countOfLostData != *last_lost_rx) {
        Air780ePpp_RequestReconnect("uart-rx-lost");
    }

    if (line_err_delta >= PPP_UART_LINE_ERR_WARN_DELTA) {
        if (*line_err_streak < 255U) {
            (*line_err_streak)++;
        }
        if (*line_err_streak >= PPP_UART_LINE_ERR_RESET_STREAK) {
            Air780ePpp_RequestReconnect("uart-line-error");
        }
    } else {
        *line_err_streak = 0U;
    }

    *last_lost_rx = rx_state->countOfLostData;
    *last_line_err = line_err;
}

static void ppp_connected_idle_loop(void)
{
    T_UartBufferState rx_state;
    T_UartBufferState tx_state;
    uint8_t tmp[PPP_AT_READ_CHUNK_SIZE];
    uint32_t negotiate_start;
    uint32_t last_lost_rx;
    uint32_t last_line_err;
    uint8_t line_err_streak = 0U;

    ppp_log("[PPP] CONNECT");

    if (!ppp_lwip_start()) {
        ppp_session_cleanup_for_reconnect("pppos-start-failed");
        return;
    }

    negotiate_start = (uint32_t)osKernelGetTickCount();
    memset(&rx_state, 0, sizeof(rx_state));
    memset(&tx_state, 0, sizeof(tx_state));
    UART_GetBufferState(PPP_UART_AT, &rx_state, &tx_state);
    last_lost_rx = rx_state.countOfLostData;
    last_line_err = UART_GetUart7LineErrorClears();

    for (;;) {
        uint32_t t0 = (uint32_t)osKernelGetTickCount();
        uint32_t rx_bytes = 0U;

        while (((uint32_t)osKernelGetTickCount() - t0) < PPP_ONLINE_STATS_MS) {
            int r = UART_Read(PPP_UART_AT, tmp, (uint16_t)sizeof(tmp));
            if (r > 0) {
                rx_bytes += (uint32_t)r;
                pppos_input(s_ppp_pcb, tmp, r);
            } else {
                osDelay(PPP_AT_POLL_MS);
            }
            sys_check_timeouts();
            SolarCleanOta_Service();
            ppp_mqtt_service();

            if (!Ds800Protocol_GetFourGEnabled()) {
                Air780ePpp_RequestReconnect("4g-disabled");
            }
            if (s_ppp_reconnect_requested != 0U) {
                ppp_session_cleanup_for_reconnect("reconnect-request");
                return;
            }
        }

        memset(&rx_state, 0, sizeof(rx_state));
        memset(&tx_state, 0, sizeof(tx_state));
        UART_GetBufferState(PPP_UART_AT, &rx_state, &tx_state);
        ppp_check_uart_health(&rx_state, rx_bytes, &last_lost_rx,
                              &last_line_err, &line_err_streak);

        if (s_ppp_reconnect_requested != 0U) {
            ppp_session_cleanup_for_reconnect("reconnect-request");
            return;
        }

        if (s_ppp_link_up == 0U &&
            ppp_timeout_expired(negotiate_start, PPP_NEGOTIATE_TIMEOUT_MS)) {
            ppp_log("[PPP] negotiate timeout");
            ppp_session_cleanup_for_reconnect("negotiate-timeout");
            return;
        }
    }
}

void Air780ePpp_Run(void)
{
    uint32_t attempt = 0U;
    uint8_t last_four_g_enabled = 0xFFU;

    UART_Init(DJI_CONSOLE_UART_NUM, DJI_CONSOLE_UART_BAUD);
    UART_Init(PPP_UART_AT, AIR780E_PPP_UART_BOOT_BAUD);
    Air780ePower_Init();
    Air780ePower_Off();
    SolarCleanOta_Init();
    ppp_prepare_mqtt_identity();

    ppp_log("[PPP] Air780E PPP/MQTT task start");

    for (;;) {
        if (!Ds800Protocol_GetFourGEnabled()) {
            if (last_four_g_enabled != 0U) {
                ppp_log("[PPP] 4G disabled by param, Air780E power off");
                last_four_g_enabled = 0U;
            }
            Air780ePower_Off();
            osDelay(1000U);
            continue;
        }
        if (last_four_g_enabled != 1U) {
            ppp_log("[PPP] 4G enabled by param, start modem workflow");
            last_four_g_enabled = 1U;
        }

        attempt++;
        ppp_logf("[PPP] attempt=%lu power on Air780E", (unsigned long)attempt);
        Air780ePower_Init();
        Air780ePower_Off();
        osDelay(PPP_POWER_CYCLE_OFF_MS);
        Air780ePower_On();
        osDelay(AIR780E_PPP_POWER_ON_DELAY_MS);
        ppp_uart_drain(200U);

        if (!Ds800Protocol_GetFourGEnabled()) {
            Air780ePower_Off();
            osDelay(PPP_RETRY_PAUSE_MS);
            continue;
        }

        if (!ppp_wait_modem_ready()) {
            ppp_log("[PPP] AT sync failed");
            Air780ePower_Off();
            osDelay(PPP_RETRY_PAUSE_MS);
            continue;
        }
        ppp_log("[PPP] AT sync OK");

        if (!ppp_switch_module_baud(AIR780E_PPP_UART_DATA_BAUD)) {
            ppp_log("[PPP] baud switch failed");
            Air780ePower_Off();
            osDelay(PPP_RETRY_PAUSE_MS);
            continue;
        }

        if (!ppp_wait_network_registered()) {
            ppp_log("[PPP] network registration failed");
            Air780ePower_Off();
            osDelay(PPP_RETRY_PAUSE_MS);
            continue;
        }
        ppp_log("[PPP] network registration OK");

        if (!ppp_configure_pdp()) {
            ppp_log("[PPP] PDP config failed");
            Air780ePower_Off();
            osDelay(PPP_RETRY_PAUSE_MS);
            continue;
        }
        ppp_log("[PPP] PDP config OK");

        if (ppp_dial_connect()) {
            ppp_connected_idle_loop();
            ppp_log("[PPP] session ended; power cycle before retry");
            Air780ePower_Off();
            osDelay(PPP_RETRY_PAUSE_MS);
            continue;
        }

        ppp_log("[PPP] dial failed, retry later");
        Air780ePower_Off();
        osDelay(PPP_RETRY_PAUSE_MS);
    }
}
