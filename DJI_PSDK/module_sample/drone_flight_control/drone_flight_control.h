/**
 * @file drone_flight_control.h
 * @brief RTOS 占位：起飞/返航可后续接 PSDK flight_control sample。
 */
#ifndef DRONE_FLIGHT_CONTROL_H
#define DRONE_FLIGHT_CONTROL_H

#include "dji_typedef.h"

T_DjiReturnCode DroneFlightControl_Init(double latitude, double longitude, double altitude);
int DroneFlightControl_Takeoff(void);
int DroneFlightControl_GoHome(void);

#endif
