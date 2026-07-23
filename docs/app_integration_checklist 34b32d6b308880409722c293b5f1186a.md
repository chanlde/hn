# app_integration_checklist

# 光伏清洗 / SolarClean — App 对接检查清单

> 协议细节见 [mqtt_protocol_summary.md](./mqtt_protocol_summary.md) 与 [mqtt_protocol.md](./mqtt_protocol.md)。
> 
> 
> 固件内 Broker 与测试账号见 `DJI_PSDK/APP/mqtt_task/mqtt_app_config.h`（**勿**将生产凭据写进公网 App）。
> 

按目标分两档：**A. 工程调试**、**B. 正式/上架**。B 在 A 的基础上增加安全、账号与运维项。

---

## 一、协议与 Topic（A / B 都要）

| 序号 | 项 | 说明 |
| --- | --- | --- |
| ☐ | 明确 `deviceId` | 与 MCU ClientId 一致：`T` + 8 位十六进制（如 `T36393932`）。App 需提供输入或扫码绑定方式。 |
| ☐ | 平台三类 Topic | `SolarClean/devices/<sn>/control`、`…/status`、`…/lifecycle`；控制**只发 `control`**。payload 内仍用 `type=ack/state/downloadProgress...` 区分语义。 |
| ☐ | 命令外壳 | `v`、`type`、`msgId`（下发必填）；解析 `ack` 时按 `msgId` 配对。 |
| ☐ | 命令枚举 | `ping`、`pumpSwitch`、`pumpPressure`、`sprayAngle`、`servoSwing`、`routeList`、`routeDelete`、`routeDownload`、`routeDownloadCancel`、`executeSlot`。 |
| ☐ | `ack` | `type=ack`、`ofType` 对应原命令、`ok`、`code`、`msg`、`data`（部分命令有 `data`）。 |
| ☐ | 错误码区间 | 见 `mqtt_protocol.md`（0 / 100–199 / …）；App 可做文案映射。 |
| ☐ | `state` 字段表 | 仪表盘需对照 **`mqtt_protocol.md`「周期状态」** 全文字段（单位、无飞机时的 0/占位）。 |
| ☐ | `lifecycle` 事件类型 | `downloadProgress`、`downloadDone`、`downloadError` 等；下载流程依赖 **`lifecycle`** topic，不要只在 `ack` 里等进度。 |

---

## 二、连接与 Broker（A：直连调试；B：必须加固）

| 序号 | 项 | A 工程调试 | B 正式/上架 |
| --- | --- | --- | --- |
| ☐ | 地址与端口 | 与当前固件一致即可（如 `mqtt_app_config.h` 中 IP + **1883**）。 | **禁止**把固件里的账号密码硬编码进 App；应 **TLS（如 8883）** 或 **自家后端转发 MQTT**。 |
| ☐ | 用户名密码 | 团队内部共用可临时写死（注意安全）。 | 每用户/每设备鉴权、或 App 只调 **HTTPS API**，由服务端代发 MQTT。 |
| ☐ | ClientId 冲突 | MCU 已占用 `deviceId`；**App 侧 ClientId 必须另取**，不能与设备相同。 |  |
| ☐ | QoS / Retain | 与 Broker 策略一致；`state` 侧为 Retain（见协议文档）。App 订阅后应先处理** Retain 最后一条**。 |  |

---

## 三、业务功能映射（建议 UI 层）

| 序号 | 项 | 说明 |
| --- | --- | --- |
| ☐ | 水泵 | `pumpSwitch`，必填 **`on`: bool**，勿把开关含义写在 `msgId` 里。 |
| ☐ | 水泵压力 | `pumpPressure` + **`percent`**（0~100，PWM 占空比）；不自动开泵，需先 `pumpSwitch` 开泵后再调压力。 |
| ☐ | 喷洒总摆幅 | `sprayAngle` + **`angleDeg`（0~100，总角）**；MCU 以 90° 为中点左右各一半（如 100° → 各 50°）。 |
| ☐ | 舵机摆动 | `servoSwing` + `on`，可选 `speed`；摆幅用 `sprayAngle`，勿与可选 `amplitude` 混用。 |
| ☐ | 航线列表 | `routeList`，解析 `ack.data.slots[]`。 |
| ☐ | 删除槽 | `routeDelete` + `slot`。 |
| ☐ | 下载 KMZ | `routeDownload`：`slot`、`url`、`size`；可选 `checksum`；进度看 **`lifecycle`** topic。 |
| ☐ | 取消下载 | `routeDownloadCancel`。 |
| ☐ | 执行航线 | `executeSlot` — **高风险**，建议二次确认、禁用自动执行、权限控制。 |
| ☐ | 心跳 | `ping` + `ack`；可选结合 `state` 内 `mqtt.tcp` / `lastErr`。 |

---

## 四、航线下载与时序（易踩坑）

| 序号 | 项 | 说明 |
| --- | --- | --- |
| ☐ | URL 来源 | `url` 一般由 **服务端** 生成（预签名 OSS/COS、限时）；App 或后端要保证 **`size` 与实际文件一致**。 |
| ☐ | CRC | 若带 `checksum`（`crc32:XXXXXXXX`），算法与协议一致（IEEE CRC32）。 |
| ☐ | TCP 断连 | 固件侧 HTTP 下载时可能 **短时间断开 MQTT**；App 不应把短时断开一律当「指令失败」，应以 **`lifecycle` 事件**为准或重连后继续订 `lifecycle` / `status`。 |
| ☐ | 超时 | 为 `routeDownload` 受理 `ack`、首条 `downloadProgress`、`downloadDone`/`downloadError` 分别设合理超时与重试策略。 |

---

## 五、安全与合规（主要 B）

| 序号 | 项 | 说明 |
| --- | --- | --- |
| ☐ | 凭据 | 生产凭据不进 Git、不进客户端明文。 |
| ☐ | 传输 | 优先 **MQTTS / TLS**；明文 1883 仅限内网或调试。 |
| ☐ | 设备归属 | 用户—设备绑定、解绑、换机策略。 |
| ☐ | 操作审计 | 尤其是 `executeSlot`、水泵等，是否需要服务端日志。 |

---

## 六、测试用例（上线前建议跑通）

| 序号 | 场景 | 期望 |
| --- | --- | --- |
| ☐ | 订阅 `status/lifecycle` 后向 `control` 发 `ping` | 在 `status` 收到 `type=ack`，`ofType=ping`。 |
| ☐ | `pumpSwitch` on/off | `ok=true`，必要时结合硬件或 `state`（若协议中有泵状态字段）。 |
| ☐ | `pumpPressure` percent=0~100 | `ok=true`，`ofType=pumpPressure`；大于 100 时固件按 100 处理。 |
| ☐ | `sprayAngle` angleDeg=100 | `ok=true`；舵机摆动范围约为中点左右各 50°（总 100°）。 |
| ☐ | `routeList` | `data.slots` 结构与文档一致。 |
| ☐ | 小文件 `routeDownload` | `lifecycle` 上进度 → `downloadDone`，再 `routeList` 校验 `valid/bytes`。 |
| ☐ | 错误 URL / 错误 size | `downloadError`，App 提示合理。 |
| ☐ | 下载中 Kill App 再打开 | 重连后仍能收后续 `lifecycle` 或刷新 `state`。 |
| ☐ | 弱网 / broker 断开 | UI 离线提示；重连后订阅恢复。 |

---

## 七、文档与交付物（给 App 团队）

| 交付物 | 用途 |
| --- | --- |
| [mqtt_protocol_summary.md](./mqtt_protocol_summary.md) | 复制即用的 Topic 与 JSON 示例 |
| [mqtt_protocol.md](./mqtt_protocol.md) | `state` 全字段、`lifecycle` 事件、错误码、Retain 说明 |
| 本文 | 分工、测试、上架前检查 |

---

## 八、可选：后端配合（B 强烈建议）

若 App **不直连同一套 Broker 账号**，通常需要：

- 用户登录、设备列表 API；
- 生成 **`routeDownload` 用预签名 URL**（含 `size`、可选 CRC）；
- 可选：**消息桥接**（App → 后端 → MQTT），避免客户端持有机型级密码。

---

**结论**：仅凭 `mqtt_protocol_summary.md` 可以做出 **可用原型**；补齐 **第二节 + 第四节 + `mqtt_protocol.md` 字段表 + 第五节** 后，才接近 **可上架的完整方案**。