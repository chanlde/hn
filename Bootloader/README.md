# SolarClean Bootloader

STM32H743VITx bootloader for OTA install-from-staging.

## Flash Layout

| Region | Address range | Size | Purpose |
| --- | --- | ---: | --- |
| Bootloader | `0x08000000 - 0x0801FFFF` | 128KB | Reset entry and OTA installer |
| App slot A | `0x08020000 - 0x080BFFFF` | 640KB | Running application image |
| App slot B | `0x080C0000 - 0x0815FFFF` | 640KB | OTA staging image |
| Route storage | `0x08160000 - 0x081DFFFF` | 512KB | Reserved for route KMZ/JSON files |
| OTA state | `0x081E0000 - 0x081FFFFF` | 128KB | OTA metadata, result and failure reason |

All product variants use this same layout. Versions without route features keep
the route storage region reserved so that serial OTA and 4G OTA always write to
the same slot B address that the bootloader later reads.

## OTA Flow

1. App downloads the new A-linked firmware image to slot B.
2. App verifies SHA256 and writes `PENDING_VERIFY` to the OTA state region.
3. Bootloader verifies the staged image in slot B.
4. Bootloader erases slot A and copies slot B to slot A.
5. Bootloader verifies slot A, marks the OTA state valid, then boots slot A.

The app image is always linked to run from `0x08020000`; slot B is only
temporary storage.

## Boot Log

- UART: USART1, PA9 TX / PA10 RX, 115200 8N1
- Logs are printed before jumping to APP, for example:
  - `[BOOT] start`
  - `[BOOT] install pending ...`
  - `[BOOT] verify B OK`
  - `[BOOT] erase A OK`
  - `[BOOT] copy B to A OK ...`
  - `[BOOT] verify A OK`
  - `[BOOT] mark valid rc=0`
  - `[BOOT] jump 0x08020000`

## Build And Burn Order

1. Open and build `Bootloader/MDK-ARM/Bootloader.uvprojx`.
2. Burn `Bootloader.hex` at `0x08000000`.
3. Open and build `APP/MDK-ARM/APP.uvprojx`.
4. Burn `APP.hex` at `0x08020000`.
5. When migrating from the old layout, erase OTA state `0x081E0000 - 0x081FFFFF`.
