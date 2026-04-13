# V1.2.5 发行说明（相对 V1.2.0）

**当前发行版：** V1.2.5（2026-04-12）

---

## V1.2.5 主要变更

| 项 | 说明 |
|------|------|
| RTMP 推流精简 | `FpvRtmpStreamer` 移除飞控 `KeyConnection` 监听及「航线结束 + 飞控重连」触发的 stop/start；保留会话 start/stop、`LiveStreamStatus` 掉线与 `onError` 的指数退避重试。 |
| 回调链移除 | 删除 `WaypointMissionStateManager` 的 `onWaypointMissionFinished` 及 `DeviceDataManager` 中对 `FpvRtmpStreamer.notifyWaypointMissionEnded()` 的调用；**不影响** `WaypointMissionStateManager` 在飞控重连后重新注册航线监听的逻辑。 |

---

## 一、多镜头存储设置（UXSDK，V1.2.0 起）

| 项 | 说明 |
|------|------|
| 行为修复 | `CameraStreamSettingsDialog` 打开时仅 **读取** 当前 `KeyCaptureCameraStreamSettings` 回填勾选；回填期间通过 `isLoading` 屏蔽 `OnCheckedChangeListener`，避免误触发 `applySettings()` 多次写入飞机，减少「拍照设置失败」误报。 |

---

## 二、飞行数据 / MQTT 航点索引

| 项 | 说明 |
|------|------|
| 飞控重连 | 由 `WaypointMissionStateManager` 监听 `FlightControllerKey.KeyConnection`；当 **之前未处于已连接** 且 **当前已连接**（`!wasConnected && connected`，含首次 `null → true`）时，仅调用 `reRegisterWaypointListeners()`，重新挂载 `WaypointMissionManager` 的航点与任务状态监听，避免飞机关机再开后 `currentWaypointIndex` 长期不更新、需重启 App 的问题。**该路径不自动调用** `DeviceDataManager.resetMissionState()`；任务字段清空与 `resetMissionState()` 由上层显式逻辑触发。 |

---

## 三、协议文档

- 项目根目录 **`断点续飞协议说明.md`**：MQTT 双入口（`uav/control` / `pauseResumeMission`）、前置 `setTaskFile`、`pause+stop` 与续飞分支、成功/失败条件及 JSON 示例，供后端对接参考。

---

**最后更新：** 2026-04-12
