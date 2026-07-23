#include "solarclean_ota.h"

#include "dji_sdk_config.h"
#include "air780e_mqtt.h"
#include "pwm_swing.h"
#include "solarclean_ota_flash.h"
#include "solarclean_ota_hash.h"
#include "solarclean_ota_state.h"

#include "FreeRTOS.h"
#include "task.h"
#include "uart.h"
#include "stm32h7xx_hal.h"

#include "lwip/dns.h"
#include "lwip/ip_addr.h"
#include "lwip/pbuf.h"
#include "lwip/tcp.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SOLARCLEAN_OTA_PROGRESS_STEP      5U
#define SOLARCLEAN_OTA_HTTP_HEADER_MAX    1024U
#define SOLARCLEAN_OTA_HTTP_TIMEOUT_MS    120000U
#define SOLARCLEAN_OTA_RESET_DELAY_MS     2000U

typedef struct {
    E_SolarCleanOtaState state;
    E_SolarCleanOtaFailReason failReason;
    const char *lastResult;
    char targetVersion[48];
    uint32_t targetInnerVersion;
    unsigned progress;
} T_SolarCleanOtaContext;

typedef struct {
    T_SolarCleanOtaFlashWriter writer;
    uint32_t expectedTotal;
    SolarCleanOta_PublishStatus publish;
    void *publishUser;
    unsigned lastPublishedProgress;
    E_SolarCleanOtaSlot targetSlot;
} T_SolarCleanOtaDownloadContext;

typedef enum {
    SOLARCLEAN_OTA_HTTP_IDLE = 0,
    SOLARCLEAN_OTA_HTTP_ERASING,
    SOLARCLEAN_OTA_HTTP_DNS,
    SOLARCLEAN_OTA_HTTP_CONNECTING,
    SOLARCLEAN_OTA_HTTP_RECV,
    SOLARCLEAN_OTA_HTTP_DONE,
    SOLARCLEAN_OTA_HTTP_FAILED,
} E_SolarCleanOtaHttpState;

typedef struct {
    E_SolarCleanOtaHttpState state;
    struct tcp_pcb *pcb;
    ip_addr_t serverIp;
    T_SolarCleanOtaStartRequest req;
    T_SolarCleanOtaStartRequest pendingReq;
    SolarCleanOta_PublishStatus publish;
    SolarCleanOta_PublishStatus pendingPublish;
    void *publishUser;
    void *pendingPublishUser;
    T_SolarCleanOtaDownloadContext download;
    char host[80];
    char uri[256];
    char header[SOLARCLEAN_OTA_HTTP_HEADER_MAX + 1U];
    uint16_t port;
    uint16_t headerLen;
    uint16_t statusCode;
    uint8_t headerDone;
    uint8_t pendingValid;
    uint8_t active;
    uint8_t resetScheduled;
    uint32_t contentLength;
    uint32_t downloadedBytes;
    uint32_t startedMs;
    uint32_t resetAtMs;
} T_SolarCleanOtaHttpContext;

static T_SolarCleanOtaContext s_ota = {
    SOLARCLEAN_OTA_STATE_IDLE,
    SOLARCLEAN_OTA_FAIL_NONE,
    "NONE",
    "",
    0U,
    0U,
};

static T_SolarCleanOtaHttpContext s_http;

static void ota_publish_status(SolarCleanOta_PublishStatus publish, void *user,
                               const char *status, unsigned progress,
                               const char *targetVersion);
static const char *ota_slot_to_string(E_SolarCleanOtaSlot slot);

static void ota_log(const char *msg)
{
    if (msg == NULL)
        return;
    UART_Write(UART_NUM_1, (const uint8_t *)msg, (uint16_t)strlen(msg));
    UART_Write(UART_NUM_1, (const uint8_t *)"\r\n", 2U);
}

static void ota_logf(const char *fmt, ...)
{
    char buf[192];
    va_list ap;
    int n;

    va_start(ap, fmt);
    n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n <= 0)
        return;
    if (n >= (int)sizeof(buf))
        n = (int)sizeof(buf) - 1;
    UART_Write(UART_NUM_1, (const uint8_t *)buf, (uint16_t)n);
    UART_Write(UART_NUM_1, (const uint8_t *)"\r\n", 2U);
}

static void ota_log_slot_probe(const char *tag, E_SolarCleanOtaSlot slot)
{
    uint32_t base = SolarCleanOtaFlash_GetSlotBase(slot);
    uint32_t word0;
    uint32_t word1;

    if (base == 0U)
        return;

    SCB_InvalidateDCache_by_Addr((uint32_t *)base, 32);
    __DSB();
    __ISB();
    word0 = *(volatile uint32_t *)base;
    word1 = *(volatile uint32_t *)(base + 4U);
    ota_logf("[OTA] probe %s slot=%s base=0x%08lX w0=0x%08lX w1=0x%08lX",
             tag,
             ota_slot_to_string(slot),
             (unsigned long)base,
             (unsigned long)word0,
             (unsigned long)word1);
}

static const char *ota_state_to_string(E_SolarCleanOtaState state)
{
    switch (state) {
    case SOLARCLEAN_OTA_STATE_IDLE:
        return "IDLE";
    case SOLARCLEAN_OTA_STATE_VALIDATING:
        return "VALIDATING";
    case SOLARCLEAN_OTA_STATE_DOWNLOADING:
        return "DOWNLOADING";
    case SOLARCLEAN_OTA_STATE_VERIFYING:
        return "VERIFYING";
    case SOLARCLEAN_OTA_STATE_WRITING:
        return "WRITING";
    case SOLARCLEAN_OTA_STATE_PENDING_REBOOT:
        return "PENDING_REBOOT";
    case SOLARCLEAN_OTA_STATE_FAILED:
        return "FAILED";
    default:
        return "UNKNOWN";
    }
}

static const char *ota_fail_to_string(E_SolarCleanOtaFailReason reason)
{
    switch (reason) {
    case SOLARCLEAN_OTA_FAIL_NONE:
        return "NONE";
    case SOLARCLEAN_OTA_FAIL_BAD_REQUEST:
        return "BAD_REQUEST";
    case SOLARCLEAN_OTA_FAIL_HW_NOT_MATCH:
        return "HW_NOT_MATCH";
    case SOLARCLEAN_OTA_FAIL_DEVICE_BUSY:
        return "DEVICE_BUSY";
    case SOLARCLEAN_OTA_FAIL_IMAGE_TOO_LARGE:
        return "IMAGE_TOO_LARGE";
    case SOLARCLEAN_OTA_FAIL_BAD_HASH:
        return "BAD_HASH";
    case SOLARCLEAN_OTA_FAIL_HASH_VERIFY_FAILED:
        return "HASH_VERIFY_FAILED";
    case SOLARCLEAN_OTA_FAIL_DOWNLOAD_FAILED:
        return "DOWNLOAD_FAILED";
    case SOLARCLEAN_OTA_FAIL_DOWNLOAD_NOT_READY:
        return "DOWNLOAD_NOT_READY";
    case SOLARCLEAN_OTA_FAIL_FLASH_NOT_READY:
        return "FLASH_NOT_READY";
    case SOLARCLEAN_OTA_FAIL_STATE_WRITE_FAILED:
        return "STATE_WRITE_FAILED";
    default:
        return "UNKNOWN";
    }
}

static void ota_enter_state(E_SolarCleanOtaState state, unsigned progress)
{
    E_SolarCleanOtaState oldState = s_ota.state;

    s_ota.state = state;
    s_ota.progress = progress;
    if (state != SOLARCLEAN_OTA_STATE_FAILED) {
        s_ota.failReason = SOLARCLEAN_OTA_FAIL_NONE;
        if (state == SOLARCLEAN_OTA_STATE_PENDING_REBOOT)
            s_ota.lastResult = "PENDING_VERIFY";
        else
            s_ota.lastResult = "NONE";
    }
    ota_logf("[OTA] state %s -> %s progress=%u",
             ota_state_to_string(oldState), ota_state_to_string(state), progress);
}

static void ota_fail(E_SolarCleanOtaFailReason reason)
{
    s_ota.state = SOLARCLEAN_OTA_STATE_FAILED;
    s_ota.failReason = reason;
    s_ota.lastResult = "FAILED";
    ota_logf("[OTA] FAIL reason=%s", ota_fail_to_string(reason));
}

static unsigned long ota_now_ms(void)
{
    return (unsigned long)xTaskGetTickCount() * (unsigned long)portTICK_PERIOD_MS;
}

static const char *ota_slot_to_string(E_SolarCleanOtaSlot slot)
{
    return (slot == SOLARCLEAN_OTA_SLOT_B) ? "B" : "A";
}

static E_SolarCleanOtaSlot ota_build_slot(void)
{
    return SOLARCLEAN_OTA_SLOT_A;
}

static E_SolarCleanOtaSlot ota_current_slot(void)
{
    E_SolarCleanOtaSlot slot;

    if (SolarCleanOtaFlash_GetSlotByAddress(SCB->VTOR, &slot) == SOLARCLEAN_OTA_FLASH_OK)
        return slot;

    return ota_build_slot();
}

static E_SolarCleanOtaSlot ota_staging_slot(void)
{
    return SOLARCLEAN_OTA_SLOT_B;
}

static void ota_set_ack(T_SolarCleanOtaAck *ack, int ok, int code, const char *msg)
{
    if (ack == NULL)
        return;
    ack->ok = ok;
    ack->code = code;
    if (msg == NULL)
        msg = "";
    (void)snprintf(ack->msg, sizeof(ack->msg), "%s", msg);
}

static int ota_http_body_to_flash(const uint8_t *data, uint32_t len, void *ctx)
{
    T_SolarCleanOtaDownloadContext *download = (T_SolarCleanOtaDownloadContext *)ctx;
    int result;

    if (download == NULL)
        return -1;

    result = SolarCleanOtaFlash_Write(&download->writer, data, len);
    if (result != SOLARCLEAN_OTA_FLASH_OK) {
        ota_logf("[OTA] flash write failed rc=%d written=%lu incoming=%lu",
                 result,
                 (unsigned long)download->writer.writtenSize,
                 (unsigned long)len);
        return -1;
    }
    return 0;
}

static int ota_http_begin_flash(uint32_t total, void *ctx)
{
    T_SolarCleanOtaDownloadContext *download = (T_SolarCleanOtaDownloadContext *)ctx;

    if (download == NULL)
        return -1;

    ota_logf("[OTA] flash begin slot=%s base=0x%08lX end=0x%08lX total=%lu slotSize=%lu",
             ota_slot_to_string(download->targetSlot),
             (unsigned long)SolarCleanOtaFlash_GetSlotBase(download->targetSlot),
             (unsigned long)SolarCleanOtaFlash_GetSlotEnd(download->targetSlot),
             (unsigned long)total,
             (unsigned long)SolarCleanOtaFlash_GetSlotSize(download->targetSlot));
    if (total == 0U || total > SolarCleanOtaFlash_GetSlotSize(download->targetSlot)) {
        ota_log("[OTA] flash begin rejected");
        return -1;
    }
    download->expectedTotal = total;
    if (SolarCleanOtaFlash_Begin(&download->writer, download->targetSlot, total) != SOLARCLEAN_OTA_FLASH_OK) {
        ota_log("[OTA] flash erase/begin failed");
        return -1;
    }
    ota_log("[OTA] flash begin OK");
    return 0;
}

static void ota_publish_status(SolarCleanOta_PublishStatus publish, void *user,
                               const char *status, unsigned progress,
                               const char *targetVersion)
{
    char payload[512];

    if (publish == NULL)
        return;
    if (status == NULL)
        status = SolarCleanOta_GetStatus();
    if (targetVersion == NULL)
        targetVersion = "";

    (void)snprintf(payload, sizeof(payload),
                   "{\"v\":1,\"type\":\"otaStatus\",\"ts\":%lu,\"ota_status\":\"%s\","
                   "\"progress\":%u,\"slot\":\"%s\",\"next_slot\":\"%s\","
                   "\"target_version\":\"%s\",\"target_inner_version\":%lu,"
                   "\"firmware_version\":\"%s\",\"inner_version\":%lu,"
                   "\"last_result\":\"%s\",\"fail_reason\":\"%s\",\"code\":%u,\"msg\":\"%s\"}",
                   ota_now_ms(), status, progress,
                   ota_slot_to_string(ota_current_slot()),
                   ota_slot_to_string(ota_staging_slot()),
                   targetVersion, (unsigned long)s_ota.targetInnerVersion,
                   SolarCleanOta_GetFirmwareVersion(), (unsigned long)SolarCleanOta_GetInnerVersion(),
                   SolarCleanOta_GetLastResult(), SolarCleanOta_GetLastFailReason(),
                   (unsigned)s_ota.failReason, SolarCleanOta_GetLastFailReason());
    publish(payload, user);
}

static int ota_parse_http_url(const char *url, char *host, size_t hostSize,
                              uint16_t *port, char *uri, size_t uriSize)
{
    const char *p = url;
    const char *slash;
    const char *colon;
    size_t hostLen;
    size_t uriLen;

    if (url == NULL || host == NULL || port == NULL || uri == NULL) {
        return -1;
    }
    *port = 80U;
    if (strncmp(p, "http://", 7U) == 0) {
        p += 7U;
    } else if (strncmp(p, "https://", 8U) == 0) {
        p += 8U;
    }
    slash = strchr(p, '/');
    if (slash == NULL) {
        return -1;
    }
    colon = memchr(p, ':', (size_t)(slash - p));
    hostLen = (colon != NULL) ? (size_t)(colon - p) : (size_t)(slash - p);
    uriLen = strlen(slash);
    if (hostLen == 0U || hostLen >= hostSize || uriLen == 0U || uriLen >= uriSize) {
        return -1;
    }
    memcpy(host, p, hostLen);
    host[hostLen] = '\0';
    memcpy(uri, slash, uriLen);
    uri[uriLen] = '\0';
    if (colon != NULL) {
        unsigned long parsed = strtoul(colon + 1, NULL, 10);
        if (parsed == 0UL || parsed > 65535UL) {
            return -1;
        }
        *port = (uint16_t)parsed;
    }
    return 0;
}

static int ota_header_status(const char *header)
{
    const char *p;

    if (header == NULL || strncmp(header, "HTTP/", 5U) != 0) {
        return 0;
    }
    p = strchr(header, ' ');
    if (p == NULL) {
        return 0;
    }
    return atoi(p + 1);
}

static uint32_t ota_header_content_length(const char *header)
{
    const char *p = strstr(header, "Content-Length:");
    uint32_t v = 0U;

    if (p == NULL) {
        return 0U;
    }
    p += 15;
    while (*p == ' ' || *p == '\t') {
        p++;
    }
    while (*p >= '0' && *p <= '9') {
        v = v * 10U + (uint32_t)(*p - '0');
        p++;
    }
    return v;
}

static void ota_http_cleanup_tcp(void)
{
    if (s_http.pcb != NULL) {
        tcp_arg(s_http.pcb, NULL);
        tcp_recv(s_http.pcb, NULL);
        tcp_poll(s_http.pcb, NULL, 0U);
        tcp_err(s_http.pcb, NULL);
        tcp_abort(s_http.pcb);
        s_http.pcb = NULL;
    }
}

static void ota_http_fail(E_SolarCleanOtaFailReason reason, const char *msg)
{
    ota_http_cleanup_tcp();
    SolarCleanOtaFlash_Abort(&s_http.download.writer);
    s_http.state = SOLARCLEAN_OTA_HTTP_FAILED;
    s_http.active = 0U;
    Air780eMqtt_SetHttpDownloadActive(0U);
    ota_fail(reason);
    ota_logf("[OTA] PPP HTTP fail reason=%s msg=%s",
             ota_fail_to_string(reason), (msg != NULL) ? msg : "");
    ota_publish_status(s_http.publish,
                       s_http.publishUser,
                       SolarCleanOta_GetStatus(),
                       s_ota.progress,
                       s_ota.targetVersion);
}

static int ota_http_consume_body(const uint8_t *data, uint32_t len)
{
    unsigned progress;

    if (len == 0U) {
        return 0;
    }
    if (ota_http_body_to_flash(data, len, &s_http.download) != 0) {
        ota_http_fail(SOLARCLEAN_OTA_FAIL_DOWNLOAD_FAILED, "flash write failed");
        return -1;
    }
    s_http.downloadedBytes += len;
    progress = (unsigned)((s_http.downloadedBytes * 80U) / s_http.req.fileSize);
    if (progress > 80U) {
        progress = 80U;
    }
    if (progress == 80U || progress >= s_http.download.lastPublishedProgress + SOLARCLEAN_OTA_PROGRESS_STEP) {
        s_http.download.lastPublishedProgress = progress;
        s_ota.progress = progress;
        ota_publish_status(s_http.publish, s_http.publishUser,
                           SolarCleanOta_GetStatus(), progress, s_ota.targetVersion);
    }
    return 0;
}

static int ota_http_consume(const uint8_t *data, uint32_t len)
{
    uint32_t i = 0U;

    while (i < len) {
        if (s_http.headerDone == 0U) {
            if (s_http.headerLen >= SOLARCLEAN_OTA_HTTP_HEADER_MAX) {
                ota_http_fail(SOLARCLEAN_OTA_FAIL_DOWNLOAD_FAILED, "http header too large");
                return -1;
            }
            s_http.header[s_http.headerLen++] = (char)data[i++];
            s_http.header[s_http.headerLen] = '\0';
            if (s_http.headerLen >= 4U &&
                memcmp(&s_http.header[s_http.headerLen - 4U], "\r\n\r\n", 4U) == 0) {
                s_http.statusCode = (uint16_t)ota_header_status(s_http.header);
                s_http.contentLength = ota_header_content_length(s_http.header);
                ota_logf("[OTA] http header status=%u len=%lu",
                         (unsigned)s_http.statusCode,
                         (unsigned long)s_http.contentLength);
                if (strstr(s_http.header, "Transfer-Encoding: chunked") != NULL) {
                    ota_http_fail(SOLARCLEAN_OTA_FAIL_DOWNLOAD_FAILED, "chunked not supported");
                    return -1;
                }
                if (s_http.statusCode != 200U ||
                    s_http.contentLength != s_http.req.fileSize) {
                    ota_http_fail(SOLARCLEAN_OTA_FAIL_DOWNLOAD_FAILED, "http size/status mismatch");
                    return -1;
                }
                s_http.headerDone = 1U;
            }
        } else {
            return ota_http_consume_body(&data[i], len - i);
        }
    }
    return 0;
}

static err_t ota_http_recv_cb(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err)
{
    struct pbuf *q;

    (void)arg;
    if (err != ERR_OK) {
        ota_http_fail(SOLARCLEAN_OTA_FAIL_DOWNLOAD_FAILED, "http recv error");
        return ERR_OK;
    }
    if (p == NULL) {
        T_SolarCleanOtaAck ack;
        int rc;

        tcp_arg(tpcb, NULL);
        tcp_recv(tpcb, NULL);
        tcp_poll(tpcb, NULL, 0U);
        tcp_err(tpcb, NULL);
        if (s_http.pcb == tpcb) {
            s_http.pcb = NULL;
        }
        if (tcp_close(tpcb) != ERR_OK) {
            tcp_abort(tpcb);
            ota_http_fail(SOLARCLEAN_OTA_FAIL_DOWNLOAD_FAILED, "http close failed");
            return ERR_ABRT;
        }
        if (s_http.downloadedBytes != s_http.req.fileSize) {
            ota_http_fail(SOLARCLEAN_OTA_FAIL_DOWNLOAD_FAILED, "download size mismatch");
            return ERR_OK;
        }
        if (SolarCleanOtaFlash_End(&s_http.download.writer) != SOLARCLEAN_OTA_FLASH_OK) {
            ota_http_fail(SOLARCLEAN_OTA_FAIL_FLASH_NOT_READY, "flash finalize failed");
            return ERR_OK;
        }
        ota_log_slot_probe("after-write", s_http.download.targetSlot);
        memset(&ack, 0, sizeof(ack));
        rc = SolarCleanOta_CommitNextSlotForReboot(&s_http.req, &ack,
                                                   s_http.publish,
                                                   s_http.publishUser);
        if (rc == 0) {
            s_http.state = SOLARCLEAN_OTA_HTTP_DONE;
            s_http.active = 0U;
            Air780eMqtt_SetHttpDownloadActive(0U);
            s_http.resetScheduled = 1U;
            s_http.resetAtMs = (uint32_t)xTaskGetTickCount() + pdMS_TO_TICKS(SOLARCLEAN_OTA_RESET_DELAY_MS);
            ota_log("[OTA] PPP HTTP download OK, reset scheduled");
        } else {
            s_http.active = 0U;
            Air780eMqtt_SetHttpDownloadActive(0U);
        }
        return ERR_OK;
    }

    tcp_recved(tpcb, p->tot_len);
    for (q = p; q != NULL; q = q->next) {
        if (ota_http_consume((const uint8_t *)q->payload, q->len) != 0) {
            break;
        }
    }
    pbuf_free(p);
    return ERR_OK;
}

static err_t ota_http_connected_cb(void *arg, struct tcp_pcb *tpcb, err_t err)
{
    char req[512];
    int n;

    (void)arg;
    if (err != ERR_OK) {
        ota_http_fail(SOLARCLEAN_OTA_FAIL_DOWNLOAD_FAILED, "http connect failed");
        return ERR_OK;
    }
    s_http.state = SOLARCLEAN_OTA_HTTP_RECV;
    tcp_recv(tpcb, ota_http_recv_cb);
    n = snprintf(req, sizeof(req),
                 "GET %s HTTP/1.1\r\n"
                 "Host: %s:%u\r\n"
                 "User-Agent: SolarClean-OTA\r\n"
                 "Connection: close\r\n\r\n",
                 s_http.uri,
                 s_http.host,
                 (unsigned)s_http.port);
    if (n <= 0 || n >= (int)sizeof(req) ||
        tcp_write(tpcb, req, (u16_t)n, TCP_WRITE_FLAG_COPY) != ERR_OK ||
        tcp_output(tpcb) != ERR_OK) {
        ota_http_fail(SOLARCLEAN_OTA_FAIL_DOWNLOAD_FAILED, "http request failed");
    }
    return ERR_OK;
}

static void ota_http_err_cb(void *arg, err_t err)
{
    (void)arg;
    (void)err;
    s_http.pcb = NULL;
    ota_http_fail(SOLARCLEAN_OTA_FAIL_DOWNLOAD_FAILED, "http tcp error");
}

static void ota_http_dns_cb(const char *name, const ip_addr_t *addr, void *arg)
{
    err_t err;

    (void)name;
    (void)arg;
    if (addr == NULL || !IP_IS_V4(addr)) {
        ota_http_fail(SOLARCLEAN_OTA_FAIL_DOWNLOAD_FAILED, "dns failed");
        return;
    }
    s_http.serverIp = *addr;
    s_http.pcb = tcp_new();
    if (s_http.pcb == NULL) {
        ota_http_fail(SOLARCLEAN_OTA_FAIL_DOWNLOAD_FAILED, "tcp alloc failed");
        return;
    }
    tcp_setprio(s_http.pcb, TCP_PRIO_MAX);
    tcp_arg(s_http.pcb, &s_http);
    tcp_err(s_http.pcb, ota_http_err_cb);
    s_http.state = SOLARCLEAN_OTA_HTTP_CONNECTING;
    err = tcp_connect(s_http.pcb, &s_http.serverIp, s_http.port, ota_http_connected_cb);
    if (err != ERR_OK) {
        ota_http_fail(SOLARCLEAN_OTA_FAIL_DOWNLOAD_FAILED, "tcp connect start failed");
    }
}

static int ota_http_start_now(const T_SolarCleanOtaStartRequest *req,
                              SolarCleanOta_PublishStatus publish,
                              void *publishUser)
{
    err_t err;
    ip_addr_t addr;

    if (req == NULL) {
        return -1;
    }
    memset(&s_http.download, 0, sizeof(s_http.download));
    s_http.req = *req;
    s_http.publish = publish;
    s_http.publishUser = publishUser;
    s_http.download.expectedTotal = req->fileSize;
    s_http.download.publish = publish;
    s_http.download.publishUser = publishUser;
    s_http.download.targetSlot = ota_staging_slot();
    s_http.downloadedBytes = 0U;
    s_http.headerLen = 0U;
    s_http.headerDone = 0U;
    s_http.statusCode = 0U;
    s_http.contentLength = 0U;
    s_http.startedMs = (uint32_t)xTaskGetTickCount();
    s_http.active = 1U;
    Air780eMqtt_SetHttpDownloadActive(1U);
    s_http.state = SOLARCLEAN_OTA_HTTP_ERASING;

    if (ota_parse_http_url(req->downloadUrl, s_http.host, sizeof(s_http.host),
                           &s_http.port, s_http.uri, sizeof(s_http.uri)) != 0) {
        ota_http_fail(SOLARCLEAN_OTA_FAIL_BAD_REQUEST, "bad ota url");
        return -1;
    }
    ota_logf("[OTA] PPP HTTP start host=%s port=%u uri=%s size=%lu",
             s_http.host, (unsigned)s_http.port, s_http.uri, (unsigned long)req->fileSize);
    ota_enter_state(SOLARCLEAN_OTA_STATE_WRITING, 0U);
    ota_publish_status(publish, publishUser, SolarCleanOta_GetStatus(), 0U, s_ota.targetVersion);
    if (ota_http_begin_flash(req->fileSize, &s_http.download) != 0) {
        ota_http_fail(SOLARCLEAN_OTA_FAIL_FLASH_NOT_READY, "flash begin failed");
        return -1;
    }
    ota_enter_state(SOLARCLEAN_OTA_STATE_DOWNLOADING, 0U);
    ota_publish_status(publish, publishUser, SolarCleanOta_GetStatus(), 0U, s_ota.targetVersion);
    s_http.state = SOLARCLEAN_OTA_HTTP_DNS;
    memset(&addr, 0, sizeof(addr));
    if (ipaddr_aton(s_http.host, &addr) != 0) {
        ota_http_dns_cb(s_http.host, &addr, NULL);
        return 0;
    }
    err = dns_gethostbyname(s_http.host, &addr, ota_http_dns_cb, NULL);
    if (err == ERR_OK) {
        ota_http_dns_cb(s_http.host, &addr, NULL);
    } else if (err != ERR_INPROGRESS) {
        ota_http_fail(SOLARCLEAN_OTA_FAIL_DOWNLOAD_FAILED, "dns start failed");
        return -1;
    }
    return 0;
}

const char *SolarCleanOta_GetHardwareVersion(void)
{
    return SOLARCLEAN_HARDWARE_VERSION;
}

const char *SolarCleanOta_GetFirmwareVersion(void)
{
    static char version[32];

    (void)snprintf(version, sizeof(version), "%u.%u.%u.%u",
                   (unsigned)USER_FIRMWARE_MAJOR_VERSION,
                   (unsigned)USER_FIRMWARE_MINOR_VERSION,
                   (unsigned)USER_FIRMWARE_MODIFY_VERSION,
                   (unsigned)USER_FIRMWARE_DEBUG_VERSION);
    return version;
}

uint32_t SolarCleanOta_GetInnerVersion(void)
{
#ifdef USER_FIRMWARE_INNER_VERSION
    return USER_FIRMWARE_INNER_VERSION;
#else
    return SOLARCLEAN_FIRMWARE_INNER_VERSION;
#endif
}

const char *SolarCleanOta_GetSlot(void)
{
    return ota_slot_to_string(ota_current_slot());
}

const char *SolarCleanOta_GetStatus(void)
{
    return ota_state_to_string(s_ota.state);
}

const char *SolarCleanOta_GetLastResult(void)
{
    return s_ota.lastResult;
}

const char *SolarCleanOta_GetLastFailReason(void)
{
    return ota_fail_to_string(s_ota.failReason);
}

const char *SolarCleanOta_GetNetwork(void)
{
    return "4g";
}

uint8_t SolarCleanOta_IsActive(void)
{
    return (s_http.active != 0U || s_http.pendingValid != 0U || s_http.resetScheduled != 0U) ? 1U : 0U;
}

void SolarCleanOta_ConfirmRunningImage(void)
{
    T_SolarCleanOtaInfo info;
    E_SolarCleanOtaSlot runningSlot = ota_current_slot();

    ota_logf("[OTA] confirm running slot=%u", (unsigned)runningSlot);
    if (SolarCleanOtaState_Load(&info) != 0) {
        ota_log("[OTA] confirm skipped: state load failed");
        return;
    }
    if (info.otaState != SOLARCLEAN_BOOT_OTA_STATE_PENDING_VERIFY) {
        ota_logf("[OTA] confirm skipped: otaState=%u", (unsigned)info.otaState);
        return;
    }
    if (info.targetSlot != (uint8_t)runningSlot) {
        ota_logf("[OTA] confirm skipped: target=%u running=%u",
                 (unsigned)info.targetSlot, (unsigned)runningSlot);
        return;
    }

    SolarCleanOtaState_MarkValid(&info, runningSlot);
    if (SolarCleanOtaState_Save(&info) == 0)
        ota_log("[OTA] confirm saved VALID");
    else
        ota_log("[OTA] confirm save failed");
}

void SolarCleanOta_Init(void)
{
    memset(&s_http, 0, sizeof(s_http));
    s_http.state = SOLARCLEAN_OTA_HTTP_IDLE;
}

void SolarCleanOta_Service(void)
{
    T_SolarCleanOtaStartRequest pending;
    SolarCleanOta_PublishStatus publish;
    void *publishUser;
    uint8_t hasPending = 0U;
    uint32_t now = (uint32_t)xTaskGetTickCount();

    taskENTER_CRITICAL();
    if (s_http.active == 0U && s_http.pendingValid != 0U) {
        pending = s_http.pendingReq;
        publish = s_http.pendingPublish;
        publishUser = s_http.pendingPublishUser;
        s_http.pendingValid = 0U;
        hasPending = 1U;
    }
    taskEXIT_CRITICAL();

    if (hasPending != 0U) {
        if (ota_http_start_now(&pending, publish, publishUser) != 0) {
            ota_publish_status(publish, publishUser,
                               SolarCleanOta_GetStatus(),
                               s_ota.progress,
                               s_ota.targetVersion);
        }
        return;
    }

    if (s_http.resetScheduled != 0U &&
        (int32_t)(now - s_http.resetAtMs) >= 0) {
        NVIC_SystemReset();
    }

    if (s_http.active != 0U &&
        (uint32_t)(now - s_http.startedMs) > pdMS_TO_TICKS(SOLARCLEAN_OTA_HTTP_TIMEOUT_MS)) {
        ota_http_fail(SOLARCLEAN_OTA_FAIL_DOWNLOAD_FAILED, "ota timeout");
    }
}

void SolarCleanOta_HandleStart(const T_SolarCleanOtaStartRequest *req,
                               T_SolarCleanOtaAck *ack,
                               SolarCleanOta_PublishStatus publish,
                               void *publishUser)
{
    E_SolarCleanOtaSlot currentSlot;
    E_SolarCleanOtaSlot targetSlot;
    uint32_t targetSlotSize;

    if (req == NULL) {
        ota_fail(SOLARCLEAN_OTA_FAIL_BAD_REQUEST);
        ota_set_ack(ack, 0, 500, "internal ota request error");
        return;
    }

    currentSlot = ota_current_slot();
    targetSlot = ota_staging_slot();
    targetSlotSize = SolarCleanOtaFlash_GetSlotSize(targetSlot);

    ota_enter_state(SOLARCLEAN_OTA_STATE_VALIDATING, 0U);
    (void)snprintf(s_ota.targetVersion, sizeof(s_ota.targetVersion), "%s", req->targetVersion);
    s_ota.targetInnerVersion = req->targetInnerVersion;
    ota_logf("[OTA] start currentSlot=%s targetSlot=%s target=%s inner=%lu size=%lu hw=%s sha=%s url=%s",
             ota_slot_to_string(currentSlot),
             ota_slot_to_string(targetSlot),
             req->targetVersion,
             (unsigned long)req->targetInnerVersion,
             (unsigned long)req->fileSize,
             req->hardwareVersion,
             req->sha256[0] ? "yes" : "no",
             req->downloadUrl);

    if (req->targetVersion[0] == '\0' ||
        req->targetInnerVersion == 0U ||
        req->downloadUrl[0] == '\0') {
        ota_log("[OTA] start rejected: missing field");
        ota_fail(SOLARCLEAN_OTA_FAIL_BAD_REQUEST);
        ota_set_ack(ack, 0, 100, "missing ota field");
        ota_publish_status(publish, publishUser, SolarCleanOta_GetStatus(), 0U, s_ota.targetVersion);
        return;
    }

    if (req->hardwareVersion[0] != '\0' &&
        strcmp(req->hardwareVersion, SOLARCLEAN_HARDWARE_VERSION) != 0) {
        ota_logf("[OTA] start rejected: hw mismatch req=%s local=%s",
                 req->hardwareVersion, SOLARCLEAN_HARDWARE_VERSION);
        ota_fail(SOLARCLEAN_OTA_FAIL_HW_NOT_MATCH);
        ota_set_ack(ack, 0, 100, "hardware mismatch");
        ota_publish_status(publish, publishUser, SolarCleanOta_GetStatus(), 0U, s_ota.targetVersion);
        return;
    }

    if (WaterPump_GetState() != 0U || PwmSwing_IsRunning() != 0U) {
        ota_logf("[OTA] start rejected: busy pump=%u swing=%u",
                 (unsigned)WaterPump_GetState(), (unsigned)PwmSwing_IsRunning());
        ota_fail(SOLARCLEAN_OTA_FAIL_DEVICE_BUSY);
        ota_set_ack(ack, 0, 200, "device busy");
        ota_publish_status(publish, publishUser, SolarCleanOta_GetStatus(), 0U, s_ota.targetVersion);
        return;
    }

    if (req->fileSize != 0U && req->fileSize > targetSlotSize) {
        ota_logf("[OTA] start rejected: image too large size=%lu limit=%lu",
                 (unsigned long)req->fileSize,
                 (unsigned long)targetSlotSize);
        ota_fail(SOLARCLEAN_OTA_FAIL_IMAGE_TOO_LARGE);
        ota_set_ack(ack, 0, 100, "ota image too large");
        ota_publish_status(publish, publishUser, SolarCleanOta_GetStatus(), 0U, s_ota.targetVersion);
        return;
    }

    {
        uint8_t expectedHash[SOLARCLEAN_OTA_SHA256_SIZE];
        if (req->sha256[0] != '\0' &&
            SolarCleanOtaHash_ParseHex(req->sha256, expectedHash) != 0) {
            ota_log("[OTA] start rejected: bad sha256 format");
            ota_fail(SOLARCLEAN_OTA_FAIL_BAD_HASH);
            ota_set_ack(ack, 0, 100, "bad sha256");
            ota_publish_status(publish, publishUser, SolarCleanOta_GetStatus(), 0U, s_ota.targetVersion);
            return;
        }
    }

    if (s_http.active != 0U || s_http.pendingValid != 0U) {
        ota_log("[OTA] start rejected: ota already running");
        ota_fail(SOLARCLEAN_OTA_FAIL_DEVICE_BUSY);
        ota_set_ack(ack, 0, 200, "ota busy");
        ota_publish_status(publish, publishUser, SolarCleanOta_GetStatus(), s_ota.progress, s_ota.targetVersion);
        return;
    }

    taskENTER_CRITICAL();
    s_http.pendingReq = *req;
    s_http.pendingPublish = publish;
    s_http.pendingPublishUser = publishUser;
    s_http.pendingValid = 1U;
    taskEXIT_CRITICAL();
    ota_enter_state(SOLARCLEAN_OTA_STATE_DOWNLOADING, 0U);
    ota_set_ack(ack, 1, 0, "ota started");
    ota_log("[OTA] start accepted");
    ota_publish_status(publish, publishUser, SolarCleanOta_GetStatus(), 0U, s_ota.targetVersion);
}

int SolarCleanOta_CommitNextSlotForReboot(const T_SolarCleanOtaStartRequest *req,
                                          T_SolarCleanOtaAck *ack,
                                          SolarCleanOta_PublishStatus publish,
                                          void *publishUser)
{
    uint8_t expectedHash[SOLARCLEAN_OTA_SHA256_SIZE];
    T_SolarCleanOtaInfo info;
    E_SolarCleanOtaSlot currentSlot;
    E_SolarCleanOtaSlot targetSlot;
    uint32_t targetSlotSize;
    uint32_t targetVersion;
    uint8_t hasSha256 = 0U;

    if (req == NULL) {
        ota_fail(SOLARCLEAN_OTA_FAIL_BAD_REQUEST);
        ota_set_ack(ack, 0, 500, "internal ota request error");
        return -1;
    }

    currentSlot = ota_current_slot();
    targetSlot = ota_staging_slot();
    targetSlotSize = SolarCleanOtaFlash_GetSlotSize(targetSlot);

    (void)snprintf(s_ota.targetVersion, sizeof(s_ota.targetVersion), "%s", req->targetVersion);
    s_ota.targetInnerVersion = req->targetInnerVersion;
    ota_enter_state(SOLARCLEAN_OTA_STATE_VERIFYING, 90U);
    ota_logf("[OTA] commit start currentSlot=%s targetSlot=%s size=%lu targetInner=%lu sha=%s",
             ota_slot_to_string(currentSlot),
             ota_slot_to_string(targetSlot),
             (unsigned long)req->fileSize,
             (unsigned long)req->targetInnerVersion,
             req->sha256[0] ? "yes" : "no");
    ota_publish_status(publish, publishUser, SolarCleanOta_GetStatus(), 90U, s_ota.targetVersion);

    if (req->fileSize == 0U || req->fileSize > targetSlotSize ||
        req->targetInnerVersion == 0U) {
        ota_log("[OTA] commit rejected: bad request");
        ota_fail(SOLARCLEAN_OTA_FAIL_BAD_REQUEST);
        ota_set_ack(ack, 0, 100, "bad ota commit request");
        ota_publish_status(publish, publishUser, SolarCleanOta_GetStatus(), 90U, s_ota.targetVersion);
        return -1;
    }

    targetVersion = req->targetInnerVersion;
    memset(expectedHash, 0, sizeof(expectedHash));
    if (req->sha256[0] != '\0') {
        if (SolarCleanOtaHash_ParseHex(req->sha256, expectedHash) != 0) {
            ota_log("[OTA] commit rejected: bad sha256 format");
            ota_fail(SOLARCLEAN_OTA_FAIL_BAD_HASH);
            ota_set_ack(ack, 0, 100, "bad sha256");
            ota_publish_status(publish, publishUser, SolarCleanOta_GetStatus(), 90U, s_ota.targetVersion);
            return -1;
        }
        hasSha256 = 1U;
    }

    if (hasSha256 != 0U) {
        ota_logf("[OTA] sha256 verify start slot=%s", ota_slot_to_string(targetSlot));
        ota_log_slot_probe("before-sha", targetSlot);
        if (SolarCleanOtaHash_VerifySlot(targetSlot, req->fileSize, expectedHash) != 0) {
            ota_log("[OTA] sha256 verify failed");
            ota_fail(SOLARCLEAN_OTA_FAIL_HASH_VERIFY_FAILED);
            ota_set_ack(ack, 0, 500, "sha256 verify failed");
            ota_publish_status(publish, publishUser, SolarCleanOta_GetStatus(), 90U, s_ota.targetVersion);
            return -1;
        }
        ota_log("[OTA] sha256 verify OK");
    }

    ota_logf("[OTA] save pending state current=%u target=%u prev=%u",
             (unsigned)currentSlot, (unsigned)targetSlot, (unsigned)currentSlot);
    SolarCleanOtaState_BuildPendingVerify(&info,
                                          currentSlot,
                                          targetSlot,
                                          targetVersion,
                                          req->fileSize,
                                          expectedHash);

    if (SolarCleanOtaState_Save(&info) != 0) {
        ota_log("[OTA] save pending state failed");
        ota_fail(SOLARCLEAN_OTA_FAIL_STATE_WRITE_FAILED);
        ota_set_ack(ack, 0, 500, "ota state write failed");
        ota_publish_status(publish, publishUser, SolarCleanOta_GetStatus(), 95U, s_ota.targetVersion);
        return -1;
    }

    ota_enter_state(SOLARCLEAN_OTA_STATE_PENDING_REBOOT, 100U);
    ota_set_ack(ack, 1, 0, "ota pending reboot");
    ota_log("[OTA] commit OK pending reboot");
    ota_publish_status(publish, publishUser, SolarCleanOta_GetStatus(), 100U, s_ota.targetVersion);
    return 0;
}

int SolarCleanOta_DownloadNextSlotAndCommit(const T_SolarCleanOtaStartRequest *req,
                                            T_SolarCleanOtaAck *ack,
                                            SolarCleanOta_PublishStatus publish,
                                            void *publishUser)
{
    (void)req;
    (void)publish;
    (void)publishUser;
    ota_set_ack(ack, 0, 500, "sync ota download disabled in ppp mode");
    ota_log("[OTA] sync download API disabled; use SolarCleanOta_HandleStart/Service");
    return -1;
}

int SolarCleanOta_CommitSlotBForReboot(const T_SolarCleanOtaStartRequest *req,
                                       T_SolarCleanOtaAck *ack,
                                       SolarCleanOta_PublishStatus publish,
                                       void *publishUser)
{
    return SolarCleanOta_CommitNextSlotForReboot(req, ack, publish, publishUser);
}

int SolarCleanOta_DownloadSlotBAndCommit(const T_SolarCleanOtaStartRequest *req,
                                         T_SolarCleanOtaAck *ack,
                                         SolarCleanOta_PublishStatus publish,
                                         void *publishUser)
{
    return SolarCleanOta_DownloadNextSlotAndCommit(req, ack, publish, publishUser);
}
