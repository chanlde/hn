# RTK / D-RTK3 / 基站模块规划说明

本巡检 Sample 应用（`android-sdk-v5-sample`）**当前未实现** RTK 网络服务、移动站/基站管理、D-RTK3 位置标定等功能。若需在大疆官方 App 外完成标定与基站配置，建议按下列方向在工程中**单独立项**开发。

## 1. 目标能力

- RTK 服务开关与状态展示（固定解 / 浮点解、定位类型等）。
- D-RTK3 / 基站：搜索、连接、基站坐标输入与标定（换位置后需重新标定，与飞手端大疆 App 行为一致）。
- 可选：网络 RTK（RTK Center）账号与挂载点配置。

## 2. 建议使用的 MSDK V5 能力（以官方文档为准）

- `RTKCenter` 或等价 RTK 网络管理入口（按当前 SDK 版本查文档）。
- `RtkMobileStationKey`：移动站状态、坐标、精度等。
- 基站侧 Key（如 `RTKBaseStationKey` 或文档中的 D-RTK 基站 API）：连接状态、基站位置设置等。

## 3. UI 与集成建议

- 新建「基站 / RTK」设置页：状态列表 + 标定向导（分步说明：需飞机悬停、搜星等）。
- 与现有 `DeviceDataManager` 上报扩展：可增加 `rtkFixType`、`rtkPositionAccuracy` 等字段（需与平台协议对齐）。
- 参考同仓库 `android-sdk-v5-uxsdk` 中与 RTK 相关的 Widget/Model 实现思路，**不要直接复制依赖**，按业务精简接入。

## 4. 风险与测试

- 不同机型（M3E/M30/M350 等）对 ComponentIndex、RTK 硬件通道支持不同，需在真机矩阵验证。
- 标定失败时的重试与安全提示（低卫星、电磁环境等）。

## 5. 当前代码状态

- Sample 内 Kotlin/Java **无** RTK 相关调用；`strings.xml` 中部分文案来自示例资源，**无对应业务代码**。
- D-RTK3 换点后仍需使用**大疆官方 App 标定**的现状，在本工程修复前仍然成立；完成本模块后可改为本 App 内标定（取决于 SDK 是否开放同等能力）。
