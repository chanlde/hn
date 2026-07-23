#ifndef BOOT_PARTITION_H
#define BOOT_PARTITION_H

#include <stdint.h>

#define BOOT_FLASH_BASE             0x08000000UL
#define BOOT_FLASH_SIZE             0x00200000UL
#define BOOT_FLASH_END              (BOOT_FLASH_BASE + BOOT_FLASH_SIZE - 1UL)

#define BOOTLOADER_BASE             0x08000000UL
#define BOOTLOADER_SIZE             0x00020000UL

#define APP_SLOT_A_BASE             0x08020000UL
#define APP_SLOT_A_SIZE             0x000A0000UL
#define APP_SLOT_A_END              (APP_SLOT_A_BASE + APP_SLOT_A_SIZE - 1UL)

#define APP_SLOT_B_BASE             0x080C0000UL
#define APP_SLOT_B_SIZE             0x000A0000UL
#define APP_SLOT_B_END              (APP_SLOT_B_BASE + APP_SLOT_B_SIZE - 1UL)

#define ROUTE_STORAGE_BASE          0x08160000UL
#define ROUTE_STORAGE_SIZE          0x00080000UL
#define ROUTE_STORAGE_END           (ROUTE_STORAGE_BASE + ROUTE_STORAGE_SIZE - 1UL)

#define OTA_STATE_BASE              0x081E0000UL
#define OTA_STATE_SIZE              0x00020000UL
#define OTA_STATE_END               (OTA_STATE_BASE + OTA_STATE_SIZE - 1UL)

#define BOOT_SRAM_BASE              0x20000000UL
#define BOOT_SRAM_SIZE              0x00020000UL
#define BOOT_SRAM_END               (BOOT_SRAM_BASE + BOOT_SRAM_SIZE)

#define BOOT_AXI_SRAM_BASE          0x24000000UL
#define BOOT_AXI_SRAM_SIZE          0x00080000UL
#define BOOT_AXI_SRAM_END           (BOOT_AXI_SRAM_BASE + BOOT_AXI_SRAM_SIZE)

#endif /* BOOT_PARTITION_H */
