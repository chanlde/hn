#include "serial_ota.h"

#include "solarclean_ota.h"
#include "solarclean_ota_flash.h"
#include "solarclean_ota_hash.h"
#include "ds800_protocol.h"
#include "uart.h"
#include "usbd_cdc_vcp.h"

#include "cmsis_os.h"
#include "stm32h7xx_hal.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SERIAL_OTA_LINE_MAX       320U
#define SERIAL_OTA_SHA256_HEX_LEN 64U
#define SERIAL_OTA_MAX_IMAGE_SIZE (896UL * 1024UL)
#define SERIAL_OTA_PROTOCOL_VERSION 2U

#define SERIAL_OTA_CMD_GET_INFO         "GET_DEVICE_INFO"
#define SERIAL_OTA_CMD_SET_DEVICE_PARAM "SET_DEVICE_PARAM"

#define SERIAL_OTA_MAGIC_0        'S'
#define SERIAL_OTA_MAGIC_1        'C'
#define SERIAL_OTA_MAGIC_2        'O'
#define SERIAL_OTA_MAGIC_3        'T'
#define SERIAL_OTA_FRAME_VERSION  1U
#define SERIAL_OTA_FRAME_HEADER   20U
#define SERIAL_OTA_FRAME_DATA     0x02U
#define SERIAL_OTA_FRAME_END      0x03U
#define SERIAL_OTA_FRAME_ABORT    0x04U
#define SERIAL_OTA_MAX_PAYLOAD    1024U

typedef struct {
    uint8_t magic[4];
    uint8_t version;
    uint8_t type;
    uint16_t headerLen;
    uint32_t seq;
    uint32_t offset;
    uint16_t len;
    uint16_t crc16;
} __attribute__((packed)) T_SerialOtaFrameHeader;

static E_SerialOtaState s_state = SERIAL_OTA_STATE_IDLE;
static uint32_t s_fileSize;
static char s_sha256[SERIAL_OTA_SHA256_HEX_LEN + 1U];
static uint8_t s_expectedHash[SOLARCLEAN_OTA_SHA256_SIZE];
static T_SolarCleanOtaFlashWriter s_writer;
static uint32_t s_expectedSeq;
static uint8_t s_writeReady;

static void serial_ota_debug(const char *s)
{
    if (s == NULL)
        return;

    UART_Write(UART_NUM_1, (const uint8_t *)s, (uint16_t)strlen(s));
    UART_Write(UART_NUM_1, (const uint8_t *)"\r\n", 2U);
}

static void serial_ota_debugf(const char *fmt, ...)
{
    char buf[192];
    va_list ap;
    int n;

    if (fmt == NULL)
        return;

    va_start(ap, fmt);
    n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n <= 0)
        return;
    if (n >= (int)sizeof(buf))
        n = (int)sizeof(buf) - 1;
    buf[n] = '\0';
    serial_ota_debug(buf);
}

static void serial_ota_write(const char *s)
{
    uint32_t realLen;

    if (s == NULL)
        return;

    (void)USBD_CDC_WriteData((const uint8_t *)s, (uint32_t)strlen(s), &realLen);
}

static void serial_ota_json_escape(char *dst, uint32_t dstSize, const char *src)
{
    uint32_t pos = 0U;

    if (dst == NULL || dstSize == 0U)
        return;

    if (src == NULL)
        src = "";

    while (*src != '\0' && pos < (dstSize - 1U)) {
        unsigned char ch = (unsigned char)*src++;

        if (ch == '"' || ch == '\\') {
            if ((pos + 2U) >= dstSize)
                break;
            dst[pos++] = '\\';
            dst[pos++] = (char)ch;
        } else if (ch == '\r') {
            if ((pos + 2U) >= dstSize)
                break;
            dst[pos++] = '\\';
            dst[pos++] = 'r';
        } else if (ch == '\n') {
            if ((pos + 2U) >= dstSize)
                break;
            dst[pos++] = '\\';
            dst[pos++] = 'n';
        } else if (ch < 0x20U) {
            dst[pos++] = '?';
        } else {
            dst[pos++] = (char)ch;
        }
    }

    dst[pos] = '\0';
}

static void serial_ota_build_device_id(char *dst, uint32_t dstSize)
{
    if (dst == NULL || dstSize == 0U)
        return;

    (void)snprintf(dst, dstSize, "%08lX%08lX%08lX",
                   (unsigned long)HAL_GetUIDw0(),
                   (unsigned long)HAL_GetUIDw1(),
                   (unsigned long)HAL_GetUIDw2());
}

static void serial_ota_send_device_info(void)
{
    char reply[768];
    char deviceIdRaw[32];
    char deviceId[40];
    char hardwareVersion[48];
    char firmwareVersion[48];
    char bootSlot[24];
    char otaStatus[32];
    char lastResult[32];
    char lastFailReason[80];
    char network[32];

    serial_ota_build_device_id(deviceIdRaw, sizeof(deviceIdRaw));
    serial_ota_json_escape(deviceId, sizeof(deviceId), deviceIdRaw);
    serial_ota_json_escape(hardwareVersion, sizeof(hardwareVersion), SolarCleanOta_GetHardwareVersion());
    serial_ota_json_escape(firmwareVersion, sizeof(firmwareVersion), SolarCleanOta_GetFirmwareVersion());
    serial_ota_json_escape(bootSlot, sizeof(bootSlot), SolarCleanOta_GetSlot());
    serial_ota_json_escape(otaStatus, sizeof(otaStatus), SolarCleanOta_GetStatus());
    serial_ota_json_escape(lastResult, sizeof(lastResult), SolarCleanOta_GetLastResult());
    serial_ota_json_escape(lastFailReason, sizeof(lastFailReason), SolarCleanOta_GetLastFailReason());
    serial_ota_json_escape(network, sizeof(network), SolarCleanOta_GetNetwork());

    (void)snprintf(reply, sizeof(reply),
                   "{\"type\":\"deviceInfo\",\"ok\":true,"
                   "\"protocol\":\"genericSerialOta\",\"protocolVersion\":%u,"
                   "\"productId\":\"SolarClean\",\"productCode\":\"SolarClean\","
                   "\"productName\":\"SolarClean\",\"productType\":\"cleaner\","
                   "\"model\":\"FC100\",\"board\":\"STM32H743VI\",\"deviceId\":\"%s\","
                   "\"serialNumber\":\"%s\",\"mcuUid\":\"%s\","
                   "\"hardwareVersion\":\"%s\",\"firmwareVersion\":\"%s\","
                   "\"innerVersion\":%lu,\"bootloaderVersion\":\"\","
                   "\"bootSlot\":\"%s\",\"otaStatus\":\"%s\","
                   "\"lastResult\":\"%s\",\"lastFailReason\":\"%s\","
                   "\"network\":\"%s\",\"slotBSize\":%lu,\"maxChunk\":%u,"
                   "\"extParams\":{\"failsafeHoldEnabled\":%s,"
                   "\"remoteControlEnabled\":%s,"
                   "\"fourGEnabled\":%s,\"psdkEnabled\":%s},"
                   "\"capabilities\":[\"serial_ota\",\"device_params\"]}\r\n",
                   (unsigned)SERIAL_OTA_PROTOCOL_VERSION,
                   deviceId,
                   deviceId,
                   deviceId,
                   hardwareVersion,
                   firmwareVersion,
                   (unsigned long)SolarCleanOta_GetInnerVersion(),
                   bootSlot,
                   otaStatus,
                   lastResult,
                   lastFailReason,
                   network,
                   (unsigned long)SolarCleanOtaFlash_GetSlotBSize(),
                   (unsigned)SERIAL_OTA_MAX_PAYLOAD,
                   Ds800Protocol_GetFailsafeHoldEnabled() ? "true" : "false",
                   Ds800Protocol_GetRemoteControlEnabled() ? "true" : "false",
                   Ds800Protocol_GetFourGEnabled() ? "true" : "false",
                   Ds800Protocol_GetPsdkEnabled() ? "true" : "false");
    serial_ota_write(reply);
    serial_ota_debugf("[SERIAL_OTA] device info sent fw=%s inner=%lu params=%u/%u/%u/%u",
                      SolarCleanOta_GetFirmwareVersion(),
                      (unsigned long)SolarCleanOta_GetInnerVersion(),
                      (unsigned)Ds800Protocol_GetFailsafeHoldEnabled(),
                      (unsigned)Ds800Protocol_GetRemoteControlEnabled(),
                      (unsigned)Ds800Protocol_GetFourGEnabled(),
                      (unsigned)Ds800Protocol_GetPsdkEnabled());
}

static void serial_ota_send_device_params(void)
{
    char reply[240];

    (void)snprintf(reply, sizeof(reply),
                   "{\"type\":\"deviceParams\",\"ok\":true,"
                   "\"extParams\":{\"failsafeHoldEnabled\":%s,"
                   "\"remoteControlEnabled\":%s,"
                   "\"fourGEnabled\":%s,\"psdkEnabled\":%s}}\r\n",
                   Ds800Protocol_GetFailsafeHoldEnabled() ? "true" : "false",
                   Ds800Protocol_GetRemoteControlEnabled() ? "true" : "false",
                   Ds800Protocol_GetFourGEnabled() ? "true" : "false",
                   Ds800Protocol_GetPsdkEnabled() ? "true" : "false");
    serial_ota_write(reply);
}

static void serial_ota_status(const char *state, uint32_t progress, uint32_t written)
{
    char reply[160];

    if (state == NULL)
        state = "unknown";

    (void)snprintf(reply, sizeof(reply),
                   "{\"type\":\"otaSerialStatus\",\"state\":\"%s\",\"progress\":%lu,\"written\":%lu,\"size\":%lu}\r\n",
                   state,
                   (unsigned long)progress,
                   (unsigned long)written,
                   (unsigned long)s_fileSize);
    serial_ota_write(reply);
    serial_ota_debugf("[SERIAL_OTA] state=%s progress=%lu written=%lu size=%lu",
                      state,
                      (unsigned long)progress,
                      (unsigned long)written,
                      (unsigned long)s_fileSize);
}

static void serial_ota_reply_error(uint32_t code, const char *msg)
{
    char reply[160];

    if (msg == NULL)
        msg = "error";

    (void)snprintf(reply, sizeof(reply),
                   "{\"type\":\"otaSerialAck\",\"ok\":false,\"code\":%lu,\"msg\":\"%s\"}\r\n",
                   (unsigned long)code, msg);
    serial_ota_write(reply);
    serial_ota_debugf("[SERIAL_OTA] error code=%lu msg=%s", (unsigned long)code, msg);
}

static uint16_t serial_ota_crc16(const uint8_t *data, uint32_t len)
{
    uint16_t crc = 0xFFFFU;

    while (len-- > 0U) {
        crc ^= (uint16_t)(*data++);
        for (uint32_t i = 0U; i < 8U; i++) {
            if ((crc & 1U) != 0U)
                crc = (uint16_t)((crc >> 1U) ^ 0xA001U);
            else
                crc >>= 1U;
        }
    }

    return crc;
}

static uint16_t serial_ota_get_le16(const uint8_t *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8U);
}

static uint32_t serial_ota_get_le32(const uint8_t *p)
{
    return (uint32_t)p[0] |
           ((uint32_t)p[1] << 8U) |
           ((uint32_t)p[2] << 16U) |
           ((uint32_t)p[3] << 24U);
}

static int serial_ota_is_hex_sha256(const char *s)
{
    if (s == NULL || strlen(s) != SERIAL_OTA_SHA256_HEX_LEN)
        return 0;

    for (uint32_t i = 0U; i < SERIAL_OTA_SHA256_HEX_LEN; i++) {
        if (!isxdigit((unsigned char)s[i]))
            return 0;
    }

    return 1;
}

static char *serial_ota_trim(char *s)
{
    char *end;

    while (*s != '\0' && isspace((unsigned char)*s))
        s++;

    end = s + strlen(s);
    while (end > s && isspace((unsigned char)end[-1]))
        *--end = '\0';

    return s;
}

static int serial_ota_parse_bool_value(const char *value, uint8_t *out)
{
    if (value == NULL || out == NULL)
        return 0;

    if (strcmp(value, "1") == 0 || strcmp(value, "on") == 0 || strcmp(value, "true") == 0) {
        *out = 1U;
        return 1;
    }
    if (strcmp(value, "0") == 0 || strcmp(value, "off") == 0 || strcmp(value, "false") == 0) {
        *out = 0U;
        return 1;
    }
    return 0;
}

static int serial_ota_next_token(char **cursor, char **token)
{
    char *p;

    if (cursor == NULL || token == NULL || *cursor == NULL)
        return 0;

    p = serial_ota_trim(*cursor);
    if (*p == '\0')
        return 0;

    *token = p;
    while (*p != '\0' && !isspace((unsigned char)*p))
        p++;
    if (*p != '\0')
        *p++ = '\0';
    *cursor = p;
    return 1;
}

static int serial_ota_parse_start(char *line, uint32_t *sizeOut, char shaOut[SERIAL_OTA_SHA256_HEX_LEN + 1U])
{
    char *p;
    char *sizeStr;
    char *shaStr;
    char *end;
    unsigned long size;

    if (line == NULL || sizeOut == NULL || shaOut == NULL)
        return 0;

    p = serial_ota_trim(line);
    if (strncmp(p, "START_OTA_SERIAL", 16) != 0)
        return 0;

    p += 16;
    p = serial_ota_trim(p);

    if (*p == '(') {
        p++;
        sizeStr = serial_ota_trim(p);
        shaStr = strchr(sizeStr, ',');
        if (shaStr == NULL)
            return 0;
        *shaStr++ = '\0';
        shaStr = serial_ota_trim(shaStr);
        end = strchr(shaStr, ')');
        if (end == NULL)
            return 0;
        *end = '\0';
    } else {
        sizeStr = p;
        shaStr = strpbrk(sizeStr, " \t");
        if (shaStr == NULL)
            return 0;
        *shaStr++ = '\0';
        shaStr = serial_ota_trim(shaStr);
    }

    sizeStr = serial_ota_trim(sizeStr);
    size = strtoul(sizeStr, &end, 10);
    if (end == sizeStr || *serial_ota_trim(end) != '\0')
        return 0;
    if (size == 0UL || size > SERIAL_OTA_MAX_IMAGE_SIZE)
        return 0;
    if (!serial_ota_is_hex_sha256(shaStr))
        return 0;

    *sizeOut = (uint32_t)size;
    memcpy(shaOut, shaStr, SERIAL_OTA_SHA256_HEX_LEN);
    shaOut[SERIAL_OTA_SHA256_HEX_LEN] = '\0';
    return 1;
}

static int serial_ota_begin_write(uint32_t size, const char *sha)
{
    if (SolarCleanOtaHash_ParseHex(sha, s_expectedHash) != 0)
        return -1;

    serial_ota_status("erasing", 0U, 0U);
    if (SolarCleanOtaFlash_Begin(&s_writer, SOLARCLEAN_OTA_SLOT_B, size) != SOLARCLEAN_OTA_FLASH_OK)
        return -1;

    s_expectedSeq = 0U;
    s_writeReady = 1U;
    serial_ota_status("ready", 0U, 0U);
    return 0;
}

static int serial_ota_handle_line(char *line)
{
    uint32_t size = 0U;
    char sha[SERIAL_OTA_SHA256_HEX_LEN + 1U];
    char reply[256];
    char *cmd = serial_ota_trim(line);

    if (strcmp(cmd, SERIAL_OTA_CMD_GET_INFO) == 0) {
        serial_ota_send_device_info();
        return 0;
    }

    if (strncmp(cmd, SERIAL_OTA_CMD_SET_DEVICE_PARAM, strlen(SERIAL_OTA_CMD_SET_DEVICE_PARAM)) == 0) {
        char *cursor = cmd + strlen(SERIAL_OTA_CMD_SET_DEVICE_PARAM);
        char *key;
        char *value;
        uint8_t enabled;

        if (!serial_ota_next_token(&cursor, &key) ||
            !serial_ota_next_token(&cursor, &value) ||
            *serial_ota_trim(cursor) != '\0') {
            serial_ota_reply_error(400U, "bad device param command");
            return 0;
        }

        if (!serial_ota_parse_bool_value(value, &enabled)) {
            serial_ota_reply_error(400U, "bad device param value");
            return 0;
        }

        if (strcmp(key, "failsafeHoldEnabled") == 0) {
            if (Ds800Protocol_SetFailsafeHoldEnabled(enabled, 1U) != 0) {
                serial_ota_reply_error(500U, "device param save failed");
                return 0;
            }
        } else if (strcmp(key, "remoteControlEnabled") == 0) {
            if (Ds800Protocol_SetRemoteControlEnabled(enabled, 1U) != 0) {
                serial_ota_reply_error(500U, "device param save failed");
                return 0;
            }
        } else if (strcmp(key, "fourGEnabled") == 0) {
            if (Ds800Protocol_SetFourGEnabled(enabled, 1U) != 0) {
                serial_ota_reply_error(500U, "device param save failed");
                return 0;
            }
        } else if (strcmp(key, "psdkEnabled") == 0) {
            if (Ds800Protocol_SetPsdkEnabled(enabled, 1U) != 0) {
                serial_ota_reply_error(500U, "device param save failed");
                return 0;
            }
        } else {
            serial_ota_reply_error(404U, "unknown device param");
            return 0;
        }

        if (strcmp(key, "psdkEnabled") == 0) {
            serial_ota_debug("[SERIAL_OTA] psdkEnabled changes take effect after reboot");
        }

        if (strcmp(key, "fourGEnabled") == 0) {
            serial_ota_debug("[SERIAL_OTA] fourGEnabled changes apply in MQTT service loop");
        }

        if (strcmp(key, "remoteControlEnabled") == 0 && !enabled) {
            serial_ota_debug("[SERIAL_OTA] remote control disabled and outputs stopped");
        }

        serial_ota_send_device_params();
        return 0;
    }

    if (strcmp(cmd, "ENTER_OTA_SERIAL") == 0) {
        s_state = SERIAL_OTA_STATE_ACTIVE;
        serial_ota_write("{\"type\":\"otaSerialReady\",\"ok\":true,\"mode\":\"serial_ota\"}\r\n");
        serial_ota_debug("[SERIAL_OTA] entered ota standby mode");
        return 1;
    }

    if (!serial_ota_parse_start(cmd, &size, sha)) {
        serial_ota_reply_error(400U, "bad start command");
        return 0;
    }

    serial_ota_debugf("[SERIAL_OTA] start command size=%lu sha=%s",
                      (unsigned long)size, sha);
    s_fileSize = size;
    memcpy(s_sha256, sha, sizeof(s_sha256));
    s_state = SERIAL_OTA_STATE_ACTIVE;

    if (serial_ota_begin_write(size, sha) != 0) {
        s_state = SERIAL_OTA_STATE_IDLE;
        serial_ota_reply_error(500U, "slot b erase/begin failed");
        return 0;
    }

    (void)snprintf(reply, sizeof(reply),
                   "{\"type\":\"otaSerialAck\",\"ok\":true,\"mode\":\"serial_ota\",\"size\":%lu,\"sha256\":\"%s\",\"chunk\":%u}\r\n",
                   (unsigned long)s_fileSize, s_sha256, (unsigned)SERIAL_OTA_MAX_PAYLOAD);
    serial_ota_write(reply);
    serial_ota_debug("[SERIAL_OTA] start ack sent, waiting chunks");
    return 1;
}

static void serial_ota_send_chunk_ack(uint32_t seq, uint32_t nextOffset)
{
    char reply[160];

    (void)snprintf(reply, sizeof(reply),
                   "{\"type\":\"otaSerialChunkAck\",\"ok\":true,\"seq\":%lu,\"nextOffset\":%lu}\r\n",
                   (unsigned long)seq, (unsigned long)nextOffset);
    serial_ota_write(reply);
}

static void serial_ota_send_chunk_error(uint32_t seq, uint32_t code, const char *msg, uint32_t nextOffset)
{
    char reply[192];

    if (msg == NULL)
        msg = "chunk error";

    (void)snprintf(reply, sizeof(reply),
                   "{\"type\":\"otaSerialChunkAck\",\"ok\":false,\"seq\":%lu,\"code\":%lu,\"msg\":\"%s\",\"nextOffset\":%lu}\r\n",
                   (unsigned long)seq, (unsigned long)code, msg, (unsigned long)nextOffset);
    serial_ota_write(reply);
}

static void serial_ota_finalize_and_reset(uint32_t seq)
{
    T_SolarCleanOtaStartRequest req;
    T_SolarCleanOtaAck ack;
    char reply[192];

    serial_ota_status("finalizing", 90U, s_writer.writtenSize);
    if (SolarCleanOtaFlash_End(&s_writer) != SOLARCLEAN_OTA_FLASH_OK) {
        serial_ota_send_chunk_error(seq, 500U, "flash end failed", s_writer.writtenSize);
        return;
    }

    serial_ota_status("verifying", 95U, s_writer.writtenSize);
    if (SolarCleanOtaHash_VerifySlotB(s_fileSize, s_expectedHash) != 0) {
        serial_ota_send_chunk_error(seq, 500U, "sha256 verify failed", s_writer.writtenSize);
        return;
    }

    memset(&req, 0, sizeof(req));
    memset(&ack, 0, sizeof(ack));
    (void)snprintf(req.targetVersion, sizeof(req.targetVersion), "serial");
    req.targetInnerVersion = SolarCleanOta_GetInnerVersion() + 1U;
    (void)snprintf(req.hardwareVersion, sizeof(req.hardwareVersion), "%s", SolarCleanOta_GetHardwareVersion());
    req.fileSize = s_fileSize;
    (void)snprintf(req.sha256, sizeof(req.sha256), "%s", s_sha256);

    serial_ota_status("committing", 98U, s_writer.writtenSize);
    if (SolarCleanOta_CommitSlotBForReboot(&req, &ack, NULL, NULL) != 0) {
        (void)snprintf(reply, sizeof(reply),
                       "{\"type\":\"otaSerialDone\",\"ok\":false,\"code\":%d,\"msg\":\"%s\"}\r\n",
                       ack.code, ack.msg);
        serial_ota_write(reply);
        serial_ota_debugf("[SERIAL_OTA] commit failed code=%d msg=%s", ack.code, ack.msg);
        return;
    }

    s_writeReady = 0U;
    (void)snprintf(reply, sizeof(reply),
                   "{\"type\":\"otaSerialDone\",\"ok\":true,\"msg\":\"pending reboot\",\"size\":%lu}\r\n",
                   (unsigned long)s_fileSize);
    serial_ota_write(reply);
    serial_ota_status("rebooting", 100U, s_fileSize);
    osDelay(300U);
    NVIC_SystemReset();
}

static void serial_ota_handle_frame(const T_SerialOtaFrameHeader *hdr, const uint8_t *payload)
{
    uint32_t nextOffset = s_writer.writtenSize;

    if (hdr == NULL)
        return;

    if (s_writeReady == 0U) {
        serial_ota_send_chunk_error(hdr->seq, 409U, "ota not ready", nextOffset);
        return;
    }

    if (hdr->type == SERIAL_OTA_FRAME_ABORT) {
        SolarCleanOtaFlash_Abort(&s_writer);
        s_writeReady = 0U;
        serial_ota_write("{\"type\":\"otaSerialDone\",\"ok\":false,\"code\":499,\"msg\":\"aborted\"}\r\n");
        serial_ota_debug("[SERIAL_OTA] aborted by host");
        return;
    }

    if (hdr->type == SERIAL_OTA_FRAME_END) {
        if (s_writer.writtenSize != s_fileSize) {
            serial_ota_send_chunk_error(hdr->seq, 409U, "size mismatch", s_writer.writtenSize);
            return;
        }
        serial_ota_finalize_and_reset(hdr->seq);
        return;
    }

    if (hdr->type != SERIAL_OTA_FRAME_DATA) {
        serial_ota_send_chunk_error(hdr->seq, 400U, "bad frame type", nextOffset);
        return;
    }

    if (hdr->seq != s_expectedSeq) {
        serial_ota_send_chunk_error(hdr->seq, 409U, "seq mismatch", nextOffset);
        return;
    }

    if (hdr->offset != s_writer.writtenSize || hdr->offset >= s_fileSize ||
        hdr->len == 0U || hdr->len > SERIAL_OTA_MAX_PAYLOAD ||
        hdr->len > (s_fileSize - hdr->offset)) {
        serial_ota_send_chunk_error(hdr->seq, 409U, "offset/len mismatch", nextOffset);
        return;
    }

    if (serial_ota_crc16(payload, hdr->len) != hdr->crc16) {
        serial_ota_send_chunk_error(hdr->seq, 422U, "crc mismatch", nextOffset);
        return;
    }

    if (SolarCleanOtaFlash_Write(&s_writer, payload, hdr->len) != SOLARCLEAN_OTA_FLASH_OK) {
        serial_ota_send_chunk_error(hdr->seq, 500U, "flash write failed", nextOffset);
        return;
    }

    if ((hdr->seq % 16U) == 0U || s_writer.writtenSize == s_fileSize) {
        uint32_t progress = (s_fileSize == 0U) ? 0U : (s_writer.writtenSize * 90U) / s_fileSize;
        if (progress > 90U)
            progress = 90U;
        serial_ota_status("writing", progress, s_writer.writtenSize);
    }
    s_expectedSeq++;
    serial_ota_send_chunk_ack(hdr->seq, s_writer.writtenSize);
}

void SerialOta_Init(void)
{
    s_state = SERIAL_OTA_STATE_IDLE;
    s_fileSize = 0U;
    s_sha256[0] = '\0';
    memset(s_expectedHash, 0, sizeof(s_expectedHash));
    memset(&s_writer, 0, sizeof(s_writer));
    s_expectedSeq = 0U;
    s_writeReady = 0U;
}

int SerialOta_WaitEnterWindow(uint32_t timeoutMs)
{
    uint8_t ch;
    uint32_t realLen;
    uint32_t pos = 0U;
    char line[SERIAL_OTA_LINE_MAX];
    uint32_t startTick = osKernelGetTickCount();
    uint32_t timeoutTicks = (timeoutMs * osKernelGetTickFreq()) / 1000U;

    if (timeoutTicks == 0U)
        timeoutTicks = 1U;

    serial_ota_debugf("[SERIAL_OTA] wait enter window %lu ms", (unsigned long)timeoutMs);
    while ((osKernelGetTickCount() - startTick) < timeoutTicks) {
        realLen = 0U;
        (void)USBD_CDC_ReadData(&ch, 1U, &realLen);
        if (realLen == 1U) {
            if (ch == '\r' || ch == '\n') {
                if (pos > 0U) {
                    line[pos] = '\0';
                    serial_ota_debugf("[SERIAL_OTA] rx line: %s", line);
                    if (serial_ota_handle_line(line))
                        return 1;
                    pos = 0U;
                }
            } else if (pos < (SERIAL_OTA_LINE_MAX - 1U)) {
                line[pos++] = (char)ch;
            } else {
                pos = 0U;
                serial_ota_reply_error(413U, "line too long");
            }
        } else {
            osDelay(10U);
        }
    }

    return 0;
}

void SerialOta_ServiceLoop(void)
{
    enum {
        PARSER_SYNC = 0,
        PARSER_HEADER,
        PARSER_PAYLOAD,
    } parserState = PARSER_SYNC;
    uint8_t headerBuf[SERIAL_OTA_FRAME_HEADER];
    uint8_t payload[SERIAL_OTA_MAX_PAYLOAD];
    uint32_t headerPos = 0U;
    uint32_t payloadPos = 0U;
    uint32_t linePos = 0U;
    char line[SERIAL_OTA_LINE_MAX];
    T_SerialOtaFrameHeader hdr;

    for (;;) {
        uint8_t ch;
        uint32_t realLen = 0U;

        (void)USBD_CDC_ReadData(&ch, 1U, &realLen);
        if (realLen != 1U) {
            osDelay(1U);
            continue;
        }

        if (s_writeReady == 0U) {
            if (ch == '\r' || ch == '\n') {
                if (linePos > 0U) {
                    line[linePos] = '\0';
                    serial_ota_debugf("[SERIAL_OTA] service rx line: %s", line);
                    (void)serial_ota_handle_line(line);
                    linePos = 0U;
                }
            } else if (linePos < (SERIAL_OTA_LINE_MAX - 1U)) {
                line[linePos++] = (char)ch;
            } else {
                linePos = 0U;
                serial_ota_reply_error(413U, "line too long");
            }
            continue;
        }

        if (parserState == PARSER_SYNC) {
            const uint8_t magic[4] = {
                SERIAL_OTA_MAGIC_0,
                SERIAL_OTA_MAGIC_1,
                SERIAL_OTA_MAGIC_2,
                SERIAL_OTA_MAGIC_3,
            };

            if (ch == magic[headerPos]) {
                headerBuf[headerPos++] = ch;
                if (headerPos == 4U)
                    parserState = PARSER_HEADER;
            } else {
                headerPos = (ch == magic[0]) ? 1U : 0U;
                if (headerPos == 1U)
                    headerBuf[0] = ch;
            }
            continue;
        }

        if (parserState == PARSER_HEADER) {
            headerBuf[headerPos++] = ch;
            if (headerPos < SERIAL_OTA_FRAME_HEADER)
                continue;

            hdr.magic[0] = headerBuf[0];
            hdr.magic[1] = headerBuf[1];
            hdr.magic[2] = headerBuf[2];
            hdr.magic[3] = headerBuf[3];
            hdr.version = headerBuf[4];
            hdr.type = headerBuf[5];
            hdr.headerLen = serial_ota_get_le16(&headerBuf[6]);
            hdr.seq = serial_ota_get_le32(&headerBuf[8]);
            hdr.offset = serial_ota_get_le32(&headerBuf[12]);
            hdr.len = serial_ota_get_le16(&headerBuf[16]);
            hdr.crc16 = serial_ota_get_le16(&headerBuf[18]);

            if (hdr.version != SERIAL_OTA_FRAME_VERSION ||
                hdr.headerLen != SERIAL_OTA_FRAME_HEADER ||
                hdr.len > SERIAL_OTA_MAX_PAYLOAD) {
                headerPos = 0U;
                parserState = PARSER_SYNC;
                serial_ota_send_chunk_error(hdr.seq, 400U, "bad frame header", s_writer.writtenSize);
                continue;
            }

            payloadPos = 0U;
            if (hdr.len == 0U) {
                serial_ota_handle_frame(&hdr, payload);
                headerPos = 0U;
                parserState = PARSER_SYNC;
            } else {
                parserState = PARSER_PAYLOAD;
            }
            continue;
        }

        payload[payloadPos++] = ch;
        if (payloadPos >= hdr.len) {
            serial_ota_handle_frame(&hdr, payload);
            headerPos = 0U;
            payloadPos = 0U;
            parserState = PARSER_SYNC;
        }
    }
}

E_SerialOtaState SerialOta_GetState(void)
{
    return s_state;
}
