# SolarClean Production Flashing

## Flash Layout

| Region | Address range | Size |
| --- | --- | ---: |
| Bootloader | `0x08000000 - 0x0801FFFF` | 128KB |
| App slot A | `0x08020000 - 0x080BFFFF` | 640KB |
| App slot B | `0x080C0000 - 0x0815FFFF` | 640KB |
| Route storage | `0x08160000 - 0x081DFFFF` | 512KB |
| OTA state | `0x081E0000 - 0x081FFFFF` | 128KB |

## Recommended Delivery File

Use `production/SolarClean_boot_app.hex` for production flashing.

The HEX file carries absolute addresses, so production does not need to type the
APP offset manually.

## BIN Alternative

Use `production/SolarClean_boot_app.bin` only when the flashing tool requires a
raw binary file.

Burn address for the combined BIN:

```text
0x08000000
```

The combined BIN contains:

- Bootloader at offset `0x00000000`
- `0xFF` padding up to offset `0x00020000`
- APP at offset `0x00020000`

## Separated BIN Files

If production wants to flash Bootloader only once and APP separately, export the
separated files:

```powershell
powershell -ExecutionPolicy Bypass -File tools\export_flash_bins.ps1
```

Burn addresses:

- `SolarClean_Bootloader.bin` -> `0x08000000`
- `SolarClean_Vx.x.x.bin` -> `0x08020000`

## Generate Production Images

Build Bootloader and APP first, then run:

```powershell
powershell -ExecutionPolicy Bypass -File tools\make_production_image.ps1
```

## Production Notes

- Fresh chips can use the combined HEX or combined BIN.
- APP-only direct flashing uses `0x08020000`.
- When migrating from an older layout, reflash both bootloader and APP once, then erase OTA state `0x081E0000 - 0x081FFFFF`.
- After migration, serial OTA and 4G OTA use slot B at `0x080C0000`.
- Bootloader only needs reflashing when Bootloader code or the Flash layout changes.
