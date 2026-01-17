# 文件日志系统使用说明

## 📦 模块位置

`FileLogger` 位于 **network 模块**，包名：`com.dji.util.FileLogger`

### 使用方式

#### 在 sample 模块中使用：
```kotlin
import com.dji.util.FileLogger

// 初始化（已在 Application.onCreate() 中自动初始化，无需手动调用）
// FileLogger.init(context, enableCrashHandler = true)  // 全局异常捕获已启用

// 使用
FileLogger.i("TAG", "日志消息")
```

#### 在 uxsdk 模块中使用：
```kotlin
import com.dji.util.FileLogger

// 初始化（在 Application.onCreate() 中调用）
FileLogger.init(context, enableCrashHandler = true)  // enableCrashHandler: 是否启用全局异常捕获

// 使用
FileLogger.i("TAG", "日志消息")
```

## 📁 日志文件位置

所有日志文件保存在**外部存储的 Download 目录**，方便随时访问：
```
/storage/emulated/0/Download/AppLogs/
```

日志文件命名格式：`log_yyyyMMdd.txt`
例如：`log_20251124.txt`

### 为什么选择 Download 目录？
- ✅ 任何人都可以通过文件管理器直接访问
- ✅ 不需要 Root 权限
- ✅ 可以通过 USB 连接电脑后直接复制
- ✅ 可以通过微信/QQ 等应用直接分享

## 🔍 查看日志

### 方法 1：使用文件管理器（最简单）
1. 打开文件管理器 App
2. 进入 **Download（下载）** 文件夹
3. 打开 **AppLogs** 文件夹
4. 选择对应日期的日志文件查看
5. 可以直接分享或发送到微信/QQ

### 方法 2：使用 ADB
```bash
# 查看今天的日志
adb shell cat /storage/emulated/0/Download/AppLogs/log_$(date +%Y%m%d).txt

# 实时查看日志（类似 tail -f）
adb shell tail -f /storage/emulated/0/Download/AppLogs/log_$(date +%Y%m%d).txt

# 导出日志到电脑
adb pull /storage/emulated/0/Download/AppLogs/ ~/Desktop/logs/
```

### 方法 3：USB 连接电脑
1. 用 USB 连接手机和电脑
2. 在电脑上打开手机存储
3. 进入 `Download/AppLogs/` 文件夹
4. 直接复制日志文件到电脑

## 📝 日志格式

每行日志格式：
```
[时间戳] [日志级别] [TAG] [线程名:线程ID] 消息内容
```

示例：
```
[2025-11-24 15:30:45.123] [INFO] [DeviceDataManager] [DefaultDispatcher-worker-1:12345] 数据更新完成, 耗时=5ms, 航点索引=3
[2025-11-24 15:30:45.234] [THREAD] [FlightDataReport] [DefaultDispatcher-worker-1:12345] 协程启动 - 飞行数据上报循环
[2025-11-24 15:30:45.345] [DATA] [DeviceDataManager] [WaypointMissionManager-Thread:9876] [数据更新] 航线监听器更新航点索引 | old=2, new=3 | 线程: WaypointMissionManager-Thread
```

## 🎯 关键日志标记

系统会在以下关键位置记录日志：

### 1. **线程信息** (`FileLogger.thread`)
- 协程启动/结束时的线程名
- 数据更新操作时的线程名
- 用于追踪线程安全问题

### 2. **数据更新** (`FileLogger.dataUpdate`)
- 每次 `updateAllData()` 调用
- 航线监听器回调
- 记录耗时和数据变化

### 3. **Activity 生命周期**
- `DJIMainActivity.onCreate()`
- `DJIMainActivity.onDestroy()`
- `DefaultLayoutActivity` 启动

### 4. **协程任务**
- 飞行数据上报循环启动/结束
- MQTT 发布成功/失败
- 异常捕获

## 🔧 调试崩溃问题

### 步骤 1：重现问题
1. 启动 App
2. 进入 DefaultLayoutActivity
3. 执行航线任务
4. 观察是否崩溃

### 步骤 2：查看日志
```bash
# 查看今天的日志
adb shell cat /storage/emulated/0/Download/AppLogs/log_$(date +%Y%m%d).txt | tail -500

# 查找 ERROR 级别日志
adb shell grep "ERROR" /storage/emulated/0/Download/AppLogs/log_$(date +%Y%m%d).txt

# 查找线程相关日志
adb shell grep "THREAD\|DATA" /storage/emulated/0/Download/AppLogs/log_$(date +%Y%m%d).txt
```

### 步骤 3：分析关键信息
查看崩溃前的最后几条日志，关注：
- **线程冲突**：不同线程同时访问 `deviceData`
- **异常堆栈**：ERROR 级别的日志包含完整堆栈
- **资源清理**：`onDestroy()` 是否正确执行
- **协程状态**：协程是否正常结束

## 🧹 自动清理

系统会自动清理 **7 天前** 的日志文件，避免占用过多存储空间。

## 💥 全局异常捕获

系统会自动捕捉**未捕获的异常**（导致应用崩溃的异常），并记录到日志文件中。

### 记录的信息包括：
- 异常类型和消息
- 完整的异常堆栈
- 崩溃线程信息
- 崩溃时间戳

### 示例日志格式：
```
[2025-11-24 15:30:45.123] [ERROR] [CRASH] [main:12345] ========================================
[2025-11-24 15:30:45.123] [ERROR] [CRASH] [main:12345] 未捕获的异常导致应用崩溃
[2025-11-24 15:30:45.123] [ERROR] [CRASH] [main:12345] 崩溃线程: main [1]
[2025-11-24 15:30:45.123] [ERROR] [CRASH] [main:12345] 异常类型: java.lang.NullPointerException
[2025-11-24 15:30:45.123] [ERROR] [CRASH] [main:12345] 异常消息: Attempt to invoke virtual method...
异常堆栈:
  at com.app.transmission.DJIMainActivity.onCreate(DJIMainActivity.kt:123)
  at android.app.Activity.performCreate(Activity.java:1234)
  ...
```

### 启用/禁用全局异常捕获：

```kotlin
// 启用（默认已启用）
FileLogger.init(context, enableCrashHandler = true)

// 禁用
FileLogger.init(context, enableCrashHandler = false)

// 手动取消注册
FileLogger.unregisterCrashHandler()
```

## 💡 注意事项

1. **日志文件大小**：高频写入可能导致日志文件较大，建议定期清理
2. **性能影响**：文件写入操作会轻微影响性能，已在关键位置做了优化（减少频繁写入）
3. **权限要求**：需要存储权限才能写入日志文件
4. **全局异常捕获**：已自动启用，崩溃信息会自动保存到日志文件

