# SolarClean STM32H743VITx OTA Bootloader Plan

## Flash Layout

| Region | Address | Size | Purpose |
| --- | ---: | ---: | --- |
| Bootloader | `0x08000000` | `128KB` | Start selection and rollback |
| App Slot A | `0x08020000` | `640KB` | Current/primary app image |
| App Slot B | `0x080C0000` | `640KB` | OTA target app image |
| Route Storage | `0x08160000` | `512KB` | Reserved for route KMZ/JSON files |
| OTA State | `0x081E0000` | `128KB` | `T_OtaInfo`, result and fail reason |

Current APP ROM is about 382KB, so a 640KB slot still has room for the current
firmware plus growth. All product variants use the same layout; versions without
route features keep the route storage region reserved.

## Bring-Up Order

1. Build and flash Bootloader at `0x08000000`.
2. Move APP scatter load/exec address to `0x08020000`. Done.
3. Enable APP vector relocation to `0x08020000`. Done.
4. Add OTA state erase/write helpers in APP. Done.
5. Verify Bootloader can jump Slot A on hardware.
6. Add Slot B erase/write helpers. Done.
7. Add firmware hash verification. Done.
8. Add Slot B commit flow: hash verify and write `PENDING_VERIFY`. Done.
9. Add HTTP download to Slot B. Done.
10. Add app-side success confirmation after new image boots. Done.
11. Add rollback on failed boot attempts. Done.
12. Add delayed reset after OTA commit. Done.

## OTA State Contract

APP and Bootloader share the same OTA state layout:

- `magic`: `0x53434F54`
- `structVersion`: `1`
- `currentSlot`: confirmed slot
- `targetSlot`: newly written slot
- `previousSlot`: rollback slot
- `otaState`: Bootloader decision state
- `bootTryCount`: retry counter for pending image
- `maxBootTryCount`: default `3`
- `firmwareSize`: downloaded image size
- `firmwareSha256`: expected image hash
- `crc32`: CRC over the structure before this field

The APP writes `PENDING_VERIFY` only after Slot B is written and verified.
Bootloader reads that state after reset and jumps to `targetSlot`.

## APP Slot B Writer

`solarclean_ota_flash` owns the Slot B write flow:

- Slot B base: `0x080C0000`
- Slot B end: `0x0815FFFF`
- Begin checks image size and erases Slot B.
- Write appends blocks in order, buffers STM32H7 32-byte Flash words, and verifies each written Flash word by reading Flash back.
- End succeeds only when the written byte count equals the expected image size.
- The final partial Flash word is padded with `0xFF`.

`START_OTA` now rejects firmware larger than Slot B before any download starts.

## Firmware Hash

`solarclean_ota_hash` owns SHA256 validation:

- `START_OTA` rejects malformed `sha256` values before download.
- The expected hash is parsed from 64 lowercase or uppercase hex characters.
- Slot B can be verified by hashing exactly `firmwareSize` bytes from `0x080C0000`.
- Verification ignores the final `0xFF` Flash-word padding added by the writer.

## Commit Flow

After the download writer finishes, APP calls `SolarCleanOta_CommitSlotBForReboot`:

1. Parse the expected SHA256.
2. Hash Slot B over exactly `firmwareSize` bytes.
3. Build OTA state with `otaState = PENDING_VERIFY`.
4. Save OTA state to `0x081E0000`.
5. Publish `ota_status = PENDING_REBOOT`.

The device should reboot only after this commit step succeeds.

## HTTP Download Flow

`START_OTA` now starts an OTA worker task:

1. Validate request fields, hardware version, image size, and SHA256 format.
2. Reply ACK with `ota started`.
3. Worker erases Slot B.
4. `Air780eHttp_GetToStream` downloads HTTP body in chunks.
5. Each HTTP body chunk is written to Slot B through `SolarCleanOtaFlash_Write`.
6. APP publishes progress every 5% during download, up to 80%.
7. Writer finalizes the last 32-byte Flash word.
8. APP verifies SHA256 and publishes 90%.
9. APP commits `PENDING_VERIFY` and publishes 100% / `PENDING_REBOOT`.

HTTP download uses the existing Air780E modem session lock, so MQTT service is
paused while the modem is occupied by the download.

The OTA worker retries the whole download/write flow up to 3 times. Each retry
erases Slot B and starts from byte 0. The APP does not write `PENDING_VERIFY`
and does not reset unless a full HTTP body is received, the written byte count
matches `file_size`, Flash readback verification passes, and SHA256 matches.

## Boot Confirm And Rollback

APP confirms a newly booted image during startup:

1. `main()` calls `SolarCleanOta_ConfirmRunningImage()` after basic peripherals initialize.
2. If OTA state is `PENDING_VERIFY` and `targetSlot` matches the running slot, APP marks the state `VALID`.
3. The confirmed slot becomes `currentSlot`, and retry count is reset.

Bootloader handles pending images before every jump:

1. If state is `PENDING_VERIFY`, increment `bootTryCount` and boot `targetSlot`.
2. If `bootTryCount` already reached `maxBootTryCount`, mark rollback failed reason as `NEW_FW_BOOT_FAILED`.
3. Bootloader then boots `previousSlot`.

After APP downloads, writes, verifies, and commits Slot B, the OTA worker delays
2 seconds and calls `NVIC_SystemReset()`.

## Build Note

If Keil command-line build reports `No space left on device` under the Windows
Temp directory, set `TEMP` and `TMP` to a workspace directory before building.
