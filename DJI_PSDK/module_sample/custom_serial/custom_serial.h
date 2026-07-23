#ifndef CUSTOM_SERIAL_H
#define CUSTOM_SERIAL_H

#include <dji_core.h>
#include <dji_logger.h>
#include <dji_platform.h>
#include "utils/util_misc.h"

#ifdef __cplusplus
extern "C" {
#endif

#pragma pack(push, 1)
typedef struct {
    uint8_t droneId;
    int32_t latitude_1e6;
    int32_t longitude_1e6;
    uint16_t height_1e2;
    uint16_t speed_1e2;
    int16_t yaw_1e2;
    int16_t pitch_1e2;
    int16_t roll_1e2;
    uint8_t satelliteCount;
    uint16_t battery_1e2;
    uint8_t currentWaypoint;
    uint8_t waterLevelStatus;
} T_CustomSerialDroneStatus;
#pragma pack(pop)

void CustomSerial_GetLastDroneStatus(T_CustomSerialDroneStatus *out);

/* Initializes the MCU status snapshot path. Route/KMZ storage is disabled. */
T_DjiReturnCode CustomSerial_StorageInit(void);

/* Legacy route APIs retained for compatibility; they do not touch Flash. */
#define CUSTOM_ROUTE_SLOT_COUNT 5U
T_DjiReturnCode RouteSlot_Execute(uint8_t slotIdx);
uint32_t CustomSerial_GetRouteSlotLen(uint8_t slotIdx);
void CustomSerial_GetRouteSlotsSummary(char *buf, uint32_t bufSize);
T_DjiReturnCode CustomSerial_EraseRouteSlotForMqtt(uint8_t slotIdx);
T_DjiReturnCode CustomSerial_WriteKmzToSlot(uint8_t slotIdx, const uint8_t *data, uint32_t len);

T_DjiReturnCode ServoSwing_Start(void);
T_DjiReturnCode ServoSwing_Stop(void);

#ifdef __cplusplus
}
#endif

#endif /* CUSTOM_SERIAL_H */
