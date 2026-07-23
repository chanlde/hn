/**
 * @file air780e_http.c
 * @brief HTTP/HTTPS GET for Air780E.
 *
 * http://  uses the legacy TCP transparent path.
 * https:// uses the Air780E built-in HTTP/SSL AT stack and downloads in
 * BREAK/BREAKEND ranges. HTTPEX is kept as an experimental path but disabled
 * by default because the module can stall during continuous reads.
 */

#include "air780e_http.h"
#include "air780e_mqtt.h"
#include "mqtt_app_config.h"
#include "uart.h"

#include "cmsis_os.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define UART_AT                  UART_NUM_7
#define UART_LOG                 UART_NUM_1
#define AT_READ_MS               10U
#define HEADER_MAX               4096U

#ifndef HTTPS_READ_CHUNK_MAX
#define HTTPS_READ_CHUNK_MAX     8192U
#endif
#define HTTPS_READ_CHUNK_SAFE    3072U
#define HTTPS_AT_TIMEOUT_MS      10000U
#define HTTPS_BEARER_TIMEOUT_MS  30000U
#define HTTPS_ACTION_TIMEOUT_MS  (5U * 60U * 1000U)
#define HTTPEX_READ_CHUNK_MAX    3072U
#define HTTPEX_WAIT_DATA_MS      30000U
#define HTTPEX_READ_TIMEOUT_MS   8000U
#define HTTPEX_NO_DATA_RETRY_MAX 8U

#define HTTPS_READ_NO_DATA       (-2)

static uint32_t s_httpsChunkMax = HTTPS_READ_CHUNK_SAFE;
static uint8_t s_useHttpEx = 0U;

typedef struct {
    uint8_t dataReady;
    uint8_t done;
    unsigned int status;
} T_HttpExSignal;

static int https_wait_ok_tail(const uint8_t *buf, uint32_t len);

static void http_log(const char *msg)
{
    if (msg == NULL)
        return;
    UART_Write(UART_LOG, (const uint8_t *)msg, (uint16_t)strlen(msg));
    UART_Write(UART_LOG, (const uint8_t *)"\r\n", 2U);
}

static void http_logf(const char *fmt, uint32_t a, uint32_t b, uint32_t c)
{
    char buf[128];
    int n = snprintf(buf, sizeof(buf), fmt, (unsigned long)a, (unsigned long)b, (unsigned long)c);

    if (n <= 0)
        return;
    if (n >= (int)sizeof(buf))
        n = (int)sizeof(buf) - 1;
    UART_Write(UART_LOG, (const uint8_t *)buf, (uint16_t)n);
    UART_Write(UART_LOG, (const uint8_t *)"\r\n", 2U);
}

static void http_log_text_preview(const char *tag, const uint8_t *data, uint32_t len)
{
    char buf[160];
    uint32_t wi = 0U;
    uint32_t copyLen = len;

    if (tag == NULL || data == NULL || len == 0U)
        return;
    if (copyLen > 96U)
        copyLen = 96U;

    wi += (uint32_t)snprintf(buf, sizeof(buf), "%s len=%lu data=\"", tag, (unsigned long)len);
    for (uint32_t i = 0U; i < copyLen && wi + 5U < sizeof(buf); i++) {
        uint8_t c = data[i];
        if (c == '\r' || c == '\n' || c == '\t') {
            buf[wi++] = ' ';
        } else if (c >= 32U && c < 127U) {
            buf[wi++] = (char)c;
        } else {
            wi += (uint32_t)snprintf(&buf[wi], sizeof(buf) - wi, "\\x%02X", c);
        }
    }
    if (len > copyLen && wi + 4U < sizeof(buf)) {
        buf[wi++] = '.';
        buf[wi++] = '.';
        buf[wi++] = '.';
    }
    if (wi + 2U < sizeof(buf)) {
        buf[wi++] = '"';
        buf[wi] = '\0';
    } else {
        buf[sizeof(buf) - 1U] = '\0';
    }
    http_log(buf);
}

static void drain_uart7(void)
{
    uint8_t b[128];

    while (UART_Read(UART_AT, b, sizeof(b)) > 0)
        ;
}

static bool at_send_expect(const char *at, const char *expect, uint32_t timeout_ms)
{
    uint8_t rx[512];
    int n = 0;
    uint32_t t0 = osKernelGetTickCount();

    drain_uart7();
    if (at != NULL && at[0] != '\0') {
        UART_Write(UART_AT, (const uint8_t *)at, (uint16_t)strlen(at));
        UART_Write(UART_AT, (const uint8_t *)"\r\n", 2U);
        osDelay(AT_READ_MS);
    }

    while ((osKernelGetTickCount() - t0) < timeout_ms) {
        int r = UART_Read(UART_AT, rx + n, (uint16_t)(sizeof(rx) - 1U - (size_t)n));
        if (r > 0) {
            n += r;
            rx[n] = '\0';
            if (expect != NULL && expect[0] != '\0' && strstr((char *)rx, expect) != NULL)
                return true;
            if (strstr((char *)rx, "ERROR") != NULL || strstr((char *)rx, "FAIL") != NULL)
                return false;
            if (n > (int)sizeof(rx) - 64) {
                memmove(rx, rx + n - 128, 128U);
                n = 128;
                rx[n] = '\0';
            }
        }
        osDelay(AT_READ_MS);
    }
    return false;
}

static void at_send_raw(const uint8_t *data, uint16_t len)
{
    UART_Write(UART_AT, data, len);
}

static bool at_wait_prompt_send_raw(const uint8_t *data, uint16_t len)
{
    char cipsend[32];
    uint8_t prompt[64];
    uint32_t t0;

    (void)snprintf(cipsend, sizeof(cipsend), "AT+CIPSEND=%u\r\n", (unsigned int)len);
    UART_Write(UART_AT, (const uint8_t *)cipsend, (uint16_t)strlen(cipsend));
    t0 = osKernelGetTickCount();
    while ((osKernelGetTickCount() - t0) < 400U) {
        int r = UART_Read(UART_AT, prompt, sizeof(prompt));
        if (r > 0 && memchr(prompt, '>', (size_t)r) != NULL)
            break;
        osDelay(5U);
    }

    at_send_raw(data, len);
    t0 = osKernelGetTickCount();
    while ((osKernelGetTickCount() - t0) < 8000U) {
        uint8_t rx[256];
        int r = UART_Read(UART_AT, rx, sizeof(rx));
        if (r > 0) {
            rx[(r < (int)sizeof(rx)) ? r : (int)sizeof(rx) - 1] = '\0';
            if (strstr((char *)rx, "SEND OK") != NULL || strstr((char *)rx, "DATA ACCEPT") != NULL)
                return true;
            if (strstr((char *)rx, "FAIL") != NULL || strstr((char *)rx, "ERROR") != NULL)
                return false;
        }
        osDelay(AT_READ_MS);
    }
    return false;
}

static int attach_and_tcp_connect(const char *host, uint16_t port)
{
    char cmd[160];

    if (!at_send_expect("AT+CGPADDR=1", "+CGPADDR", 3000U))
        (void)0;
    (void)at_send_expect("AT+CIPSHUT", "SHUT OK", 5000U);
    osDelay(300U);
    if (!at_send_expect("AT+CIPMUX=0", "OK", 2000U))
        return -1;
    osDelay(100U);
    if (!at_send_expect("AT+CSTT=\"CMNET\"", "OK", 5000U))
        return -1;
    osDelay(100U);
    if (!at_send_expect("AT+CIICR", "OK", 10000U))
        return -1;
    osDelay(200U);
    (void)at_send_expect("AT+CIFSR", ".", 3000U);
    osDelay(100U);
    (void)at_send_expect("AT+CIPQSEND=1", "OK", 2000U);
    osDelay(100U);

    (void)snprintf(cmd, sizeof(cmd), "AT+CIPSTART=\"TCP\",\"%s\",%u\r\n", host, (unsigned int)port);
    drain_uart7();
    UART_Write(UART_AT, (const uint8_t *)cmd, (uint16_t)strlen(cmd));
    osDelay(50U);
    {
        uint32_t t0 = osKernelGetTickCount();
        uint8_t rx[512];
        int n = 0;
        while ((osKernelGetTickCount() - t0) < 15000U) {
            int r = UART_Read(UART_AT, rx + n, (uint16_t)(sizeof(rx) - 1U - (size_t)n));
            if (r > 0) {
                n += r;
                rx[n] = '\0';
                if (strstr((char *)rx, "CONNECT") != NULL)
                    return 0;
                if (strstr((char *)rx, "FAIL") != NULL || strstr((char *)rx, "ERROR") != NULL)
                    return -1;
                if (n > (int)sizeof(rx) - 64) {
                    memmove(rx, rx + n - 128, 128U);
                    n = 128;
                    rx[n] = '\0';
                }
            }
            osDelay(AT_READ_MS);
        }
    }
    return -1;
}

static int parse_http_url(const char *url, char *host, size_t host_sz, uint16_t *port,
                          char *path, size_t path_sz)
{
    const char *p = url;
    const char *slash;
    const char *colon;

    *port = 80U;
    if (strncmp(p, "http://", 7U) == 0) {
        p += 7U;
    } else {
        return -1;
    }

    slash = strchr(p, '/');
    if (slash == NULL) {
        if (strlen(p) >= host_sz)
            return -1;
        strncpy(host, p, host_sz - 1U);
        host[host_sz - 1U] = '\0';
        (void)strncpy(path, "/", path_sz - 1U);
        path[path_sz - 1U] = '\0';
        return 0;
    }

    colon = memchr(p, ':', (size_t)(slash - p));
    if (colon != NULL && colon < slash) {
        size_t hl = (size_t)(colon - p);
        if (hl >= host_sz)
            return -1;
        memcpy(host, p, hl);
        host[hl] = '\0';
        *port = (uint16_t)atoi(colon + 1);
    } else {
        size_t hl = (size_t)(slash - p);
        if (hl >= host_sz)
            return -1;
        memcpy(host, p, hl);
        host[hl] = '\0';
    }

    if (strlen(slash) >= path_sz)
        return -1;
    strncpy(path, slash, path_sz - 1U);
    path[path_sz - 1U] = '\0';
    return 0;
}

static int parse_content_length(const char *hdr)
{
    const char *p = strstr(hdr, "Content-Length:");
    const char *v;

    if (p == NULL)
        p = strstr(hdr, "content-length:");
    if (p == NULL)
        return -1;
    v = strchr(p, ':');
    if (v == NULL)
        return -1;
    v++;
    while (*v == ' ' || *v == '\t')
        v++;
    return atoi(v);
}

static uint32_t parse_http_total_length(const char *hdr)
{
    int contentLen = parse_content_length(hdr);
    const char *p;

    if (contentLen > 0)
        return (uint32_t)contentLen;

    p = strstr(hdr, "Content-Range:");
    if (p == NULL)
        p = strstr(hdr, "content-range:");
    if (p != NULL) {
        const char *slash = strchr(p, '/');
        if (slash != NULL && slash[1] >= '0' && slash[1] <= '9')
            return (uint32_t)strtoul(slash + 1, NULL, 10);
    }

    return 0U;
}

static int http_get_to_stream_raw(const char *url,
                                  uint32_t expectedTotal,
                                  Air780eHttp_BeginCb onBegin,
                                  void *beginCtx,
                                  Air780eHttp_BodyChunkCb onBody,
                                  void *bodyCtx,
                                  void (*onProgress)(uint32_t got, uint32_t total, void *ctx),
                                  void *progressCtx)
{
    char host[128];
    char path[320];
    uint16_t port;
    uint8_t hdrBuf[HEADER_MAX];
    uint8_t bodyBuf[512];
    uint32_t hdrGot = 0U;
    uint32_t bodyGot = 0U;
    int contentLen;
    uint32_t bodyTotal;
    uint32_t tEnd;
    char *hdrEnd;
    uint32_t totalHint;
    int ret = -1;

    if (parse_http_url(url, host, sizeof(host), &port, path, sizeof(path)) != 0)
        goto out_http;

    if (attach_and_tcp_connect(host, port) != 0)
        goto out_http;

    {
        char req[640];
        int ln = snprintf(req, sizeof(req),
                          "GET %s HTTP/1.1\r\nHost: %s\r\nConnection: close\r\nUser-Agent: hn-mcu\r\n\r\n",
                          path, host);
        if (ln <= 0 || ln >= (int)sizeof(req))
            goto out_close;
        if (!at_wait_prompt_send_raw((const uint8_t *)req, (uint16_t)strlen(req)))
            goto out_close;
    }

    tEnd = osKernelGetTickCount() + AIR780E_HTTP_GET_TIMEOUT_MS;
    while (osKernelGetTickCount() < tEnd && hdrGot < HEADER_MAX - 256U) {
        int r = UART_Read(UART_AT, hdrBuf + hdrGot, (uint16_t)(HEADER_MAX - hdrGot - 1U));
        if (r > 0)
            hdrGot += (uint32_t)r;
        hdrBuf[hdrGot] = '\0';
        hdrEnd = strstr((char *)hdrBuf, "\r\n\r\n");
        if (hdrEnd != NULL)
            break;
        osDelay(AT_READ_MS);
    }

    hdrBuf[hdrGot < HEADER_MAX ? hdrGot : HEADER_MAX - 1U] = '\0';
    hdrEnd = strstr((char *)hdrBuf, "\r\n\r\n");
    if (hdrEnd == NULL)
        goto out_close;

    if (strstr((char *)hdrBuf, "HTTP/1.") == NULL ||
        (strstr((char *)hdrBuf, "200") == NULL && strstr((char *)hdrBuf, "201") == NULL))
        goto out_close;

    contentLen = parse_content_length((char *)hdrBuf);
    if (contentLen < 0)
        goto out_close;

    bodyTotal = (uint32_t)contentLen;
    totalHint = expectedTotal > 0U ? expectedTotal : bodyTotal;
    if (expectedTotal > 0U && bodyTotal != expectedTotal)
        goto out_close;
    if (onBegin != NULL && onBegin(bodyTotal, beginCtx) != 0)
        goto out_close;

    {
        uint32_t cp = (uint32_t)((hdrEnd + 4) - (char *)hdrBuf);
        uint32_t initial = hdrGot > cp ? hdrGot - cp : 0U;
        if (initial > bodyTotal)
            initial = bodyTotal;
        if (initial > 0U && onBody(hdrBuf + cp, initial, bodyCtx) != 0)
            goto out_close;
        bodyGot = initial;
        if (onProgress != NULL && totalHint > 0U)
            onProgress(bodyGot, totalHint, progressCtx);
    }

    while (bodyGot < bodyTotal && osKernelGetTickCount() < tEnd) {
        uint32_t need = bodyTotal - bodyGot;
        uint32_t chunk = need > sizeof(bodyBuf) ? sizeof(bodyBuf) : need;
        int r = UART_Read(UART_AT, bodyBuf, (uint16_t)chunk);
        if (r > 0) {
            if (onBody(bodyBuf, (uint32_t)r, bodyCtx) != 0)
                goto out_close;
            bodyGot += (uint32_t)r;
            if (onProgress != NULL && totalHint > 0U)
                onProgress(bodyGot, totalHint, progressCtx);
        } else {
            osDelay(AT_READ_MS);
        }
    }

    ret = (bodyGot == bodyTotal) ? 0 : -1;

out_close:
    (void)at_send_expect("AT+CIPCLOSE", "OK", 3000U);
out_http:
    return ret;
}

static int https_set_range(uint32_t start, uint32_t end)
{
    char cmd[64];

    (void)snprintf(cmd, sizeof(cmd), "AT+HTTPPARA=\"BREAK\",%lu", (unsigned long)start);
    if (!at_send_expect(cmd, "OK", HTTPS_AT_TIMEOUT_MS))
        return -1;
    (void)snprintf(cmd, sizeof(cmd), "AT+HTTPPARA=\"BREAKEND\",%lu", (unsigned long)end);
    if (!at_send_expect(cmd, "OK", HTTPS_AT_TIMEOUT_MS))
        return -1;
    return 0;
}

static int https_http_action(unsigned int method, unsigned int *status, uint32_t *length)
{
    char cmd[32];
    uint8_t rx[512];
    int n = 0;
    uint32_t t0;

    if (status == NULL || length == NULL)
        return -1;

    *status = 0U;
    *length = 0U;
    (void)snprintf(cmd, sizeof(cmd), "AT+HTTPACTION=%u\r\n", method);
    drain_uart7();
    UART_Write(UART_AT, (const uint8_t *)cmd, (uint16_t)strlen(cmd));

    t0 = osKernelGetTickCount();
    while ((osKernelGetTickCount() - t0) < HTTPS_ACTION_TIMEOUT_MS) {
        int r = UART_Read(UART_AT, rx + n, (uint16_t)(sizeof(rx) - 1U - (size_t)n));
        if (r > 0) {
            char *p;

            n += r;
            rx[n] = '\0';
            p = strstr((char *)rx, "+HTTPACTION:");
            if (p != NULL) {
                unsigned int gotMethod = 0U;
                unsigned int gotStatus = 0U;
                unsigned long gotLen = 0UL;
                if (sscanf(p, "+HTTPACTION: %u,%u,%lu", &gotMethod, &gotStatus, &gotLen) == 3 ||
                    sscanf(p, "+HTTPACTION:%u,%u,%lu", &gotMethod, &gotStatus, &gotLen) == 3) {
                    if (gotMethod == method) {
                        *status = gotStatus;
                        *length = (uint32_t)gotLen;
                        return 0;
                    }
                }
            }
            if (strstr((char *)rx, "ERROR") != NULL || strstr((char *)rx, "FAIL") != NULL)
                return -1;
            if (n > (int)sizeof(rx) - 64) {
                memmove(rx, rx + n - 128, 128U);
                n = 128;
                rx[n] = '\0';
            }
        }
        osDelay(AT_READ_MS);
    }

    return -1;
}

static int https_parse_ex_action(const char *buf, unsigned int method, unsigned int *status)
{
    const char *p = strstr(buf, "+HTTPEXACTION:");
    unsigned int gotMethod = 0U;
    unsigned int gotStatus = 0U;

    if (p == NULL)
        return 0;
    if (sscanf(p, "+HTTPEXACTION: %u,%u", &gotMethod, &gotStatus) == 2 ||
        sscanf(p, "+HTTPEXACTION:%u,%u", &gotMethod, &gotStatus) == 2) {
        if (gotMethod == method) {
            if (status != NULL)
                *status = gotStatus;
            return 1;
        }
    }
    return 0;
}

static void https_ex_parse_signal(const char *buf, unsigned int method, T_HttpExSignal *signal)
{
    unsigned int status = 0U;

    if (buf == NULL || signal == NULL)
        return;
    if (strstr(buf, "+HTTPEXGET") != NULL)
        signal->dataReady = 1U;
    if (https_parse_ex_action(buf, method, &status) != 0) {
        signal->done = 1U;
        signal->status = status;
    }
}

static int https_ex_wait_signal(unsigned int method, T_HttpExSignal *signal, uint32_t timeoutMs)
{
    uint8_t rx[512];
    int n = 0;
    uint32_t t0 = osKernelGetTickCount();

    if (signal == NULL)
        return -1;

    while ((osKernelGetTickCount() - t0) < timeoutMs) {
        int r = UART_Read(UART_AT, rx + n, (uint16_t)(sizeof(rx) - 1U - (size_t)n));
        if (r > 0) {
            n += r;
            rx[n] = '\0';
            https_ex_parse_signal((char *)rx, method, signal);
            if (signal->dataReady != 0U || signal->done != 0U)
                return 0;
            if (strstr((char *)rx, "ERROR") != NULL || strstr((char *)rx, "FAIL") != NULL)
                return -1;
            if (n > (int)sizeof(rx) - 64) {
                memmove(rx, rx + n - 128, 128U);
                n = 128;
                rx[n] = '\0';
            }
        }
        osDelay(AT_READ_MS);
    }
    return -1;
}

static int https_ex_start_get(T_HttpExSignal *signal)
{
    const unsigned int method = 0U;
    uint8_t rx[512];
    int n = 0;
    uint32_t t0;

    if (signal == NULL)
        return -1;
    memset(signal, 0, sizeof(*signal));

    drain_uart7();
    UART_Write(UART_AT, (const uint8_t *)"AT+HTTPEXACTION=0\r\n",
               (uint16_t)strlen("AT+HTTPEXACTION=0\r\n"));

    t0 = osKernelGetTickCount();
    while ((osKernelGetTickCount() - t0) < HTTPS_AT_TIMEOUT_MS) {
        int r = UART_Read(UART_AT, rx + n, (uint16_t)(sizeof(rx) - 1U - (size_t)n));
        if (r > 0) {
            n += r;
            rx[n] = '\0';
            https_ex_parse_signal((char *)rx, method, signal);
            if (strstr((char *)rx, "\r\nOK\r\n") != NULL || strstr((char *)rx, "\nOK\r\n") != NULL)
                return 0;
            if (strstr((char *)rx, "ERROR") != NULL || strstr((char *)rx, "+CME ERROR") != NULL)
                return -1;
            if (n > (int)sizeof(rx) - 64) {
                memmove(rx, rx + n - 128, 128U);
                n = 128;
                rx[n] = '\0';
            }
        }
        osDelay(AT_READ_MS);
    }
    return -1;
}

static int https_ex_wait_ok_tail(const uint8_t *buf, uint32_t len, T_HttpExSignal *signal)
{
    uint8_t rx[160];
    int n = 0;
    uint32_t t0;

    if (buf != NULL && len > 0U) {
        uint32_t copyLen = len > sizeof(rx) - 1U ? sizeof(rx) - 1U : len;
        memcpy(rx, buf + len - copyLen, copyLen);
        n = (int)copyLen;
        rx[n] = '\0';
        https_ex_parse_signal((char *)rx, 0U, signal);
        if (strstr((char *)rx, "\r\nOK") != NULL)
            return 0;
        if (strstr((char *)rx, "ERROR") != NULL)
            return -1;
    }

    t0 = osKernelGetTickCount();
    while ((osKernelGetTickCount() - t0) < HTTPEX_READ_TIMEOUT_MS) {
        int r = UART_Read(UART_AT, rx + n, (uint16_t)(sizeof(rx) - 1U - (size_t)n));
        if (r > 0) {
            n += r;
            rx[n] = '\0';
            https_ex_parse_signal((char *)rx, 0U, signal);
            if (strstr((char *)rx, "\r\nOK") != NULL)
                return 0;
            if (strstr((char *)rx, "ERROR") != NULL)
                return -1;
            if (n > (int)sizeof(rx) - 32) {
                memmove(rx, rx + n - 64, 64U);
                n = 64;
                rx[n] = '\0';
            }
        }
        osDelay(AT_READ_MS);
    }

    return -1;
}

static int https_ex_read_chunk(uint32_t maxLen,
                               Air780eHttp_BodyChunkCb onBody,
                               void *bodyCtx,
                               uint32_t *actualLen,
                               T_HttpExSignal *signal)
{
    char cmd[40];
    uint8_t rx[512];
    uint8_t line[64];
    uint8_t tail[128];
    uint32_t lineLen = 0U;
    uint32_t bodyLen = 0U;
    uint32_t bodyGot = 0U;
    uint32_t tailLen = 0U;
    uint8_t headerDone = 0U;
    uint32_t t0;

    if (maxLen == 0U || onBody == NULL || actualLen == NULL || signal == NULL)
        return -1;
    *actualLen = 0U;

    (void)snprintf(cmd, sizeof(cmd), "AT+HTTPEXGET=%lu\r\n", (unsigned long)maxLen);
    UART_Write(UART_AT, (const uint8_t *)cmd, (uint16_t)strlen(cmd));

    t0 = osKernelGetTickCount();
    while ((osKernelGetTickCount() - t0) < HTTPEX_READ_TIMEOUT_MS) {
        int r = UART_Read(UART_AT, rx, sizeof(rx));
        uint32_t pos = 0U;

        if (r <= 0) {
            osDelay(AT_READ_MS);
            continue;
        }

        while (pos < (uint32_t)r) {
            if (headerDone == 0U) {
                if (lineLen < sizeof(line) - 1U)
                    line[lineLen++] = rx[pos];
                pos++;
                if (lineLen >= 2U && line[lineLen - 2U] == '\r' && line[lineLen - 1U] == '\n') {
                    line[lineLen - 2U] = '\0';
                    if (strncmp((char *)line, "+HTTPGET:", 9U) == 0 ||
                        strncmp((char *)line, "+HTTPEXGET:", 11U) == 0) {
                        char *p = strchr((char *)line, ':');
                        bodyLen = (p != NULL) ? (uint32_t)strtoul(p + 1, NULL, 10) : 0U;
                        if (bodyLen == 0U || bodyLen > maxLen) {
                            http_logf("[HTTP] HTTPEX read len invalid body=%lu max=%lu",
                                      bodyLen, maxLen, 0U);
                            return -1;
                        }
                        headerDone = 1U;
                    } else if (strstr((char *)line, "ERROR") != NULL) {
                        return -1;
                    }
                    lineLen = 0U;
                } else if (lineLen >= sizeof(line) - 1U) {
                    lineLen = 0U;
                }
            } else if (bodyGot < bodyLen) {
                uint32_t available = (uint32_t)r - pos;
                uint32_t need = bodyLen - bodyGot;
                uint32_t copyLen = available > need ? need : available;

                if (copyLen > 0U && onBody(rx + pos, copyLen, bodyCtx) != 0)
                    return -1;
                bodyGot += copyLen;
                pos += copyLen;
            } else {
                uint32_t available = (uint32_t)r - pos;
                uint32_t copyLen = available;

                if (copyLen > sizeof(tail) - 1U - tailLen)
                    copyLen = sizeof(tail) - 1U - tailLen;
                if (copyLen > 0U) {
                    memcpy(tail + tailLen, rx + pos, copyLen);
                    tailLen += copyLen;
                    tail[tailLen] = '\0';
                    https_ex_parse_signal((char *)tail, 0U, signal);
                }
                pos = (uint32_t)r;
            }
        }

        if (headerDone != 0U && bodyGot == bodyLen) {
            if (https_ex_wait_ok_tail(tail, tailLen, signal) != 0)
                return -1;
            *actualLen = bodyGot;
            return 0;
        }
    }

    http_logf("[HTTP] HTTPEX read timeout header=%lu body=%lu/%lu",
              (unsigned long)headerDone, (unsigned long)bodyGot, (unsigned long)bodyLen);
    if (headerDone == 0U && bodyGot == 0U)
        return HTTPS_READ_NO_DATA;
    return -1;
}

static int https_read_header_total(uint32_t *total)
{
    uint8_t rx[HEADER_MAX];
    uint32_t got = 0U;
    uint32_t t0;

    if (total == NULL)
        return -1;
    *total = 0U;

    drain_uart7();
    UART_Write(UART_AT, (const uint8_t *)"AT+HTTPHEAD\r\n", (uint16_t)strlen("AT+HTTPHEAD\r\n"));

    t0 = osKernelGetTickCount();
    while ((osKernelGetTickCount() - t0) < HTTPS_AT_TIMEOUT_MS && got < HEADER_MAX - 1U) {
        int r = UART_Read(UART_AT, rx + got, (uint16_t)(HEADER_MAX - got - 1U));
        if (r > 0) {
            got += (uint32_t)r;
            rx[got] = '\0';
            if (strstr((char *)rx, "\r\nOK\r\n") != NULL || strstr((char *)rx, "\nOK\r\n") != NULL) {
                *total = parse_http_total_length((char *)rx);
                return (*total > 0U) ? 0 : -1;
            }
            if (strstr((char *)rx, "ERROR") != NULL)
                return -1;
        }
        osDelay(AT_READ_MS);
    }

    rx[got < HEADER_MAX ? got : HEADER_MAX - 1U] = '\0';
    *total = parse_http_total_length((char *)rx);
    return (*total > 0U) ? 0 : -1;
}

static int https_wait_ok_tail(const uint8_t *buf, uint32_t len)
{
    uint8_t rx[96];
    int n = 0;
    uint32_t t0;

    if (buf != NULL && len > 0U) {
        uint32_t copyLen = len > sizeof(rx) - 1U ? sizeof(rx) - 1U : len;
        memcpy(rx, buf + len - copyLen, copyLen);
        n = (int)copyLen;
        rx[n] = '\0';
        if (strstr((char *)rx, "\r\nOK") != NULL)
            return 0;
        if (strstr((char *)rx, "ERROR") != NULL)
            return -1;
    }

    t0 = osKernelGetTickCount();
    while ((osKernelGetTickCount() - t0) < HTTPS_AT_TIMEOUT_MS) {
        int r = UART_Read(UART_AT, rx + n, (uint16_t)(sizeof(rx) - 1U - (size_t)n));
        if (r > 0) {
            n += r;
            rx[n] = '\0';
            if (strstr((char *)rx, "\r\nOK") != NULL)
                return 0;
            if (strstr((char *)rx, "ERROR") != NULL)
                return -1;
            if (n > (int)sizeof(rx) - 16) {
                memmove(rx, rx + n - 32, 32U);
                n = 32;
                rx[n] = '\0';
            }
        }
        osDelay(AT_READ_MS);
    }

    return -1;
}

static int https_read_chunk(uint32_t expectedLen,
                            Air780eHttp_BodyChunkCb onBody,
                            void *bodyCtx,
                            uint32_t *actualLen)
{
    uint8_t rx[512];
    uint8_t line[64];
    uint32_t lineLen = 0U;
    uint32_t bodyLen = 0U;
    uint32_t bodyGot = 0U;
    uint8_t headerDone = 0U;
    uint8_t tail[96];
    uint32_t tailLen = 0U;
    uint32_t t0;

    if (actualLen != NULL)
        *actualLen = 0U;

    drain_uart7();
    UART_Write(UART_AT, (const uint8_t *)"AT+HTTPREAD\r\n", (uint16_t)strlen("AT+HTTPREAD\r\n"));

    t0 = osKernelGetTickCount();
    while ((osKernelGetTickCount() - t0) < HTTPS_AT_TIMEOUT_MS) {
        int r = UART_Read(UART_AT, rx, sizeof(rx));
        uint32_t pos = 0U;

        if (r <= 0) {
            osDelay(AT_READ_MS);
            continue;
        }

        while (pos < (uint32_t)r) {
            if (headerDone == 0U) {
                if (lineLen < sizeof(line) - 1U)
                    line[lineLen++] = rx[pos];
                pos++;
                if (lineLen >= 2U && line[lineLen - 2U] == '\r' && line[lineLen - 1U] == '\n') {
                    line[lineLen - 2U] = '\0';
                    if (strncmp((char *)line, "+HTTPREAD:", 10U) == 0) {
                        char *p = (char *)line + 10;
                        while (*p == ' ' || *p == '\t')
                            p++;
                        if (strncmp(p, "DATA,", 5U) == 0)
                            p += 5U;
                        bodyLen = (uint32_t)strtoul(p, NULL, 10);
                        if (bodyLen == 0U || bodyLen > expectedLen) {
                            http_logf("[HTTP] HTTPS read len invalid body=%lu expectMax=%lu",
                                      bodyLen, expectedLen, 0U);
                            return -1;
                        }
                        if (bodyLen != expectedLen) {
                            http_logf("[HTTP] HTTPS read short body=%lu expect=%lu",
                                      bodyLen, expectedLen, 0U);
                        }
                        headerDone = 1U;
                    }
                    lineLen = 0U;
                } else if (lineLen >= sizeof(line) - 1U) {
                    lineLen = 0U;
                }
            } else if (bodyGot < bodyLen) {
                uint32_t available = (uint32_t)r - pos;
                uint32_t need = bodyLen - bodyGot;
                uint32_t copyLen = available > need ? need : available;

                if (copyLen > 0U && onBody != NULL && onBody(rx + pos, copyLen, bodyCtx) != 0)
                    return -1;
                bodyGot += copyLen;
                pos += copyLen;
            } else {
                uint32_t available = (uint32_t)r - pos;
                uint32_t copyLen = available;
                if (copyLen > sizeof(tail) - 1U - tailLen)
                    copyLen = sizeof(tail) - 1U - tailLen;
                if (copyLen > 0U) {
                    memcpy(tail + tailLen, rx + pos, copyLen);
                    tailLen += copyLen;
                    tail[tailLen] = '\0';
                }
                pos = (uint32_t)r;
            }
        }

        if (headerDone != 0U && bodyGot == bodyLen) {
            if (https_wait_ok_tail(tail, tailLen) == 0) {
                if (actualLen != NULL)
                    *actualLen = bodyGot;
                return 0;
            }
            http_logf("[HTTP] HTTPS read OK tail missing body=%lu tail=%lu",
                      bodyGot, tailLen, 0U);
            return -1;
        }
    }

    http_logf("[HTTP] HTTPS read timeout header=%lu body=%lu/%lu",
              (unsigned long)headerDone, (unsigned long)bodyGot, (unsigned long)bodyLen);
    return -1;
}

static int https_discard_body(const uint8_t *data, uint32_t len, void *ctx)
{
    (void)data;
    (void)len;
    (void)ctx;
    return 0;
}

typedef struct {
    uint8_t data[128];
    uint32_t len;
} T_HttpPreviewCtx;

static int https_preview_body(const uint8_t *data, uint32_t len, void *ctx)
{
    T_HttpPreviewCtx *preview = (T_HttpPreviewCtx *)ctx;
    uint32_t copyLen;

    if (preview == NULL || data == NULL)
        return -1;
    copyLen = len;
    if (copyLen > sizeof(preview->data) - preview->len)
        copyLen = sizeof(preview->data) - preview->len;
    if (copyLen > 0U) {
        memcpy(&preview->data[preview->len], data, copyLen);
        preview->len += copyLen;
    }
    return 0;
}

static void https_log_error_body(uint32_t len)
{
    T_HttpPreviewCtx preview;

    memset(&preview, 0, sizeof(preview));
    if (len == 0U || len > HTTPS_READ_CHUNK_MAX) {
        http_logf("[HTTP] HTTPS error body skipped len=%lu", len, 0U, 0U);
        return;
    }
    if (https_read_chunk(len, https_preview_body, &preview, NULL) == 0)
        http_log_text_preview("[HTTP] HTTPS error body", preview.data, preview.len);
    else
        http_log("[HTTP] HTTPS error body read failed");
}

static void https_reduce_chunk_on_error(uint32_t offset)
{
    if (s_httpsChunkMax <= HTTPS_READ_CHUNK_SAFE)
        return;
    http_logf("[HTTP] HTTPS reduce chunk old=%lu safe=%lu offset=%lu",
              s_httpsChunkMax, HTTPS_READ_CHUNK_SAFE, offset);
    s_httpsChunkMax = HTTPS_READ_CHUNK_SAFE;
}

static void https_close_session(uint8_t force)
{
    (void)at_send_expect("AT+HTTPTERM", "OK", 3000U);
    osDelay(200U);

    if (force == 0U)
        return;

    (void)at_send_expect("AT+HTTPTERM", "OK", 3000U);
    osDelay(300U);
    (void)at_send_expect("AT+SAPBR=0,1", "OK", 5000U);
    osDelay(1000U);
    (void)at_send_expect("AT+HTTPTERM", "OK", 3000U);
    osDelay(300U);
    (void)at_send_expect("AT+CIPSHUT", "SHUT OK", 5000U);
    osDelay(1000U);
    drain_uart7();
}

static int https_open_session(const char *url)
{
    char cmd[320];

    (void)at_send_expect("AT+HTTPTERM", "OK", 2000U);
    (void)at_send_expect("AT+CIPSHUT", "SHUT OK", 5000U);
    (void)at_send_expect("AT+SAPBR=0,1", "OK", 5000U);

    if (!at_send_expect("AT+SAPBR=3,1,\"CONTYPE\",\"GPRS\"", "OK", HTTPS_AT_TIMEOUT_MS)) {
        http_log("[HTTP] HTTPS open fail: SAPBR CONTYPE");
        return -1;
    }
    if (!at_send_expect("AT+SAPBR=3,1,\"APN\",\"\"", "OK", HTTPS_AT_TIMEOUT_MS)) {
        http_log("[HTTP] HTTPS open fail: SAPBR APN auto");
        return -1;
    }
    if (!at_send_expect("AT+SAPBR=1,1", "OK", HTTPS_BEARER_TIMEOUT_MS)) {
        (void)at_send_expect("AT+SAPBR=3,1,\"APN\",\"CMNET\"", "OK", HTTPS_AT_TIMEOUT_MS);
        if (!at_send_expect("AT+SAPBR=1,1", "OK", HTTPS_BEARER_TIMEOUT_MS)) {
            http_log("[HTTP] HTTPS open fail: SAPBR open");
            return -1;
        }
    }
    (void)at_send_expect("AT+SAPBR=2,1", "+SAPBR:", HTTPS_AT_TIMEOUT_MS);

    (void)at_send_expect("AT+SSLCFG=\"seclevel\",153,0", "OK", HTTPS_AT_TIMEOUT_MS);
    if (!at_send_expect("AT+HTTPINIT", "OK", HTTPS_AT_TIMEOUT_MS)) {
        http_log("[HTTP] HTTPS open fail: HTTPINIT");
        return -1;
    }
    if (!at_send_expect("AT+HTTPSSL=1", "OK", HTTPS_AT_TIMEOUT_MS)) {
        http_log("[HTTP] HTTPS open fail: HTTPSSL");
        return -1;
    }
    (void)at_send_expect("AT+HTTPPARA=\"CID\",1", "OK", HTTPS_AT_TIMEOUT_MS);
    (void)at_send_expect("AT+HTTPPARA=\"REDIR\",1", "OK", HTTPS_AT_TIMEOUT_MS);

    if (strlen(url) + strlen("AT+HTTPPARA=\"URL\",\"\"") >= sizeof(cmd))
        return -1;
    (void)snprintf(cmd, sizeof(cmd), "AT+HTTPPARA=\"URL\",\"%s\"", url);
    if (!at_send_expect(cmd, "OK", HTTPS_AT_TIMEOUT_MS)) {
        http_log("[HTTP] HTTPS open fail: URL");
        return -1;
    }

    return 0;
}

static int https_probe_total_by_head(uint32_t *total)
{
    unsigned int status = 0U;
    uint32_t length = 0U;

    if (https_http_action(2U, &status, &length) == 0 &&
        (status == 200U || status == 201U || status == 204U) &&
        https_read_header_total(total) == 0) {
        http_logf("[HTTP] HTTPS HEAD total=%lu status=%lu", *total, status, 0U);
        return 0;
    }
    http_logf("[HTTP] HTTPS HEAD failed status=%lu len=%lu", status, length, 0U);
    return -1;
}

static int https_probe_total_by_range(uint32_t *total)
{
    unsigned int status = 0U;
    uint32_t length = 0U;

    if (https_set_range(0U, 0U) != 0)
        return -1;
    if (https_http_action(0U, &status, &length) != 0)
        return -1;
    if (status != 206U || length != 1U) {
        http_logf("[HTTP] HTTPS range probe bad status=%lu len=%lu", status, length, 0U);
        return -1;
    }
    if (https_read_header_total(total) != 0) {
        http_log("[HTTP] HTTPS range header no total");
        return -1;
    }
    if (https_read_chunk(length, https_discard_body, NULL, NULL) != 0) {
        http_log("[HTTP] HTTPS range body discard failed");
        return -1;
    }
    http_logf("[HTTP] HTTPS range total=%lu", *total, 0U, 0U);
    return (*total > 0U) ? 0 : -1;
}

static int https_probe_total_by_get(const char *url, uint32_t *total)
{
    unsigned int status = 0U;
    uint32_t length = 0U;

    if (total == NULL)
        return -1;
    *total = 0U;

    if (https_http_action(0U, &status, &length) == 0 &&
        (status == 200U || status == 201U) &&
        length > 0U) {
        *total = length;
        http_logf("[HTTP] HTTPS GET total=%lu status=%lu", *total, status, 0U);

        /*
         * The full GET may overflow the module's small HTTP cache for OTA bins.
         * Re-open HTTP before range download so stale body data is discarded.
         */
        (void)at_send_expect("AT+HTTPTERM", "OK", 3000U);
        if (https_open_session(url) != 0)
            return -1;
        return 0;
    }

    http_logf("[HTTP] HTTPS GET total failed status=%lu len=%lu", status, length, 0U);
    return -1;
}

static int http_get_to_stream_https_range(const char *url,
                                          uint32_t expectedTotal,
                                          Air780eHttp_BeginCb onBegin,
                                          void *beginCtx,
                                          Air780eHttp_BodyChunkCb onBody,
                                          void *bodyCtx,
                                          void (*onProgress)(uint32_t got, uint32_t total, void *ctx),
                                          void *progressCtx)
{
    uint32_t total = expectedTotal;
    uint32_t got = 0U;
    int ret = -1;

    http_log("[HTTP] HTTPS OTA start");
    http_logf("[HTTP] HTTPS expectedTotal=%lu", expectedTotal, 0U, 0U);

    if (https_open_session(url) != 0) {
        http_log("[HTTP] HTTPS open failed");
        goto out;
    }

    if (total == 0U) {
        if (https_probe_total_by_head(&total) != 0 &&
            https_probe_total_by_range(&total) != 0 &&
            https_probe_total_by_get(url, &total) != 0) {
            http_log("[HTTP] HTTPS total length failed");
            goto out;
        }
    }

    if (total == 0U) {
        http_log("[HTTP] HTTPS total length is zero");
        goto out;
    }
    if (expectedTotal > 0U && total != expectedTotal) {
        http_logf("[HTTP] HTTPS size mismatch got=%lu expect=%lu", total, expectedTotal, 0U);
        goto out;
    }
    http_logf("[HTTP] HTTPS total=%lu chunk=%lu", total, s_httpsChunkMax, 0U);

    while (got < total) {
        uint32_t remain = total - got;
        uint32_t chunk = remain > s_httpsChunkMax ? s_httpsChunkMax : remain;
        uint32_t end = got + chunk - 1U;
        unsigned int status = 0U;
        uint32_t actionLen = 0U;

        http_logf("[HTTP] HTTPS chunk request offset=%lu end=%lu len=%lu", got, end, chunk);

        if (https_set_range(got, end) != 0) {
            http_logf("[HTTP] HTTPS range failed offset=%lu len=%lu", got, chunk, 0U);
            https_reduce_chunk_on_error(got);
            goto out;
        }
        if (https_http_action(0U, &status, &actionLen) != 0) {
            http_logf("[HTTP] HTTPS action failed offset=%lu len=%lu", got, chunk, 0U);
            https_reduce_chunk_on_error(got);
            goto out;
        }

        if (status == 206U) {
            if (actionLen == 0U) {
                http_logf("[HTTP] HTTPS partial len invalid got=%lu expect=%lu status=%lu",
                          actionLen, chunk, status);
                https_reduce_chunk_on_error(got);
                goto out;
            }
            if (actionLen != chunk) {
                http_logf("[HTTP] HTTPS action len differs got=%lu expect=%lu offset=%lu",
                          actionLen, chunk, got);
            }
        } else if (status == 200U && got == 0U && actionLen == total && total <= HTTPS_READ_CHUNK_MAX) {
            chunk = actionLen;
        } else {
            http_logf("[HTTP] HTTPS bad status=%lu len=%lu offset=%lu", status, actionLen, got);
            https_log_error_body(actionLen);
            https_reduce_chunk_on_error(got);
            goto out;
        }

        if (got == 0U && onBegin != NULL && onBegin(total, beginCtx) != 0) {
            http_log("[HTTP] HTTPS begin callback failed");
            goto out;
        }

        {
            uint32_t readLen = 0U;
            if (https_read_chunk(chunk, onBody, bodyCtx, &readLen) != 0) {
                http_logf("[HTTP] HTTPS read failed offset=%lu len=%lu", got, chunk, 0U);
                https_reduce_chunk_on_error(got);
                goto out;
            }
            if (readLen != chunk) {
                http_logf("[HTTP] HTTPS read adjusted got=%lu expect=%lu offset=%lu",
                          readLen, chunk, got);
                chunk = readLen;
            }
        }
        got += chunk;
        if (onProgress != NULL)
            onProgress(got, total, progressCtx);
    }

    ret = (got == total) ? 0 : -1;
    if (ret == 0)
        http_log("[HTTP] HTTPS OTA download done");

out:
    https_close_session((ret == 0) ? 0U : 1U);
    return ret;
}

static int http_get_to_stream_httpex(const char *url,
                                     uint32_t expectedTotal,
                                     Air780eHttp_BeginCb onBegin,
                                     void *beginCtx,
                                     Air780eHttp_BodyChunkCb onBody,
                                     void *bodyCtx,
                                     void (*onProgress)(uint32_t got, uint32_t total, void *ctx),
                                     void *progressCtx)
{
    T_HttpExSignal signal;
    uint32_t total = expectedTotal;
    uint32_t got = 0U;
    uint32_t lastLog = 0U;
    uint32_t noDataRetry = 0U;
    int ret = -1;

    http_log("[HTTP] HTTPEX OTA start");
    http_logf("[HTTP] HTTPEX expectedTotal=%lu", expectedTotal, 0U, 0U);

    if (total == 0U) {
        http_log("[HTTP] HTTPEX skipped: expectedTotal required");
        goto out;
    }

    if (https_open_session(url) != 0) {
        http_log("[HTTP] HTTPEX open failed");
        goto out;
    }

    if (https_ex_start_get(&signal) != 0) {
        http_log("[HTTP] HTTPEX start failed");
        goto out;
    }

    if (onBegin != NULL && onBegin(total, beginCtx) != 0) {
        http_log("[HTTP] HTTPEX begin callback failed");
        goto out;
    }

    while (got < total) {
        uint32_t remain = total - got;
        uint32_t toRead = remain > HTTPEX_READ_CHUNK_MAX ? HTTPEX_READ_CHUNK_MAX : remain;
        uint32_t readLen = 0U;

        /*
         * HTTPEX reports the first readable data with +HTTPEXGET, but following
         * AT+HTTPEXGET reads can be issued continuously until the expected file
         * size is consumed. Waiting for a fresh URC after every chunk can stall.
         */
        if (got == 0U && signal.dataReady == 0U) {
            if (https_ex_wait_signal(0U, &signal, HTTPEX_WAIT_DATA_MS) != 0) {
                http_logf("[HTTP] HTTPEX wait data timeout got=%lu total=%lu", got, total, 0U);
                goto out;
            }
            if (signal.done != 0U && signal.dataReady == 0U) {
                http_logf("[HTTP] HTTPEX done without data status=%lu got=%lu total=%lu",
                          signal.status, got, total);
                goto out;
            }
        }

        signal.dataReady = 0U;
        {
            int readRc = https_ex_read_chunk(toRead, onBody, bodyCtx, &readLen, &signal);
            if (readRc == HTTPS_READ_NO_DATA && noDataRetry < HTTPEX_NO_DATA_RETRY_MAX) {
                noDataRetry++;
                http_logf("[HTTP] HTTPEX no data retry=%lu got=%lu want=%lu",
                          noDataRetry, got, toRead);
                (void)https_ex_wait_signal(0U, &signal, 5000U);
                continue;
            }
            if (readRc != 0) {
                http_logf("[HTTP] HTTPEX read failed got=%lu want=%lu", got, toRead, 0U);
                goto out;
            }
        }
        noDataRetry = 0U;
        if (readLen == 0U || readLen > remain) {
            http_logf("[HTTP] HTTPEX read len invalid got=%lu remain=%lu total=%lu",
                      readLen, remain, total);
            goto out;
        }

        got += readLen;
        if (onProgress != NULL)
            onProgress(got, total, progressCtx);

        if (got == total || (got / 32768U) != (lastLog / 32768U)) {
            http_logf("[HTTP] HTTPEX progress got=%lu total=%lu len=%lu",
                      got, total, readLen);
            lastLog = got;
        }
    }

    if (signal.done == 0U)
        (void)https_ex_wait_signal(0U, &signal, 2000U);
    if (signal.done != 0U &&
        signal.status != 200U &&
        signal.status != 201U &&
        signal.status != 206U) {
        http_logf("[HTTP] HTTPEX bad final status=%lu got=%lu total=%lu",
                  signal.status, got, total);
        goto out;
    }

    ret = (got == total) ? 0 : -1;
    if (ret == 0)
        http_log("[HTTP] HTTPEX OTA download done");

out:
    https_close_session((ret == 0) ? 0U : 1U);
    return ret;
}

static int http_get_to_stream_https(const char *url,
                                    uint32_t expectedTotal,
                                    Air780eHttp_BeginCb onBegin,
                                    void *beginCtx,
                                    Air780eHttp_BodyChunkCb onBody,
                                    void *bodyCtx,
                                    void (*onProgress)(uint32_t got, uint32_t total, void *ctx),
                                    void *progressCtx)
{
    if (s_useHttpEx != 0U) {
        if (http_get_to_stream_httpex(url,
                                      expectedTotal,
                                      onBegin,
                                      beginCtx,
                                      onBody,
                                      bodyCtx,
                                      onProgress,
                                      progressCtx) == 0) {
            return 0;
        }
        http_log("[HTTP] HTTPEX fallback to range");
    }

    return http_get_to_stream_https_range(url,
                                          expectedTotal,
                                          onBegin,
                                          beginCtx,
                                          onBody,
                                          bodyCtx,
                                          onProgress,
                                          progressCtx);
}

int Air780eHttp_GetToStreamEx(const char *url,
                              uint32_t expectedTotal,
                              Air780eHttp_BeginCb onBegin,
                              void *beginCtx,
                              Air780eHttp_BodyChunkCb onBody,
                              void *bodyCtx,
                              void (*onProgress)(uint32_t got, uint32_t total, void *ctx),
                              void *progressCtx)
{
    int ret;

    if (url == NULL || onBody == NULL)
        return -1;

    Air780eMqtt_ModemSessionLock();
    Air780eMqtt_SetHttpDownloadActive(1U);

    Air780eMqtt_Disconnect();
    osDelay(400U);

    if (strncmp(url, "https://", 8U) == 0) {
        ret = http_get_to_stream_https(url,
                                       expectedTotal,
                                       onBegin,
                                       beginCtx,
                                       onBody,
                                       bodyCtx,
                                       onProgress,
                                       progressCtx);
    } else {
        ret = http_get_to_stream_raw(url,
                                     expectedTotal,
                                     onBegin,
                                     beginCtx,
                                     onBody,
                                     bodyCtx,
                                     onProgress,
                                     progressCtx);
    }

    Air780eMqtt_SetHttpDownloadActive(0U);
    Air780eMqtt_ModemSessionUnlock();
    return ret;
}

int Air780eHttp_GetToStream(const char *url,
                            uint32_t expectedTotal,
                            Air780eHttp_BodyChunkCb onBody,
                            void *bodyCtx,
                            void (*onProgress)(uint32_t got, uint32_t total, void *ctx),
                            void *progressCtx)
{
    return Air780eHttp_GetToStreamEx(url,
                                     expectedTotal,
                                     NULL,
                                     NULL,
                                     onBody,
                                     bodyCtx,
                                     onProgress,
                                     progressCtx);
}

typedef struct {
    uint8_t *buf;
    uint32_t cap;
    uint32_t len;
} T_HttpBufferCtx;

static int http_buffer_begin(uint32_t total, void *ctx)
{
    T_HttpBufferCtx *buffer = (T_HttpBufferCtx *)ctx;

    if (buffer == NULL || total > buffer->cap)
        return -1;
    return 0;
}

static int http_buffer_body(const uint8_t *data, uint32_t len, void *ctx)
{
    T_HttpBufferCtx *buffer = (T_HttpBufferCtx *)ctx;

    if (buffer == NULL || data == NULL)
        return -1;
    if (len > (buffer->cap - buffer->len))
        return -1;
    memcpy(buffer->buf + buffer->len, data, len);
    buffer->len += len;
    return 0;
}

int Air780eHttp_GetToBuffer(const char *url, uint8_t *buf, uint32_t bufCap, uint32_t *outLen,
                            uint32_t expectedTotal,
                            void (*onProgress)(uint32_t got, uint32_t total, void *ctx),
                            void *progressCtx)
{
    T_HttpBufferCtx buffer;
    int ret;

    if (url == NULL || buf == NULL || outLen == NULL || bufCap == 0U)
        return -1;

    buffer.buf = buf;
    buffer.cap = bufCap;
    buffer.len = 0U;
    *outLen = 0U;

    ret = Air780eHttp_GetToStreamEx(url,
                                    expectedTotal,
                                    http_buffer_begin,
                                    &buffer,
                                    http_buffer_body,
                                    &buffer,
                                    onProgress,
                                    progressCtx);
    if (ret == 0)
        *outLen = buffer.len;
    return ret;
}
