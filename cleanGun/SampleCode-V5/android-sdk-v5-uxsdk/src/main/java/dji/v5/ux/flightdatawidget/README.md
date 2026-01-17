# FlightDataWidget 使用说明

## 📋 功能概述

`FlightDataWidget` 用于显示飞行数据，目前支持：
- ✅ **位置数据**：经度、纬度、高度
- 🔄 **可扩展**：方便后期添加姿态、速度、电池等数据

## 🚀 快速开始

### 1. 在 Activity 中创建 ViewModel

```kotlin
import dji.v5.ux.flightdatawidget.FlightDataViewModel
import androidx.lifecycle.ViewModelProvider

class DefaultLayoutActivity : AppCompatActivity() {
    
    private lateinit var flightDataViewModel: FlightDataViewModel
    
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        
        // 创建 ViewModel
        flightDataViewModel = ViewModelProvider(this)[FlightDataViewModel::class.java]
    }
}
```

### 2. 绑定 ViewModel 到 Widget

```kotlin
override fun onCreate(savedInstanceState: Bundle?) {
    super.onCreate(savedInstanceState)
    setContentView(R.layout.uxsdk_activity_default_layout)
    
    // 创建 ViewModel
    flightDataViewModel = ViewModelProvider(this)[FlightDataViewModel::class.java]
    
    // 找到 Widget
    val flightDataWidget = findViewById<FlightDataWidget>(R.id.flightdata_tip)
    
    // 绑定 ViewModel（位置数据会自动更新）
    flightDataWidget.bindFlightDataViewModel(flightDataViewModel, this)
}
```

## 📊 数据结构

### LocationData（位置数据）

```kotlin
data class LocationData(
    val longitude: Double,  // 经度
    val latitude: Double,   // 纬度
    val altitude: Double,    // 高度（米）
    val isValid: Boolean     // 数据是否有效
)
```

### 显示格式

```
经度: 118.774781
纬度: 31.833779
高度: 99.96m
```

## 🔧 扩展其他数据

### 步骤 1：在 ViewModel 中添加数据类

```kotlin
// 在 FlightDataViewModel.kt 中添加
data class AttitudeData(
    val pitch: Double,  // 俯仰角
    val roll: Double,   // 横滚角
    val yaw: Double      // 偏航角
) {
    fun getFormattedString(): String {
        return "俯仰: %.2f°\n横滚: %.2f°\n偏航: %.2f°".format(pitch, roll, yaw)
    }
}
```

### 步骤 2：添加 LiveData

```kotlin
private val _attitude = MutableLiveData<AttitudeData>()
val attitude: LiveData<AttitudeData> = _attitude
```

### 步骤 3：添加监听器

```kotlin
private fun setupAttitudeListener() {
    val attitudeKey = KeyTools.createKey(FlightControllerKey.KeyAircraftAttitude)
    val disposable = RxUtil.addListener(attitudeKey, this)
        .subscribeOn(Schedulers.io())
        .observeOn(AndroidSchedulers.mainThread())
        .subscribe { attitude ->
            if (attitude != null) {
                val attitudeData = AttitudeData(
                    pitch = attitude.pitch,
                    roll = attitude.roll,
                    yaw = attitude.yaw
                )
                _attitude.postValue(attitudeData)
            }
        }
    compositeDisposable.add(disposable)
}
```

### 步骤 4：在 Widget 中观察数据

```kotlin
// 在 FlightDataWidget.kt 中
private val attitudeObserver = Observer<FlightDataViewModel.AttitudeData> { attitudeData ->
    if (isActive) {
        // 更新 UI
        text = attitudeData.getFormattedString()
    }
}

fun bindFlightDataViewModel(viewModel: FlightDataViewModel, lifecycleOwner: LifecycleOwner) {
    // ... 现有代码 ...
    
    // 观察姿态数据
    viewModel.attitude.observe(lifecycleOwner, attitudeObserver)
}
```

## 📝 注意事项

1. **生命周期管理**：ViewModel 会自动在 Activity 销毁时清理资源
2. **线程安全**：使用 `postValue()` 可在任意线程更新 LiveData
3. **数据有效性**：始终检查 `isValid` 标志，避免显示无效数据
4. **性能优化**：RxJava 监听器会自动处理数据更新，无需手动轮询

## 🎯 完整示例

```kotlin
class DefaultLayoutActivity : AppCompatActivity() {
    
    private lateinit var flightDataViewModel: FlightDataViewModel
    private lateinit var flightDataWidget: FlightDataWidget
    
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.uxsdk_activity_default_layout)
        
        // 1. 创建 ViewModel
        flightDataViewModel = ViewModelProvider(this)[FlightDataViewModel::class.java]
        
        // 2. 获取 Widget
        flightDataWidget = findViewById(R.id.flightdata_tip)
        
        // 3. 绑定 ViewModel（位置数据会自动更新）
        flightDataWidget.bindFlightDataViewModel(flightDataViewModel, this)
    }
}
```

## 🔍 调试

位置数据更新会记录到日志文件：
- 日志位置：`/storage/emulated/0/Download/AppLogs/`
- 查看日志：`adb shell cat /storage/emulated/0/Download/AppLogs/log_$(date +%Y%m%d).txt | grep FlightDataViewModel`


