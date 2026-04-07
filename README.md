# V1.1.8 新增说明（相对 V1.1.7）

**当前发行版：** V1.1.8（2026-04-07）

---

## 一、多镜头存储设置（UXSDK）

| 项 | 说明 |
|------|------|
| 弹窗 | 新增 `CameraStreamSettingsDialog` + `uxsdk_dialog_camera_stream_settings.xml` |
| 入口 | 默认布局相机区旁 **「C」** 按钮（`DefaultLayoutActivity`） |
| 能力 | 可勾选拍照/录像保存的镜头及「当前画面」；`KeyCameraVideoStreamSourceRange` 动态列出可选镜头 |
| API | `KeyCaptureCameraStreamSettings` / `KeyRecordCameraStreamSettings` 分别写入拍照与录像存储镜头 |

---

## 二、飞行数据面板

- **AMSL**：`FlightDataViewModel` 监听 `KeyAltitude` 与 `KeyTakeoffLocationAltitude`，推算海拔并暴露 `altitudeAmslMeters`。
- **当前航点**：注册 `WaylineExecutingInfoListener`，与 sample 侧 `DeviceDataManager` / MQTT `currentWaypointIndex` 同源；无航线或未回调时界面显示 `--`。
- **布局**：`FlightDataWidget` 使用 `wrap_content`，约束在顶栏下方，避免遮挡 FPV。

---

## 三、航线与任务诊断（不改变业务逻辑）

- **DeviceDataManager**：航点索引变化、任务执行状态切换、中断、`resetMissionState`、`destroy`、`clearAllData` 等增加 `[DIAG-*]` 日志；`updateCurrentTaskStatus` 增加节流诊断；检测「飞行中且 mission 活跃但 `currentWaypointIndex` 为 null」等异常场景。
- **TaskService**：`startMission` 前/失败后再打 **高度诊断** 日志；从 KMZ 内 kml/xml 粗提取 `executeHeight`、`heightMode`、`wpml:height` 等线索，辅助排查「航线高度过高」类启动失败。

---

## 四、主界面布局与样式

- 移除 `btn_swingParms_setting`；`SimulatorControlWidget` 嵌套至 `FPVInteractionWidget` 内。
- `uxsdk_background_black_rectangle`：圆角与背景色调整（弹窗/按钮共用）。

---

**最后更新：** 2026-04-07
