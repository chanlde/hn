# SVN Upload Notes

This repository should be uploaded as source plus the current customer updater executable.

## Keep

- `APP/`
- `Bootloader/`
- `BSP/`
- `DJI_PSDK/`
- `docs/`
- `serial_ota/`
- `tools/`
- `README.md`
- `SVN_UPLOAD_NOTES.md`
- `工程功能报告.md`

## Do Not Upload

- `.git/`
- `.cursor/`
- `.vscode/`
- `__pycache__/`
- Keil output files: `*.o`, `*.d`, `*.crf`, `*.axf`, `*.hex`, `*.map`, `*.htm`, `*.lnp`, `*.dep`, `*.iex`, `*.lst`, `*.log`
- PyInstaller intermediate folders: `build/`, `dist/`, `_build_tmp/`, `_dist_tmp/`
- Generated firmware output under `APP/MDK-ARM/APP/`, except source scatter files such as `APP.sct` and `APP_B.sct`
- Generated bootloader output under `Bootloader/MDK-ARM/Bootloader/`

## Current Deliverable

- `serial_ota/SolarCleanFirmwareUpdater.exe`

Firmware `.bin` files should be regenerated from Keil for the exact build configuration being released.
