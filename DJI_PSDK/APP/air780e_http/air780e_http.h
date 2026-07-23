#ifndef AIR780E_HTTP_H
#define AIR780E_HTTP_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int (*Air780eHttp_BodyChunkCb)(const uint8_t *data, uint32_t len, void *ctx);
typedef int (*Air780eHttp_BeginCb)(uint32_t total, void *ctx);

int Air780eHttp_GetToStreamEx(const char *url,
                              uint32_t expectedTotal,
                              Air780eHttp_BeginCb onBegin,
                              void *beginCtx,
                              Air780eHttp_BodyChunkCb onBody,
                              void *bodyCtx,
                              void (*onProgress)(uint32_t got, uint32_t total, void *ctx),
                              void *progressCtx);

int Air780eHttp_GetToStream(const char *url,
                            uint32_t expectedTotal,
                            Air780eHttp_BodyChunkCb onBody,
                            void *bodyCtx,
                            void (*onProgress)(uint32_t got, uint32_t total, void *ctx),
                            void *progressCtx);

int Air780eHttp_GetToBuffer(const char *url, uint8_t *buf, uint32_t bufCap, uint32_t *outLen,
                            uint32_t expectedTotal,
                            void (*onProgress)(uint32_t got, uint32_t total, void *ctx),
                            void *progressCtx);

#ifdef __cplusplus
}
#endif

#endif /* AIR780E_HTTP_H */
