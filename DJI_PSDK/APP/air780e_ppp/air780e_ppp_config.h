#ifndef AIR780E_PPP_CONFIG_H
#define AIR780E_PPP_CONFIG_H

#include "mqtt_app_config.h"

/*
 * SolarClean Air780E PPP configuration.
 * UART_NUM_7 keeps the existing SolarClean modem pins and BSP mapping.
 */
#define AIR780E_PPP_APN                   "cmnet"
#define AIR780E_PPP_DIAL_NUMBER           "*99#"

#define AIR780E_PPP_UART_BOOT_BAUD        115200U
#define AIR780E_PPP_UART_DATA_BAUD        460800U
#define AIR780E_PPP_UART_HIGH_BAUD_ENABLE 1
#define AIR780E_PPP_POWER_ON_DELAY_MS     8000U

#define AIR780E_PPP_REG_WAIT_MS           120000U
#define AIR780E_PPP_AT_RETRY_DELAY_MS     1000U
#define AIR780E_PPP_MQTT_RETRY_DELAY_MS   10000U
#define AIR780E_PPP_MQTT_FAIL_RESET_MAX   6U

#define AIR780E_MQTT_BROKER_HOST          MQTT_BROKER_IP
#define AIR780E_MQTT_BROKER_PORT          MQTT_BROKER_PORT
#define AIR780E_MQTT_USERNAME             MQTT_USERNAME
#define AIR780E_MQTT_PASSWORD             MQTT_PASSWORD

#ifndef SPEAKER_LOG_PPP_VERBOSE
#define SPEAKER_LOG_PPP_VERBOSE           0
#endif
#ifndef SPEAKER_LOG_PPP_AT_RX
#define SPEAKER_LOG_PPP_AT_RX             0
#endif
#ifndef SPEAKER_LOG_PPP_AT_CMD
#define SPEAKER_LOG_PPP_AT_CMD            0
#endif
#ifndef SPEAKER_LOG_PPP_ONLINE_STATS
#define SPEAKER_LOG_PPP_ONLINE_STATS      0
#endif

#endif /* AIR780E_PPP_CONFIG_H */
