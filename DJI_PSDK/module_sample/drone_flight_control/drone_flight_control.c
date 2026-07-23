/**
 * @file drone_flight_control.c
 */
#include "drone_flight_control.h"
#include "dji_logger.h"

T_DjiReturnCode DroneFlightControl_Init(double latitude, double longitude, double altitude)
{
    (void)latitude;
    (void)longitude;
    (void)altitude;
    USER_LOG_INFO("[DroneFC] disabled");
    return DJI_ERROR_SYSTEM_MODULE_CODE_NONSUPPORT;
}

int DroneFlightControl_Takeoff(void)
{
    USER_LOG_WARN("[DroneFC] Takeoff disabled");
    return -1;
}

int DroneFlightControl_GoHome(void)
{
    USER_LOG_WARN("[DroneFC] GoHome disabled");
    return -1;
}
