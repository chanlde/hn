# HnPsdk / SolarClean

STM32H7 carrier-board firmware project with DJI PSDK integration, 4G/MQTT services, bootloader OTA support, and a standalone serial firmware updater.

## Project Layout

| Path | Purpose |
| --- | --- |
| `APP/` | STM32 application source and Keil project |
| `Bootloader/` | Bootloader source and Keil project |
| `BSP/` | Board support code and shared configuration |
| `DJI_PSDK/` | DJI PSDK library, samples, and SolarClean application modules |
| `docs/` | Protocol, flashing, OTA, and verification documents |
| `serial_ota/` | Customer-facing serial firmware upgrade tool |
| `tools/` | Build/export helper scripts |

## Main Entry Points

- Application project: `APP/MDK-ARM/APP.uvprojx`
- Bootloader project: `Bootloader/MDK-ARM/Bootloader.uvprojx`
- Serial OTA protocol: `docs/serial_ota_protocol.md`
- Serial updater source: `serial_ota/main.py`
- Serial updater executable: `serial_ota/SolarCleanFirmwareUpdater.exe`

## Release Notes

### 2026-06-22 - V0.1.10 / inner 20

- Added Flash-backed extension switches for remote control, 4G, and PSDK.
- The serial PC tool can now control `remoteControlEnabled`, `fourGEnabled`, and `psdkEnabled` through `SET_DEVICE_PARAM <name> 0|1`.
- 4G enable/disable is applied at runtime by powering AIR780E on/off and gating MQTT publishes; remote control disable stops remote-driven pump/swing output and ignores SBUS input until re-enabled.
- PSDK enable/disable is saved in Flash and applied on the next boot to avoid unsafe runtime PSDK teardown.

### 2026-06-22 - V0.1.9 / inner 19

- Added a Flash-backed DS800 failsafe hold switch controlled from the serial PC tool.
- When the switch is enabled, SBUS lost/failsafe no longer stops the current pump and swing output; when disabled, the original stop-on-failsafe behavior remains.
- Added serial `GET_DEVICE_INFO` extended parameters and `SET_DEVICE_PARAM failsafeHoldEnabled 0|1` so the PC tool can show firmware version and update settings through one extensible protocol.

### 2026-06-22 - V0.1.8 / inner 18

- Synced SVN updates through r628; project-path changes include r445, r459, and r462.
- Added DS800 SBUS remote-control service on USART6 with clean switch, swing switch, fixed-angle mode, pressure adjustment, speed/amplitude adjustment, and persistent parameter storage.
- Added widget activity tracking so recent PSDK widget operations can coexist with DS800 remote-control input.
- Added swing endpoint calibration support and raised the UI swing amplitude range to 40 degrees.
- Preserved DS800 remote-control parameters when OTA state is rewritten.

## Unified Flash Layout

| Region | Address range | Size |
| --- | --- | ---: |
| Bootloader | `0x08000000 - 0x0801FFFF` | 128KB |
| App slot A | `0x08020000 - 0x080BFFFF` | 640KB |
| App slot B | `0x080C0000 - 0x0815FFFF` | 640KB |
| Route storage | `0x08160000 - 0x081DFFFF` | 512KB |
| OTA state | `0x081E0000 - 0x081FFFFF` | 128KB |

## SVN Notes

Do not upload local IDE/cache folders or generated Keil output files. See `SVN_UPLOAD_NOTES.md` for the recommended ignore list.
