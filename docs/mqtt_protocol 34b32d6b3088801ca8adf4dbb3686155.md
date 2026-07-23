# mqtt_protocol

# SolarClean MQTT 协议（手机 APP ↔︎ MCU）

版本：`v` 字段从 **1** 开始。编码 UTF-8 JSON。

## Broker 与身份

- 默认 broker / 端口见固件 [`mqtt_app_config.h`](../DJI_PSDK/APP/mqtt_task/mqtt_app_config.h)。
- `deviceId`：与 MQTT ClientId 一致，形如 `T` + 8 位十六进制（MCU UID Word2），例如 `T36393932`。

## Topic（TJI 平台 App 统一三主题，以此为准）

> App 侧 topic 命名必须和消防吊桶一致，只替换产品前缀：`SolarClean/devices/{sn}/lifecycle|status|control`。MCU 固件（`mqtt_app_config.h`）已仅使用上述三主题；历史 `solarclean/<deviceId>/cmd|ack|state|event` 不再由固件发布，旧脚本请改用本表路径。
> 

| 方向 | 平台 App Topic 格式 | Payload type | 说明 |
| --- | --- | --- | --- |
| APP→设备 | `SolarClean/devices/<deviceSn>/control` | `ping`、`pumpSwitch`、`pumpPressure`、`sprayAngle`、`servoSwing`、`routeList`、`routeDelete`、`routeDownload`、`routeDownloadCancel`、`executeSlot` | 唯一控制入口 |
| 设备→APP | `SolarClean/devices/<deviceSn>/status` | `state`、`ack` | 周期状态与命令应答；App 通过 payload 的 `type` 区分 |
| 设备→APP | `SolarClean/devices/<deviceSn>/lifecycle` | `downloadProgress`、`downloadDone`、`downloadError`、`routeExecuteStarted`、`routeExecuteFinished` 等 | 异步事件、生命周期事件、下载进度 |

APP 订阅：`SolarClean/devices/<deviceSn>/status`、`SolarClean/devices/<deviceSn>/lifecycle`。APP 发布：`SolarClean/devices/<deviceSn>/control`。

## 公共字段

```json
{ "v": 1, "type": "<枚举>", "msgId": "<字符串>", "ts": 1766000000123 }
```

- `msgId`：APP 下发命令时必填；MCU 在对应 `ack` 中原样回填。
- `ts`：可选，毫秒时间戳。

## 命令（topic=`.../control`）

| type | 必填字段 | 说明 |
| --- | --- | --- |
| `pumpSwitch` | `on` (bool) | 水泵开/关 |
| `pumpPressure` | `percent` (number, 0~100) | 水泵压力 PWM 占空比（%）；不自动开泵，需配合 `pumpSwitch`；大于 100 时固件按 100 clamp |
| `sprayAngle` | `angleDeg` (float, **0~100**) | **总喷洒摆幅角（°）**：以中点 90° 对称，左右各 `angleDeg/2`。例 `100` → 左 50°、右 50°，即 `PwmSwing_SetRange(40°, 140°)` |
| `servoSwing` | `on` (bool)；可选 `speed` (uint32) | 舵机摆动启停与速度；摆幅请用 `sprayAngle`。可选 `amplitude` 仍映射底层幅度，**不推荐**与 `sprayAngle` 混用 |
| `routeList` | 无 | 查询 5 槽 KMZ 占用 |
| `routeDelete` | `slot` (0~4) | 删除槽并擦 Flash |
| `routeDownload` | `slot`,`url`,`size`；可选 `checksum` 如 `crc32:XXXXXXXX` | 触发 HTTP 下载入槽 |
| `routeDownloadCancel` | 可选 `slot` | 取消下载 |
| `executeSlot` | `slot` (0~4) | 执行槽位航线（Waypoint V3） |
| `ping` | 无 | 诊断 |

### `routeDownload` 示例

```json
{
  "v": 1,
  "type": "routeDownload",
  "msgId": "dl-001",
  "slot": 2,
  "url": "http://cdn.example.com/r/x.kmz?sig=...",
  "size": 6671,
  "checksum": "crc32:A1B2C3D4"
}
```

- `url` 建议使用**预签名、短时有效**的 HTTP URL（鉴权在 URL 内，MCU 无需额外 Header）。
- `size`：期望字节数；下载完成后须一致。
- `checksum`：可选；格式 `crc32:` + **8 位大写十六进制** CRC32（IEEE 多项式，与以太网常见一致）。

## 应答（topic=`.../status`，payload `type=ack`）

```json
{
  "v": 1,
  "type": "ack",
  "msgId": "dl-001",
  "ofType": "routeDownload",
  "ok": true,
  "code": 0,
  "msg": "accepted",
  "data": {}
}
```

- `routeList` 的 `data` 示例：

```json
{
  "slots": [
    { "index": 0, "bytes": 0, "valid": false },
    { "index": 1, "bytes": 6671, "valid": true }
  ]
}
```

### 错误码 `code`（约定）

| 区间 | 含义 |
| --- | --- |
| 0 | 成功 |
| 100–199 | 参数非法 |
| 200–299 | 状态不允许（如忙） |
| 300–399 | 硬件/外设 |
| 400–499 | 网络/HTTP/下载 |
| 500–599 | 内部错误 |

## 周期状态（topic=`.../status`，payload `type=state`）

约 **1 Hz**。字段含义与串口 `0x05` 一致（数值换算后浮点便于 APP）：

- `lat`,`lon`：度（由 `latitude_1e6`/`longitude_1e6` 换算）
- `alt`：米（`height_1e2/100`）
- `speed`：m/s（`speed_1e2/100`）
- `yaw`,`pitch`,`roll`：度（`_1e2/100`）
- `sat`：卫星数
- `battery`：%（`battery_1e2/100`）
- `waypoint`：当前航点序号
- `water`：水位状态（uint8）
- `mqtt`：`tcp` bool，`lastErr` int
- `download`：仅下载中出现 — `slot`,`active`,`pct`,`bytes`,`total`

无飞机链接时部分字段可能为 0。

## 事件（topic=`.../lifecycle`）

| type | 说明 |
| --- | --- |
| `downloadProgress` | `slot`,`bytes`,`total`,`pct` |
| `downloadDone` | `slot`,`size`,`checksumOk`,`storedInFlash` |
| `downloadError` | `slot`,`code`,`msg`,`retries` |
| `routeExecuteStarted` | `slot`（可选扩展） |
| `routeExecuteFinished` | `slot`,`ok`（可选扩展） |

## 流程摘要

1. APP 向 `control` 发 `routeDownload` → `status` 上的 `type=ack` 表示已受理 → `lifecycle` 上报进度 → `downloadDone`。
2. APP 再发 `executeSlot` → 开始执行航线（危险操作，勿自动执行）。

## 兼容说明

- 串口自定义协议（`0x01`…`0x15`）保持不变，与 MQTT 并行独立。
- 旧测试 topic `spectrum-detection-client/<deviceId>` 若仍配置，可与新业务并存，仅用于调试（与 SolarClean JSON 控制面无关）。

## 自检（开发者）

**Phase 1 — MQTTX**：连接 broker，订阅 `SolarClean/devices/<deviceSn>/status` 与 `SolarClean/devices/<deviceSn>/lifecycle`，向 `SolarClean/devices/<deviceSn>/control` 发送 `{"v":1,"type":"ping","msgId":"t1"}`；应在 `status` 收到 `type=ack`；`type=state` 约 1 Hz 上报。

**Phase 3 — 航线下载**：向 `SolarClean/devices/<deviceSn>/control` 发送 `routeDownload`（有效预签名 HTTP URL，`size` 与实际 KMZ 字节一致）；观察 `SolarClean/devices/<deviceSn>/lifecycle`：`downloadProgress` → `downloadDone`。下载完成后 MCU 会断开 TCP 做一次 HTTP GET，再回到 MQTT（约 10–30 s）。若使用 `SKIP_PSDK_START_TASK=1`，KMZ 写入 RAM/Flash 依赖 **未初始化** mutex 可能失败——航线相关请先保持 PSDK start task 启用或后续接入 `CustomSerial_MqttPrereqInit()`。