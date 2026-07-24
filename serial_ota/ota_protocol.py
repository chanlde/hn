from __future__ import annotations

import json
import struct
from dataclasses import dataclass
from typing import Any


DEFAULT_BAUDRATE = 115200
DEFAULT_SHA256 = "0" * 64
MAX_IMAGE_SIZE = 896 * 1024

MAGIC = b"SCOT"
VERSION = 1
HEADER_LEN = 20
TYPE_DATA = 0x02
TYPE_END = 0x03
TYPE_ABORT = 0x04
CHUNK_SIZE = 1024


def crc16_modbus(data: bytes | bytearray) -> int:
    crc = 0xFFFF
    for byte in data:
        crc ^= byte
        for _ in range(8):
            if crc & 1:
                crc = (crc >> 1) ^ 0xA001
            else:
                crc >>= 1
    return crc & 0xFFFF


def build_start_command(size: int, sha256: str) -> bytes:
    return f"START_OTA_SERIAL({size}, {sha256})\r\n".encode("ascii")


def build_enter_command() -> bytes:
    return b"ENTER_OTA_SERIAL\r\n"


def build_device_info_command() -> bytes:
    return b"GET_DEVICE_INFO\r\n"


def build_set_device_param_command(name: str, enabled: bool) -> bytes:
    validate_device_param_name(name)
    value = "1" if enabled else "0"
    return f"SET_DEVICE_PARAM {name} {value}\r\n".encode("ascii")


def build_set_servo_limits_command(left_deg: float, right_deg: float, save: bool) -> bytes:
    left_x10 = round(left_deg * 10.0)
    right_x10 = round(right_deg * 10.0)
    if not 0 <= left_x10 <= 900 or not 0 <= right_x10 <= 900:
        raise ValueError("servo limits must be between 0.0 and 90.0 degrees")
    return f"SET_SERVO_LIMITS {left_x10} {right_x10} {1 if save else 0}\r\n".encode("ascii")


def build_set_servo_motion_command(amplitude_percent: int, speed_percent: int, save: bool) -> bytes:
    if not isinstance(amplitude_percent, int) or not isinstance(speed_percent, int):
        raise ValueError("servo motion values must be integers")
    if not 0 <= amplitude_percent <= 100 or not 0 <= speed_percent <= 100:
        raise ValueError("servo motion values must be between 0 and 100 percent")
    return f"SET_SERVO_MOTION {amplitude_percent} {speed_percent} {1 if save else 0}\r\n".encode("ascii")


def build_set_servo_swing_command(enabled: bool) -> bytes:
    return f"SET_SERVO_SWING {1 if enabled else 0}\r\n".encode("ascii")


def validate_device_param_name(name: str) -> None:
    if name not in {"failsafeHoldEnabled", "remoteControlEnabled", "fourGEnabled", "psdkEnabled"}:
        raise ValueError(f"unsupported device param: {name}")


def build_frame(frame_type: int, seq: int, offset: int, payload: bytes = b"") -> bytes:
    if len(payload) > CHUNK_SIZE:
        raise ValueError(f"payload too large: {len(payload)} > {CHUNK_SIZE}")
    header = struct.pack(
        "<4sBBHIIHH",
        MAGIC,
        VERSION,
        frame_type,
        HEADER_LEN,
        seq & 0xFFFFFFFF,
        offset & 0xFFFFFFFF,
        len(payload),
        crc16_modbus(payload),
    )
    return header + payload


def build_data_frame(seq: int, offset: int, payload: bytes) -> bytes:
    return build_frame(TYPE_DATA, seq, offset, payload)


def build_end_frame(seq: int, offset: int) -> bytes:
    return build_frame(TYPE_END, seq, offset)


def build_abort_frame(seq: int, offset: int) -> bytes:
    return build_frame(TYPE_ABORT, seq, offset)


def parse_json_line(line: str) -> dict[str, Any] | None:
    line = line.strip()
    if not line.startswith("{"):
        return None
    try:
        obj = json.loads(line)
    except json.JSONDecodeError:
        return None
    return obj if isinstance(obj, dict) else None


@dataclass(frozen=True)
class FirmwareInfo:
    path: str
    size: int
    sha256: str


@dataclass(frozen=True)
class DeviceInfo:
    product_name: str = ""
    product_id: str = ""
    product_code: str = ""
    product_type: str = ""
    model: str = ""
    board: str = ""
    device_id: str = ""
    serial_number: str = ""
    mcu_uid: str = ""
    hardware_version: str = ""
    firmware_version: str = ""
    inner_version: int = 0
    boot_slot: str = ""
    ota_status: str = ""
    network: str = ""
    slot_b_size: int = 0
    max_chunk: int = CHUNK_SIZE
    failsafe_hold_enabled: bool = False
    remote_control_enabled: bool = False
    four_g_enabled: bool = False
    psdk_enabled: bool = False
    servo_left_limit_deg: float = 0.0
    servo_right_limit_deg: float = 0.0
    swing_amplitude_percent: int = 100
    swing_speed_percent: int = 0
    servo_swing_running: bool = False
    servo_limits_supported: bool = False


def parse_device_info(msg: dict[str, Any]) -> DeviceInfo:
    ext_params = msg.get("extParams", {})
    if not isinstance(ext_params, dict):
        ext_params = {}
    capabilities = msg.get("capabilities", [])
    if not isinstance(capabilities, list):
        capabilities = []
    return DeviceInfo(
        product_name=str(msg.get("productName", "")),
        product_id=str(msg.get("productId", "")),
        product_code=str(msg.get("productCode", "")),
        product_type=str(msg.get("productType", "")),
        model=str(msg.get("model", "")),
        board=str(msg.get("board", "")),
        device_id=str(msg.get("deviceId", "")),
        serial_number=str(msg.get("serialNumber", "")),
        mcu_uid=str(msg.get("mcuUid", "")),
        hardware_version=str(msg.get("hardwareVersion", "")),
        firmware_version=str(msg.get("firmwareVersion", "")),
        inner_version=int(msg.get("innerVersion", 0) or 0),
        boot_slot=str(msg.get("bootSlot", "")),
        ota_status=str(msg.get("otaStatus", "")),
        network=str(msg.get("network", "")),
        slot_b_size=int(msg.get("slotBSize", 0) or 0),
        max_chunk=int(msg.get("maxChunk", CHUNK_SIZE) or CHUNK_SIZE),
        failsafe_hold_enabled=bool(ext_params.get("failsafeHoldEnabled", msg.get("failsafeHoldEnabled", False))),
        remote_control_enabled=bool(ext_params.get("remoteControlEnabled", True)),
        four_g_enabled=bool(ext_params.get("fourGEnabled", False)),
        psdk_enabled=bool(ext_params.get("psdkEnabled", False)),
        servo_left_limit_deg=float(ext_params.get("servoLeftLimitDegX10", 0) or 0) / 10.0,
        servo_right_limit_deg=float(ext_params.get("servoRightLimitDegX10", 0) or 0) / 10.0,
        swing_amplitude_percent=int(ext_params.get("swingAmplitudePercent", 100) or 0),
        swing_speed_percent=int(ext_params.get("swingSpeedPercent", 0) or 0),
        servo_swing_running=bool(ext_params.get("servoSwingRunning", False)),
        servo_limits_supported="servo_limit_calibration" in capabilities,
    )


def parse_ext_params(msg: dict[str, Any]) -> dict[str, Any]:
    ext_params = msg.get("extParams", {})
    if not isinstance(ext_params, dict):
        return {}
    return {
        "failsafeHoldEnabled": bool(ext_params.get("failsafeHoldEnabled", False)),
        "remoteControlEnabled": bool(ext_params.get("remoteControlEnabled", True)),
        "fourGEnabled": bool(ext_params.get("fourGEnabled", False)),
        "psdkEnabled": bool(ext_params.get("psdkEnabled", False)),
        "servoLeftLimitDeg": float(ext_params.get("servoLeftLimitDegX10", 0) or 0) / 10.0,
        "servoRightLimitDeg": float(ext_params.get("servoRightLimitDegX10", 0) or 0) / 10.0,
        "swingAmplitudePercent": int(ext_params.get("swingAmplitudePercent", 100) or 0),
        "swingSpeedPercent": int(ext_params.get("swingSpeedPercent", 0) or 0),
        "servoSwingRunning": bool(ext_params.get("servoSwingRunning", False)),
    }
