from __future__ import annotations

import hashlib
from pathlib import Path

from ota_protocol import FirmwareInfo, MAX_IMAGE_SIZE

FIRMWARE_NAME_PREFIX = "SolarClean_V"
FIRMWARE_NAME_SUFFIX = ".bin"
FIRMWARE_FILE_FILTER = "SolarClean firmware (SolarClean_V*.bin)"


def is_project_firmware(path: str | Path) -> bool:
    name = Path(path).name
    return name.startswith(FIRMWARE_NAME_PREFIX) and name.lower().endswith(FIRMWARE_NAME_SUFFIX)


def load_firmware(path: str) -> tuple[FirmwareInfo, bytes]:
    file_path = Path(path)
    if not is_project_firmware(file_path):
        raise ValueError("请选择 SolarClean_V*.bin 固件文件")

    data = file_path.read_bytes()
    if not data:
        raise ValueError("升级文件为空")
    if len(data) > MAX_IMAGE_SIZE:
        raise ValueError("升级文件过大，请确认文件是否正确")

    info = FirmwareInfo(
        path=str(file_path),
        size=len(data),
        sha256=hashlib.sha256(data).hexdigest(),
    )
    return info, data
