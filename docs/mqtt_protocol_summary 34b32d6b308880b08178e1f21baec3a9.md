# mqtt_protocol_summary

# SolarClean MQTT 协议 — 实用写法说明

> 字段细节与完整说明见 [mqtt_protocol.md](./mqtt_protocol.md)。Broker / 端口 / 账号见固件 `DJI_PSDK/APP/mqtt_task/mqtt_app_config.h`。
> 

本文目的：**你知道往哪个 Topic 发什么字符串**，复制改 `deviceId` 即可用。

---

## 1. 先弄清三件事

### 1.1 `deviceId` 怎么写

与 MCU 的 MQTT ClientId 一致，形如 **`T` + 8 位十六进制**（来自芯片 UID），例如：`T36393932`。

下文用占位符 **`<deviceId>`**，实际替换为你的设备 ID。

### 1.2 Topic 采用 TJI 平台统一三主题（控制只走 `control`）

> App 侧与消防吊桶保持一致：只区分 `lifecycle`、`status`、`control` 三类 topic；光伏清洗只是产品前缀不同。
> 

| Topic | 谁发 | 你在 MQTTX / App 里怎么做 |
| --- | --- | --- |
| `SolarClean/devices/<deviceSn>/control` | **你 → 设备** | **发布** JSON 命令（唯一控制入口） |
| `SolarClean/devices/<deviceSn>/status` | 设备 → 你 | **订阅**，接收 `type=state` 周期状态和 `type=ack` 命令应答 |
| `SolarClean/devices/<deviceSn>/lifecycle` | 设备 → 你 | **订阅**，接收下载进度、下载完成、下载失败、航线执行事件等 |

**不要**往 `status` / `lifecycle` 发指令期望控制设备；控制只发布到 `control`。

### 1.3 每条下发 JSON 的固定外壳

```json
{
  "v": 1,
  "type": "<命令类型英文>",
  "msgId": "<字符串，你自己起，便于对上 ack>"
}
```

- **`msgId` 必填**：随便写，例如 `"t1"`、`"req-20260422-01"`；MCU 在 `ack` 里原样带回。
- **`type`**：必须是下面表格里列出的枚举之一。

---

## 2. MQTTX 最小操作顺序

1. 连接 Broker（地址/端口/用户名密码与 `mqtt_app_config.h` 一致）。
2. **订阅**（一次可加多条）：
    - `SolarClean/devices/<deviceSn>/status`
    - `SolarClean/devices/<deviceSn>/lifecycle`
3. **发布**到：`SolarClean/devices/<deviceSn>/control`，正文为下面某一节的 **一整行 JSON**（建议压缩成一行发）。

---

## 3. 命令 JSON — 按 `type` 复制（发到 `.../control`）

下面示例里把 `<deviceSn>` 在 Topic 里换掉即可；JSON 里的 **`msgId` 每次可改**，避免混淆多条请求。

### 3.1 `ping` — 连通性自检

**Topic：** `SolarClean/devices/<deviceSn>/control`

```json
{"v":1,"type":"ping","msgId":"t1"}
```

**期望 `ack`（示意）：**

```json
{"v":1,"type":"ack","msgId":"t1","ofType":"ping","ok":true,"code":0,"msg":"pong","data":{}}
```

---

### 3.2 `pumpSwitch` — 水泵开关

**必填：** 布尔字段 **`on`**（不是把 `"on"` 写在 `msgId` 里）。

**Topic：** `SolarClean/devices/<deviceSn>/control`

开泵：

```json
{"v":1,"type":"pumpSwitch","msgId":"pump-001","on":true}
```

关泵：

```json
{"v":1,"type":"pumpSwitch","msgId":"pump-002","on":false}
```

**常见错误：** 只写 `"msgId":"on"` 而没有 `"on":true/false` → MCU 会应答失败，`code` 约在 100 段（参数非法）。

---

### 3.2b `pumpPressure` — 水泵压力（占空比 %）

**必填：** `percent`（0~100，整数或小数均可；负数失败；大于 100 时固件 clamp 到 100）。**不自动开泵**，开关仍用 `pumpSwitch`。

```json
{"v":1,"type":"pumpPressure","msgId":"pp1","percent":60}
```

---

### 3.3 `sprayAngle` — 总喷洒摆幅角（度）

**必填：** `angleDeg`，范围 **0~100**（总角）。MCU 以 **90° 为中点**，左右各一半：`min=90-angleDeg/2`，`max=90+angleDeg/2`。

例：总角 100° → 左右各 50° → 约 40°~140° 摆动范围：

```json
{"v":1,"type":"sprayAngle","msgId":"a1","angleDeg":100}
```

---

### 3.4 `servoSwing` — 舵机摆动（启停 / 速度）

**必填：** `on`（bool）。

**可选：** `speed`（无符号整数，底层摆动速度）。**摆幅请用 `sprayAngle`**。`amplitude` 仍可下发但不推荐，避免与 `sprayAngle` 混用。

开启并带速度示例：

```json
{"v":1,"type":"servoSwing","msgId":"sw1","on":true,"speed":50}
```

关闭：

```json
{"v":1,"type":"servoSwing","msgId":"sw2","on":false}
```

---

### 3.5 `routeList` — 查询航线槽占用

无额外字段：

```json
{"v":1,"type":"routeList","msgId":"rl1"}
```

**`ack` 里 `data.slots[]`** 会带各槽 `bytes`、`valid` 等（完整结构见 `mqtt_protocol.md`）。

---

### 3.6 `routeDelete` — 删除指定槽

**必填：** `slot`（整数槽号，工程里一般为 0～4，以固件 `CUSTOM_ROUTE_SLOT_COUNT` 为准）。

```json
{"v":1,"type":"routeDelete","msgId":"rd1","slot":2}
```

---

### 3.7 `routeDownload` — HTTP 下载 KMZ 到槽

**必填：** `slot`、`url`、`size`（字节数与实际文件一致）。

**可选：** `checksum`，格式 `crc32:` + **8 位大写十六进制**。

```json
{
  "v": 1,
  "type": "routeDownload",
  "msgId": "dl-001",
  "slot": 2,
  "url": "http://cdn.example.com/route.kmz?签名参数",
  "size": 6671,
  "checksum": "crc32:A1B2C3D4"
}
```

压缩一行示例：

```json
{"v":1,"type":"routeDownload","msgId":"dl-001","slot":2,"url":"http://cdn.example.com/x.kmz","size":6671}
```

进度与结果在 **`lifecycle`**：`downloadProgress` → `downloadDone` / `downloadError`。

---

### 3.8 `routeDownloadCancel` — 取消下载

可选 `slot`；不写或特殊值表示取消当前（以固件为准）。

```json
{"v":1,"type":"routeDownloadCancel","msgId":"dc1","slot":2}
```

---

### 3.9 `executeSlot` — 执行某槽航线（谨慎）

**必填：** `slot`。

```json
{"v":1,"type":"executeSlot","msgId":"ex1","slot":2}
```

---

## 4. 应答 `ack`（设备发到 `.../status`，payload 内 `type=ack`）— 你怎么读

典型成功形态：

```json
{
  "v": 1,
  "type": "ack",
  "msgId": "与你下发相同",
  "ofType": "与你下发的 type 相同",
  "ok": true,
  "code": 0,
  "msg": "说明字符串",
  "data": {}
}
```

失败时 `ok` 为 `false`，`code` 非 0；**错误码区间约定**：0 成功；100–199 参数；200–299 状态；300–399 硬件；400–499 网络/下载；500–599 内部（细则见 `mqtt_protocol.md`）。

---

## 5. `status` / `lifecycle` — 你只订阅即可

- **`status`**：承载 `type=state` 周期状态和 `type=ack` 命令应答。
- **`lifecycle`**：承载下载进度、下载完成、下载失败、航线执行事件等；**结构为 JSON，`type` 区分事件种类**（见 `mqtt_protocol.md` 表格）。

不要在 `status` / `lifecycle` 上发布命令。

---

## 6. 典型流程（航线下载 → 执行）

1. 向 `control` 发 **`routeDownload`** → 在 `status` 看 **`type=ack`** 是否受理。
2. 订阅 **`lifecycle`**：观察 `downloadProgress` → `downloadDone` 或 `downloadError`。
3. 确认落地后再向 `control` 发 **`executeSlot`**（危险操作，勿自动执行）。

---

## 7. 与串口协议

串口自定义帧与 MQTT **并行**，互不影响；控制 MQTT 设备只依赖本文 **`control` / `status` / `lifecycle`**。

---

## 8. App 对接扩展（上架前建议）

开发与测试可只按本文操作；要做正式手机 App，见 [**app_integration_checklist.md**](./app_integration_checklist.md)（Topic/鉴权/`state` 字段/航线下载时序/安全与测试清单）。