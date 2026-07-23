#ifndef SERIAL_OTA_H
#define SERIAL_OTA_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef SERIAL_OTA_ENTER_WINDOW_MS
#define SERIAL_OTA_ENTER_WINDOW_MS 10000U
#endif

typedef enum {
    SERIAL_OTA_STATE_IDLE = 0,
    SERIAL_OTA_STATE_ACTIVE = 1,
} E_SerialOtaState;

void SerialOta_Init(void);
int SerialOta_WaitEnterWindow(uint32_t timeoutMs);
void SerialOta_ServiceLoop(void);
E_SerialOtaState SerialOta_GetState(void);

#ifdef __cplusplus
}
#endif

#endif
