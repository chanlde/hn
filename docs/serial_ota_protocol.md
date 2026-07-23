# Serial OTA Protocol

This document describes the Type-C / CP2102-compatible serial OTA flow used to
replace or supplement the 4G/HTTP download path.

## Status

Implemented:

1. APP initializes the CP2102-compatible USB virtual COM port.
2. APP waits up to 5 seconds for `ENTER_OTA_SERIAL`.
3. A valid enter command makes APP enter serial OTA standby mode and skip PSDK startup.
4. PC later sends `START_OTA_SERIAL(size, sha256)` after the user clicks Start.
5. APP erases OTA slot B.
6. PC sends firmware bytes in ordered `SCOT` binary chunks.
7. APP writes chunks to slot B, ACKing each chunk with `nextOffset`.
8. PC sends an END frame.
9. APP finalizes Flash, verifies slot B SHA256, writes OTA pending state, and resets.
10. Bootloader installs slot B into app slot A on the next boot.

## Slot Layout

| Region | Address | Size |
| --- | --- | ---: |
| Bootloader | `0x08000000` | 128KB |
| App slot A | `0x08020000` | 640KB |
| App slot B | `0x080C0000` | 640KB |
| Route storage | `0x08160000` | 512KB |
| OTA state | `0x081E0000` | 128KB |

The firmware image must be linked for app slot A at `0x08020000`. The maximum
serial OTA image size is `655360` bytes.

## Port Ownership

Startup priority:

1. USB serial OTA enter window.
2. Normal PSDK/application startup after the window times out.

If the board receives `ENTER_OTA_SERIAL` during the window, PSDK is not started.
This prevents PSDK and OTA from reading the same byte stream.

The USB identity remains CP2102-compatible for PSDK compatibility. Do not change
the VID/PID/manufacturer/product strings unless the PSDK connection strategy is
changed as well.

## Enter Command

The PC sends this ASCII line immediately after the COM port opens:

```text
ENTER_OTA_SERIAL
```

Line ending is `\r\n`.

Accepted response:

```json
{"type":"otaSerialReady","ok":true,"mode":"serial_ota"}
```

This command only enters serial OTA standby mode. It does not carry firmware
size, does not erase Flash, and does not start data transfer.

## Start Command

After the device is in standby and the user clicks Start, the PC sends:

```text
START_OTA_SERIAL(<size>, <sha256>)
```

The parser also accepts:

```text
START_OTA_SERIAL <size> <sha256>
```

| Field | Description |
| --- | --- |
| `size` | Firmware image size in bytes. Must be `1..655360`. |
| `sha256` | 64 hex characters for the complete `.bin`. |

Start accepted:

```json
{"type":"otaSerialAck","ok":true,"mode":"serial_ota","size":391248,"sha256":"...","chunk":1024}
```

## Device Responses

Device information:

```text
GET_DEVICE_INFO
```

The response includes firmware identity and `extParams`:

```json
{"type":"deviceInfo","ok":true,"firmwareVersion":"0.1.10.0","innerVersion":20,"extParams":{"failsafeHoldEnabled":false,"remoteControlEnabled":true,"fourGEnabled":true,"psdkEnabled":true}}
```

Extended parameters use one command format:

```text
SET_DEVICE_PARAM <name> 0|1
```

Supported names are `failsafeHoldEnabled`, `remoteControlEnabled`,
`fourGEnabled`, and `psdkEnabled`. The `psdkEnabled` setting is stored in Flash
and takes effect on the next boot.

Chunk accepted:

```json
{"type":"otaSerialChunkAck","ok":true,"seq":12,"nextOffset":13312}
```

Done:

```json
{"type":"otaSerialDone","ok":true,"msg":"pending reboot","size":391248}
```

Any error response has `ok:false`, `code`, and `msg`.

## Binary Frame

After `otaSerialAck ok:true`, the PC sends binary frames:

| Field | Size | Description |
| --- | ---: | --- |
| magic | 4 | ASCII `SCOT` |
| version | 1 | `1` |
| type | 1 | `0x02` data, `0x03` end, `0x04` abort |
| headerLen | 2 | `20`, little-endian |
| seq | 4 | Monotonic sequence, little-endian |
| offset | 4 | Firmware byte offset, little-endian |
| len | 2 | Payload length, little-endian |
| crc16 | 2 | CRC16-Modbus over payload |
| payload | `len` | Firmware bytes |

Rules:

- Payload is `1..1024` bytes for data frames.
- Device only accepts ordered chunks.
- `offset` must equal the number of bytes already written.
- PC advances only after receiving `otaSerialChunkAck ok:true`.
- END has `len=0` and is sent after `nextOffset == size`.

## PC Tool

Run:

```powershell
python serial_ota\main.py
```

Recommended sequence:

1. Open the COM port.
2. Reset or power-cycle the board if needed.
3. Wait for `otaSerialReady ok:true`.
4. Select the `.bin`; the tool fills `size` and `sha256`.
5. Click Start Upgrade.
6. Wait for `otaSerialDone ok:true`; the board resets and bootloader installs slot B.
