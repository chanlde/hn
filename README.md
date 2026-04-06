# 功能总览

**当前发行版：V1.1.7（2026-04-06）** — 本版本相对上一版的全部变更见文末 **「最新更新（V1.1.7）」**（该节仅描述本次发布，不含历史版本罗列）。

基于大疆 MSDK V5 开发的自定义功能列表

## 项目概述

本项目基于 DJI Mobile SDK V5 开发，在原有 SDK 基础上扩展了以下核心功能：
- MQTT 消息通信系统
- 航线任务管理
- 相机媒体文件管理
- 文件上传与存储
- 定位服务
- 断点续飞功能
- 飞行数据实时上报
- 配置管理系统

---

## 核心功能列表

### 1. MQTT 消息处理系统

#### 1.1 支持的命令主题

| 主题 | 功能 | 说明 |
|------|------|------|
| `/api/work/setTaskFile_{key}` | 航线任务下发 | 下载 KMZ 文件并上传到飞机 |
| `/api/machine/uav/control_{key}` | UAV 控制命令 | 起飞、降落、返航、拍照、录像、航线控制、相机模式、控制权 |
| `/api/work/setHomeLocation_{key}` | 设置返航点 | 设置飞机返航位置 |

#### 1.2 UAV 控制命令 (type)

- `type = 1`: 起飞
- `type = 2`: 降落（parameter: 1=普通降落, 2=视觉降落）
- `type = 3`: 一键返航
- `type = 4`: 拍照
- `type = 5`: 录像（parameter: 1=开始录像, 2=停止录像）
- `type = 6`: 航线控制（parameter: 1=航线暂停, 2=航线继续, 3=终止并悬停, 4=终止并按航线策略返航）
- `type = 7`: 相机模式（parameter: 1=广角, 2=长焦, 3=红外）*待实现*
- `type = 9`: 控制权模式（parameter: 1=控制权获取, 2=控制权释放）*待实现*

**相关文件：**
- `MqttMessageHandler.kt` - MQTT 消息路由中心
- `MessageParser.kt` - 消息解析工具

---

### 2. 任务管理功能

#### 2.1 航线任务生命周期管理

- **自动开始任务**：起飞时自动开始任务
- **自动结束任务**：降落时自动结束任务
- **任务文件夹管理**：按时间戳创建任务文件夹
- **任务ID管理**：使用下发的任务ID作为文件夹标识

#### 2.2 航线任务下载与上传

- 从 URL 下载 KMZ 文件
- 自动推送到飞机
- 自动启动航线任务

**相关文件：**
- `TaskService.kt` - 航线任务服务
- `MissionTaskManager.kt` - 任务生命周期管理器
- `MissionFolderManager.kt` - 任务文件夹管理器
- `FileDownloader.kt` - 文件下载工具

---

### 3. 相机服务

#### 3.1 拍照功能

- 支持可见光相机拍照
- 支持红外相机拍照
- 自动保存到任务文件夹

#### 3.2 录像功能

- 开始/停止录像
- 自动保存到任务文件夹

#### 3.3 媒体文件管理

- **自动下载**：拍照/录像后自动下载媒体文件
- **自动分类**：自动区分可见光（CCD）和红外（FIR）照片
- **文件命名识别**：通过文件名后缀判断相机类型
  - `_T.JPG` / `_T.JPEG` - 红外相机（FIR）
  - `_Z.JPG` / `_Z.JPEG` - 可见光相机（CCD）
- **自动上传**：下载后自动上传到 MinIO 服务器
- **自动删除**：上传成功后自动删除本地文件

**相关文件：**
- `CameraService.kt` - 相机服务

---

### 4. 文件上传与管理

#### 4.1 文件上传路径结构

```
{stationCode}/{年}/{月}/{日}/{任务ID}/{相机类型}/{文件名}
```

**示例：**
```
123456/2026/01/17/task_WH001001_20250101_001/FIR/DJI_20260117174155_0011_T.JPG
123456/2026/01/17/task_WH001001_20250101_001/CCD/DJI_20260117174156_0011_Z.JPG
```

#### 4.2 配置项

- **场站代码 (station_code)**: 在 `ConfigManager` 中配置
- **存储桶 (bucket_name)**: 在 `ConfigManager` 中配置（默认：`cloud-bucket-uav`）
- **任务ID**: 从 MQTT 消息 `TaskFileRequest.taskId` 获取

#### 4.3 上传流程

1. 拍照/录像 → 保存到本地任务文件夹
2. 自动下载媒体文件
3. 识别相机类型（FIR/CCD）
4. 构造上传路径
5. 上传到 MinIO
6. 上传成功后删除本地文件

**相关文件：**
- `CameraService.kt` - 文件上传逻辑
- `MissionFileUploader.kt` - 文件上传工具
- `ConfigManager.kt` - 配置管理

---

### 5. 定位服务

#### 5.1 GPS 定位功能

- **实时位置更新**：持续监听位置变化
- **不受生命周期影响**：在 Application 级别管理，不受 Activity 生命周期影响
- **位置持久化**：保存最后一次获取的位置

#### 5.2 遥控器位置获取

- 通过 Android LocationManager 获取遥控器 GPS 位置
- 位置信息添加到状态包中（`handsetLatitude` / `handsetLongitude`）
- 支持用于设置返航点

#### 5.3 权限检查

- 自动检查位置权限（ACCESS_FINE_LOCATION / ACCESS_COARSE_LOCATION）
- 自动检查定位服务是否启用
- 友好的错误提示

**相关文件：**
- `LocationService.kt` - 定位服务管理器
- `RealLocation.kt` - 底层定位工具
- `DJIApplication.kt` - Application 级别初始化

---

### 6. 航线控制功能（已整合到 UAV 控制命令）

#### 6.1 航线控制命令（type = 6）

通过统一的 UAV 控制命令 `/api/machine/uav/control_{key}` 进行航线控制：

- **parameter = 1**：航线暂停
  - 调用 `WaypointMissionManager.pauseMission()` 暂停当前执行的任务
  - 暂停后自动停止任务

- **parameter = 2**：航线继续（从断点恢复）
  - **自动判断场景**：
    - **空中恢复**：飞机在空中时使用 `resumeMission(BreakPointInfo)`
    - **地面重启**：飞机在地面时使用 `stopMission` + `startMission`
  - **状态判断**：
    - 优先使用 `KeyIsFlying` 判断是否在空中
    - 备用方案：查询飞机高度（阈值：2米）

- **parameter = 3**：终止并悬停
  - 调用 `WaypointMissionManager.stopMission()` 停止任务
  - 飞机在当前位置悬停

- **parameter = 4**：终止并按航线策略返航
  - 调用 `WaypointMissionManager.stopMission()` 停止任务
  - 停止成功后自动执行返航

#### 6.2 断点信息查询

- 查询飞机上的断点信息
- 使用任务文件名查询（从 `TaskFileRequest.key` 获取）
- 支持断点信息详情打印（航线ID、航点ID、位置等）

**相关文件：**
- `MissionControlService.kt` - 任务控制服务
- `MqttMessageHandler.kt` - MQTT 消息处理（`handleMissionControl()` 方法）

---

### 7. 飞行数据实时上报

#### 7.1 上报频率

- 每 200ms 上报一次（每秒 5 次）
- 通过 MQTT 发布到 `/api/machine/uav/info_{key}`

#### 7.2 上报数据内容

包含以下字段：
1. `tid` - 消息ID
2. `connection` - 连接状态
3. `isFlying` - 是否飞行
4. `flightTimeInSeconds` - 飞行时间（秒）
5. `aircraftLocation3D` - 飞机位置（经度、纬度、高度）
6. `aircraftAttitude` - 飞机姿态（俯仰、横滚、偏航）
7. `aircraftVelocity` - 飞机速度（水平速度、垂直速度）
8. `takeoffLocationAltitude` - 起飞点海拔
9. `satelliteCount` - 卫星数量
10. `GNSSSignalLevel` - GNSS信号等级
11. `compassHeading` - 罗盘航向
12. `compassHasError` - 罗盘错误
13. `ultrasonicHeight` - 超声波高度
14. `windWarning` - 风力警告
15. `windSpeed` - 风速
16. `windDirection` - 风向
17. `currentWaypointIndex` - 当前航点索引
18. `flightMode` - 飞行模式
19. `handsetLatitude` - 遥控器纬度（GPS获取）
20. `handsetLongitude` - 遥控器经度（GPS获取）
21. `currentTaskStatus` - 当前任务状态（0=待命, 2=执行航线, 3=返航, 6=降落完成, 8=下载媒体, 9=上传媒体, 10=任务中断, -1=未知）
22. `waypointMissionExecuteState` - 航线任务执行状态（IDLE, READY, UPLOADING, PREPARING, ENTER_WAYLINE, EXECUTING, INTERRUPTED, RECOVERING, FINISHED 等）

**相关文件：**
- `FlightDataReport.kt` - 飞行数据上报器
- `DeviceDataManager.kt` - 设备数据管理器
- `FlightReportData` - 上报数据格式定义

---

### 8. 配置管理系统

#### 8.1 配置文件

- **位置**：`/storage/emulated/0/Android/data/com.app.transmission/files/app_config.properties`
- **格式**：Properties 文件
- **特点**：外部存储，修改后重启 App 生效

#### 8.2 配置项列表

| 配置项 | 说明 | 默认值 |
|--------|------|--------|
| `rtmp_url` | RTMP 推流地址 | `rtmp://192.168.0.117:1935/live/stream` |
| `minio_endpoint` | MinIO 服务器地址 | `http://192.168.1.201:18005` |
| `minio_access_key` | MinIO 访问密钥 | `minio` |
| `minio_secret_key` | MinIO 密钥 | `UK13@ukdq` |
| `bucket_name` | 存储桶名称 | `cloud-bucket-dji` |
| `station_code` | 场站代码 | `123456` |
| `serverHost` | MQTT 服务器地址 | `192.168.0.122` |
| `serverPort` | MQTT 服务器端口 | `1883` |
| `clientId` | MQTT 客户端ID | `987652` |
| `username` | MQTT 用户名 | `emqx` |
| `password` | MQTT 密码 | `UK13@ukdq` |

#### 8.3 设备ID列表

支持多个设备ID，自动匹配：

```kotlin
fcDeviceIdList = listOf(
    "1581F8DBW256500A2NKB",
    "1581F8DBW255D00A2LD4",
    "1581F6GKB24C400408TU",
    "1581F6GKB237F003001L",
    "1581F7K3325AV00AR049"
)
```

**相关文件：**
- `ConfigManager.kt` - 配置管理器

---

### 9. 自动任务管理

#### 9.1 起飞自动开始任务

- 监听 `KeyIsFlying` 从 false → true
- 自动调用 `MissionTaskManager.startMission()`
- 创建任务文件夹
- 设置相机服务任务路径

#### 9.2 降落自动结束任务

- 监听 `KeyIsFlying` 从 true → false
- 自动调用 `MissionTaskManager.endMission()`
- 触发任务文件上传

**相关文件：**
- `MissionStateListener.kt` - 任务状态监听器
- `MissionTaskManager.kt` - 任务管理器

---

### 10. 降落确认监听器

#### 10.1 自动确认降落

- 监听 `KeyIsLandingConfirmationNeeded`
- 自动调用 `KeyConfirmLanding` 确认降落
- 无需人工干预

**相关文件：**
- `LandingConfirmationListener.kt` - 降落确认监听器

---

### 11. 设置返航点功能

#### 11.1 MQTT 命令

- 主题：`/api/work/setHomeLocation_{key}`
- 支持传入经纬度或自动使用 GPS 位置

#### 11.2 位置来源优先级

1. **MQTT 消息中的经纬度**（优先使用）
2. **GPS 获取的位置**（如果 MQTT 中的位置无效）
3. **错误响应**（如果两者都不可用）

#### 11.3 位置保存

- 保存遥控器位置（从 MQTT 消息接收）
- 用于后续设置返航点

**相关文件：**
- `MqttMessageHandler.kt` - `handleSetHomeLocation()` 方法
- `FlightControlService.kt` - `setHomeLocation()` 方法

---

## 技术架构

### 模块划分

1. **核心服务层**：FlightControlService、TaskService、MissionControlService、CameraService
2. **监听器层**：LandingConfirmationListener、MissionStateListener、FlightDataReport
3. **管理器层**：MissionTaskManager、MissionFolderManager、DeviceDataManager
4. **工具层**：FileDownloader、MessageParser、LocationService
5. **协调层**：MqttMessageHandler（消息路由中心）

详细架构说明请参考：[README_ARCHITECTURE.md](src/main/java/dji/sampleV5/aircraft/mqtthandle/README_ARCHITECTURE.md)

---

## 版本更新记录

### v1.1.7 (2026-04-06)

**飞行数据与状态上报**

- `FlightReportData` / MQTT 状态包新增字段：`relativeAltitudeFromTakeoff`（相对起飞点高度）、`altitudeAMSL`（推算海拔）、`currentTaskStatus`、`waypointMissionExecuteState`、`cameraMode`；`AircraftData` 增加 `relativeAltitudeFromTakeoff`、`cameraShootingModeName`，并由 `DeviceDataManager` 从 MSDK Key 与航线监听同步。

**航线控制与上传**

- `MissionControlService.pauseMission` 增加 `missionFileName`，暂停成功后串联 `stopMission`，与「暂停即结束当前执行」的预期一致。
- 地面断点恢复路径去掉多余的「先 stop 再 start」嵌套，在通过校验后直接 `startMission(missionFileName, breakPointInfo, …)`，减少无效停止与失败点。
- `MissionFileUploader`：使用 `SupervisorJob` 管理上传协程，提供 `cancelPendingUploads()` 与 `CameraService` 生命周期对齐；任务文件夹内照片/视频按**文件名排序**后再收集上传，顺序稳定。

**RTMP 与界面**

- `StreamConfig`：`autoReconnectCount == 0` 语义明确为**无限重连直至 `stopStreaming`**；默认重连基础间隔由 3000ms 调整为 **1000ms**（仍为指数退避上限 30s）。
- 主界面 `activity_main.xml` 增加「当前版本」展示（`app_version_tv`，与 `DJIMainActivity` 中 `BuildConfig.VERSION_NAME` 绑定）。

**日志与可观测性（不改变业务分支逻辑）**

- `FileLogger`（network 模块）：`fileLoggingEnabled`、`minLevelForFile`、`applyDefaultPolicy`（Debug 全量写文件 / Release 默认 INFO 及以上写文件）、`throttledD`、`logStateChange`；未捕获异常合并为**单条**崩溃记录。
- 热点模块精简刷屏日志并统一关键路径：`CameraService`、`FpvRtmpStreamer`、`VideoFrameProcessor`、`MqttMessageHandler`、`DeviceDataManager`、`FlightDataReport`、`DJIMainActivity`。
- `LogManager`：列表最多 **500** 条，连续相同条目合并为 ` (×N)`。

**说明：** 上述日志类改动仅调整输出方式、级别与节流，**不修改** MQTT 路由、任务状态机、下载上传判定、RTMP 送帧与重连策略等业务逻辑。

---

### v1.1.4 (2026-01-17)

**新增功能：**
- ✅ 航线任务执行状态监听（WaypointMissionExecuteState）
- ✅ 航线控制功能整合到 UAV 控制命令（type=6）
- ✅ 任务状态字段添加到状态包（currentTaskStatus）
- ✅ 航线任务执行状态字段添加到状态包（waypointMissionExecuteState）

**优化：**
- ✅ 断点续飞功能整合到统一控制命令，不再需要单独的 MQTT 主题
- ✅ 支持终止并悬停、终止并返航两种停止方式
- ✅ 状态包数据更完整，包含任务状态和执行状态

### v1.1.3 (2026-01-17)

**新增功能：**
- ✅ 航线任务执行状态监听（WaypointMissionExecuteState）
- ✅ 航线控制功能整合到 UAV 控制命令（type=6）
- ✅ 任务状态字段添加到状态包（currentTaskStatus）
- ✅ 航线任务执行状态字段添加到状态包（waypointMissionExecuteState）

**优化：**
- ✅ 断点续飞功能整合到统一控制命令，不再需要单独的 MQTT 主题
- ✅ 支持终止并悬停、终止并返航两种停止方式
- ✅ 状态包数据更完整，包含任务状态和执行状态

### v1.1.2 (2026-01-17)

**新增功能：**
- ✅ MQTT 消息处理系统
- ✅ 航线任务管理（下载、上传、启动）
- ✅ 相机服务（拍照、录像）
- ✅ 文件上传到 MinIO（结构化路径）
- ✅ 可见光/红外照片自动分类（CCD/FIR）
- ✅ 文件上传成功后自动删除
- ✅ GPS 定位服务（Application 级别）
- ✅ 遥控器位置添加到状态包
- ✅ 断点续飞功能（暂停/恢复，自动判断空中/地面）
- ✅ 飞行数据实时上报（200ms 周期）
- ✅ 设置返航点功能（支持 MQTT 或 GPS 位置）
- ✅ 自动任务管理（起飞开始/降落结束）
- ✅ 降落确认监听器（自动确认）
- ✅ 配置管理系统（外部配置文件）

**优化：**
- ✅ 定位服务不受 Activity 生命周期影响
- ✅ 位置权限检查优化（支持 FINE 和 COARSE）
- ✅ MQTT 响应消息循环防止
- ✅ 文件路径结构优化（场站code/年/月/日/任务ID/相机类型/文件名）

---

## 注意事项

1. **生命周期管理**：所有监听器类必须在不再使用时调用 `destroy()` 方法
2. **权限要求**：需要位置权限（ACCESS_FINE_LOCATION 或 ACCESS_COARSE_LOCATION）
3. **配置更新**：修改配置文件后需要重启 App 才能生效
4. **文件上传**：上传成功后会自动删除本地文件，请确保已备份重要数据
5. **断点续飞**：如果无法判断飞机状态，默认使用地面重启方式（更安全）

---

## 依赖关系

- **DJI Mobile SDK V5**：核心 SDK
- **MQTT 客户端**：消息通信
- **MinIO 客户端**：文件上传
- **Gson**：JSON 序列化/反序列化
- **Kotlin Coroutines**：异步任务处理

---

## 开发说明

### 项目结构

```
android-sdk-v5-sample/
├── src/main/java/dji/sampleV5/aircraft/
│   ├── mqtthandle/          # MQTT 处理模块
│   │   ├── MqttMessageHandler.kt      # 消息路由中心
│   │   ├── CameraService.kt           # 相机服务
│   │   ├── TaskService.kt             # 任务服务
│   │   ├── MissionControlService.kt   # 任务控制服务
│   │   └── ...
│   ├── manager/             # 管理器模块
│   │   ├── LocationService.kt         # 定位服务
│   │   └── ...
│   └── data/                # 数据模型
│       ├── FlightReportData.kt        # 飞行数据格式
│       └── ...

android-sdk-v5-network/
└── src/main/java/com/dji/network/
    ├── ConfigManager.kt     # 配置管理器
    └── ...
```

### 配置文件位置

配置文件位于外部存储，路径：
```
/storage/emulated/0/Android/data/com.app.transmission/files/app_config.properties
```

---

**最后更新日期：** 2026-04-06

---

## 最新更新（V1.1.7）

本节**仅**记录 **V1.1.7** 相对上一版的变更（与上文「版本更新记录 → v1.1.7」一致，便于文末快速查阅）。

### 一、飞行数据与状态上报

| 类型 | 变更 |
|------|------|
| `FlightReportData` / MQTT JSON | 新增 `relativeAltitudeFromTakeoff`、`altitudeAMSL`、`currentTaskStatus`、`waypointMissionExecuteState`、`cameraMode` |
| `AircraftData` | 新增 `relativeAltitudeFromTakeoff`、`cameraShootingModeName` |
| `DeviceDataManager` | 从飞控 Key 与航线监听填充上述字段，并参与周期上报 |

### 二、航线控制与任务上传

- **暂停任务**：`MissionControlService.pauseMission` 需传入 `missionFileName`；`pauseMission` 成功后会调用 `stopMission`，避免仅暂停未停止导致状态与预期不符。
- **地面断点恢复**：去掉多余的「先 `stopMission` 再 `startMission`」；校验通过后直接 `startMission(missionFileName, breakPointInfo, …)`。
- **任务文件上传**：`MissionFileUploader` 使用 `SupervisorJob`，支持 `cancelPendingUploads()`；照片/视频目录内文件按**文件名排序**后上传，顺序可复现。

### 三、RTMP 与主界面

- **StreamConfig**：`autoReconnectCount == 0` 表示**无限重连**直到 `stopStreaming`；`reconnectIntervalMs` 默认 **1000ms**（指数退避，上限 30s）。
- **主界面**：增加「当前版本」文案与 `app_version_tv`，显示 `BuildConfig.VERSION_NAME`。

### 四、日志系统（无业务逻辑变更）

- **FileLogger**：文件开关、按级别写盘、Debug/Release 默认策略、`throttledD`、`logStateChange`、崩溃单条记录。
- **样例工程**：`CameraService`、`FpvRtmpStreamer`、`VideoFrameProcessor`、`MqttMessageHandler`、`DeviceDataManager`、`FlightDataReport`、`DJIMainActivity` 等热点日志精简与关键路径加强。
- **LogManager**：最多 500 条、连续重复合并为 ` (×N)`。

---

**历史版本说明：** v1.1.4 及更早的航线控制整合、状态包字段等说明已写入上文「版本更新记录」对应小节，本文末不再重复展开。
