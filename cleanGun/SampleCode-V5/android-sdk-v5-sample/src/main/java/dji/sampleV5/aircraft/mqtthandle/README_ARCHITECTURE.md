# MQTT Handle 模块架构说明

## 📁 目录结构

```
mqtthandle/
├── 核心服务层（Service Layer）
│   ├── FlightControlService.kt          # 飞行控制服务（起飞/降落/返航）
│   ├── TaskService.kt                   # 航线任务服务（下载/上传/启动）
│   ├── MissionControlService.kt         # 任务控制服务（暂停/恢复/停止）- 预留
│   ├── CameraService.kt                 # 相机服务（拍照/录像/媒体文件管理）
│   └── DeviceDataManager.kt             # 设备数据管理器（Key轮询 + 监听器）
│
├── 监听器层（Listener Layer）
│   ├── LandingConfirmationListener.kt   # 降落确认监听器（自动确认降落）
│   ├── MissionStateListener.kt          # 任务状态监听器（起飞开始任务/降落结束任务）
│   └── FlightDataReport.kt              # 飞行数据上报器（周期性上报到MQTT）
│
├── 管理器层（Manager Layer）
│   ├── MissionTaskManager.kt            # 任务生命周期管理器
│   ├── MissionFolderManager.kt          # 任务文件夹管理器
│   └── MissionFileUploader.kt           # 任务文件上传器
│
├── 工具层（Utility Layer）
│   ├── FileDownloader.kt                # 文件下载工具
│   └── MessageParser.kt                 # 消息解析工具
│
└── 协调层（Coordinator Layer）
    └── MqttMessageHandler.kt            # MQTT消息处理器（消息路由中心）
```

---

## 🔄 重命名历史

### 2025-11-26 架构优化

| 原类名 | 新类名 | 原因 |
|-------|--------|------|
| `FlightListener` | `LandingConfirmationListener` | 原名太宽泛，实际只处理降落确认 |
| `MissionFlightListener` | `MissionStateListener` | 避免与降落监听器混淆，更清晰表达职责 |

---

## 📊 职责划分

### 1. 核心服务层

#### FlightControlService
- **职责**：执行飞行控制指令
- **功能**：
  - 起飞（takeoff）
  - 降落（land）
  - 返航（returnHome）
  - 取消返航（cancelReturnHome）

#### TaskService
- **职责**：管理航线任务的上传和启动
- **功能**：
  - 下载 KMZ 文件
  - 推送航线到飞机
  - 启动航线任务

#### MissionControlService
- **职责**：控制航线任务的执行状态
- **状态**：⚠️ 预留（待实现）
- **计划功能**：
  - 暂停任务（pauseMission）
  - 恢复任务（resumeMission）
  - 停止任务（stopMission）

#### CameraService
- **职责**：管理相机操作和媒体文件
- **功能**：
  - 拍照（从视频流截帧）
  - 录像（开始/停止）
  - 媒体文件列表管理
  - 媒体文件下载
  - 自动上传到服务器

#### DeviceDataManager
- **职责**：统一管理设备数据
- **数据源**：
  - Key 轮询（电池、GPS、飞行状态等）
  - 监听器（航点索引等）
- **生命周期**：需要调用 `destroy()` 清理

---

### 2. 监听器层

#### LandingConfirmationListener
- **职责**：监听降落确认需求，自动确认
- **触发条件**：`KeyIsLandingConfirmationNeeded` 为 true
- **行为**：自动调用 `KeyConfirmLanding`
- **生命周期**：需要调用 `destroy()` 清理

#### MissionStateListener
- **职责**：监听飞行状态，自动管理任务
- **触发条件**：
  - 起飞（`KeyIsFlying` 从 false → true）→ 调用 `taskManager.startMission()`
  - 降落（`KeyIsFlying` 从 true → false）→ 调用 `taskManager.endMission()`
- **生命周期**：需要调用 `destroy()` 清理

#### FlightDataReport
- **职责**：周期性上报飞行数据到 MQTT
- **频率**：每 200ms 一次（每秒 5 次）
- **数据来源**：`DeviceDataManager`
- **生命周期**：需要调用 `destroy()` 清理

---

### 3. 管理器层

#### MissionTaskManager
- **职责**：管理任务的生命周期
- **功能**：
  - 创建任务文件夹
  - 记录任务信息
  - 触发文件上传

#### MissionFolderManager
- **职责**：管理本地任务文件夹
- **功能**：
  - 创建任务文件夹（按时间戳命名）
  - 获取照片/视频子文件夹

#### MissionFileUploader
- **职责**：上传任务相关文件到服务器
- **功能**：
  - 单个文件上传（照片/视频）
  - 整个任务文件夹上传

---

### 4. 协调层

#### MqttMessageHandler
- **职责**：MQTT 消息路由中心
- **功能**：
  - 订阅主题
  - 解析消息
  - 路由到对应的服务
- **设计原则**：不直接调用 DJI SDK，委托给各个服务类

---

## 🔄 数据流

### 飞行控制流程
```
MQTT 消息 → MqttMessageHandler → FlightControlService → DJI SDK
```

### 任务管理流程
```
起飞检测 → MissionStateListener → MissionTaskManager
                                      ↓
                              创建文件夹 + 通知 CameraService
                                      ↓
                              拍照/录像 → 自动保存
                                      ↓
降落检测 → MissionStateListener → MissionTaskManager
                                      ↓
                              触发文件上传
```

### 数据上报流程
```
FlightDataReport (定时器)
    ↓
DeviceDataManager.updateAllData()
    ↓
getData() → JSON
    ↓
MQTT 发布
```

---

## ⚠️ 注意事项

### 1. 监听器生命周期管理
所有监听器类必须在不再使用时调用 `destroy()` 方法：

```kotlin
override fun onDestroy() {
    landingConfirmationListener.destroy()  // ✅ 必须调用
    flightDataReport?.destroy()            // ✅ 必须调用
    cameraService.destroy()                // ✅ 必须调用
}
```

### 2. FPV 相机限制
- **不支持** `KeyStartShootPhoto`（传统拍照命令）
- **不支持** `KeyCameraMode`（相机模式切换）
- **解决方案**：从视频流截帧并转换为 JPEG

### 3. 航点索引获取
- **无法通过 Key 获取**
- **只能通过监听器**：`WaylineExecutingInfoListener`

### 4. MissionControlService 状态
- 当前全是空实现（TODO）
- 保留用于未来扩展

---

## 🎯 最佳实践

1. ✅ **职责单一**：每个类只负责一个明确的功能
2. ✅ **命名清晰**：类名准确反映职责
3. ✅ **生命周期管理**：所有监听器都实现 `destroy()` 方法
4. ✅ **错误处理**：所有异步操作都有成功/失败回调
5. ✅ **日志记录**：关键操作都有详细日志

---

## 📝 待办事项

- [ ] 实现 `MissionControlService` 的暂停/恢复/停止功能
- [ ] 考虑将 `TaskService` 和 `MissionControlService` 合并为 `WaypointMissionService`
- [ ] 添加单元测试
- [ ] 添加错误重试机制

---

**最后更新**：2025-11-26  
**维护者**：开发团队










