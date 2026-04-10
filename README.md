# V1.2.0 新增说明（相对 V1.1.8）

**当前发行版：** V1.2.0（2026-04-10）

---

## 一、多镜头存储设置（UXSDK）

| 项 | 说明 |
|------|------|
| 行为修复 | `CameraStreamSettingsDialog` 打开时仅 **读取** 当前 `KeyCaptureCameraStreamSettings` 回填勾选；回填期间通过 `isLoading` 屏蔽 `OnCheckedChangeListener`，避免误触发 `applySettings()` 多次写入飞机，减少「拍照设置失败」误报。 |

---

## 二、飞行数据 / MQTT 航点索引

| 项 | 说明 |
|------|------|
| 飞控重连 | `DeviceDataManager` 监听 `FlightControllerKey.KeyConnection`；在 **断连 → 已连接**（含首次 `null → true`）时调用 `resetMissionState()` 并 `reRegisterWaypointListeners()`，重新挂 `WaypointMissionManager` 的航点与任务状态监听，避免飞机关机再开后 `currentWaypointIndex` 长期不更新、需重启 App 的问题。 |

---

## 三、协议文档

- 项目根目录 **`断点续飞协议说明.md`**：MQTT 双入口（`uav/control` / `pauseResumeMission`）、前置 `setTaskFile`、`pause+stop` 与续飞分支、成功/失败条件及 JSON 示例，供后端对接参考。

---

**最后更新：** 2026-04-10
