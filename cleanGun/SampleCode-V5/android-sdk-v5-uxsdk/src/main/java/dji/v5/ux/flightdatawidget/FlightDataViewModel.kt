package dji.v5.ux.flightdatawidget

import android.app.Application
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.LiveData
import androidx.lifecycle.MutableLiveData
import androidx.lifecycle.viewModelScope
import com.dji.util.FileLogger
import dji.sdk.keyvalue.key.FlightControllerKey
import dji.sdk.keyvalue.key.KeyTools
import dji.sdk.keyvalue.value.common.LocationCoordinate3D
import dji.sdk.keyvalue.value.common.Velocity3D
import dji.v5.common.utils.RxUtil
import dji.v5.manager.KeyManager
import dji.v5.ux.core.base.DJISDKModel
import io.reactivex.rxjava3.android.schedulers.AndroidSchedulers
import io.reactivex.rxjava3.disposables.CompositeDisposable
import io.reactivex.rxjava3.schedulers.Schedulers
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch

/**
 * 飞行数据 ViewModel
 *
 * 职责：
 * 1. 从 DJI SDK 获取飞行数据（位置、姿态、速度等）
 * 2. 通过 LiveData 暴露数据给 UI
 * 3. 提供可扩展的数据结构，方便后期添加其他数据
 */
class FlightDataViewModel(application: Application) : AndroidViewModel(application) {

    companion object {
        private const val TAG = "FlightDataViewModel"
        // 模拟数据延迟时间（毫秒），如果没有真实数据，延迟后使用模拟数据
        private const val MOCK_DATA_DELAY = 3000L
    }

    // ==================== 数据容器 ====================

    /**
     * 位置数据（经纬高）
     */
    data class LocationData(
        val longitude: Double = Double.NaN,
        val latitude: Double = Double.NaN,
        val altitude: Double = Double.NaN,
        val isValid: Boolean = false
    ) {
        fun getFormattedString(): String {
            return if (isValid) {
                "经度: %.6f\n纬度: %.6f\n高度: %.2fm".format(longitude, latitude, altitude)
            } else {
                "位置数据未就绪"
            }
        }
    }



    // data class VelocityData(val x: Double, val y: Double, val z: Double)
    // data class BatteryData(val percentage: Int, val voltage: Double)

    // ==================== LiveData 暴露给 UI ====================

    private val _location = MutableLiveData<LocationData>()
    val location: LiveData<LocationData> = _location

    // TODO: 后期可以添加其他数据

    private val _speed = MutableLiveData<Double>()
    val speed: LiveData<Double> = _speed

    // ==================== RxJava 订阅管理 ====================

    private val compositeDisposable = CompositeDisposable()

    // ==================== 模拟数据支持 ====================

    private var hasReceivedRealData = false
    private var mockDataJob: Job? = null
    private var mockUpdateJob: Job? = null
    private var isUsingMockData = false

    // 注意：viewModelScope 是 AndroidX Lifecycle 提供的扩展属性
    // 会在 ViewModel 的 onCleared() 时自动取消所有协程，无需手动管理

    init {
        FileLogger.i(TAG, "FlightDataViewModel 初始化")
        setupLocationListener()
        // 启动模拟数据检查
        startMockDataCheck()
        setVelocity3DListener()
    }

    // ==================== 位置数据监听 ====================

    /**
     * 设置位置数据监听器
     */
    private fun setupLocationListener() {
        try {
            val locationKey = KeyTools.createKey(FlightControllerKey.KeyAircraftLocation3D)

            val disposable = RxUtil.addListener(locationKey, this)
                .subscribeOn(Schedulers.io())
                .observeOn(AndroidSchedulers.mainThread())
                .subscribe(
                    { location: LocationCoordinate3D? ->
                        if (location != null) {
                            updateLocation(location)
                        }
                    },
                    { error ->
                        FileLogger.e(TAG, "位置数据监听失败: ${error.message}", error)
                    }
                )

            compositeDisposable.add(disposable)
        } catch (e: Exception) {
        }
    }

    /**
     * 设置位置数据监听器
     */
    private fun setVelocity3DListener() {
        try {
            val aircraftVelocityKey = KeyTools.createKey(FlightControllerKey.KeyAircraftVelocity)

            val disposable = RxUtil.addListener(aircraftVelocityKey, this)
                .subscribeOn(Schedulers.io())
                .observeOn(AndroidSchedulers.mainThread())
                .subscribe(
                    { velocity3D: Velocity3D? ->
                        if (velocity3D != null) {

                            val horizontalSpeed = kotlin.math.sqrt(
                                velocity3D.x * velocity3D.x +
                                        velocity3D.y * velocity3D.y+
                                            velocity3D.z * velocity3D.z
                            )
                            updateSpeed(horizontalSpeed)
                        }
                    },
                    { error ->
                        FileLogger.e(TAG, "速度数据监听失败: ${error.message}", error)
                    }
                )

            compositeDisposable.add(disposable)
        } catch (e: Exception) {
            FileLogger.e(TAG, "设置速度监听器失败: ${e.message}", e)
        }
    }



    /**
     * 更新位置数据
     */
    private fun updateLocation(location: LocationCoordinate3D) {
        val isValid = !location.latitude.isNaN() &&
                     !location.longitude.isNaN() &&
                     !location.altitude.isNaN()

        // 如果收到有效真实数据，标记并取消模拟数据
        if (isValid) {
            stopMockData()
            hasReceivedRealData = true
        }

        val locationData = LocationData(
            longitude = location.longitude,
            latitude = location.latitude,
            altitude = location.altitude,
            isValid = isValid
        )

        _location.postValue(locationData)
    }



    /**
     * 更新速度
     */
    private fun updateSpeed(speed: Double) {

        _speed.postValue(speed)
    }

    /**
     * 手动获取当前位置（同步方式）
     */
    fun getCurrentLocation(): LocationData? {
        return try {
            val locationKey = KeyTools.createKey(FlightControllerKey.KeyAircraftLocation3D)
            val location = KeyManager.getInstance().getValue(locationKey)
            
            if (location != null) {
                val isValid = !location.latitude.isNaN() && 
                             !location.longitude.isNaN() && 
                             !location.altitude.isNaN()
                
                // 如果获取到有效真实数据，标记并取消模拟数据
                if (isValid) {
                    stopMockData()
                    hasReceivedRealData = true
                }
                
                val locationData = LocationData(
                    longitude = location.longitude,
                    latitude = location.latitude,
                    altitude = location.altitude,
                    isValid = isValid
                )
                _location.postValue(locationData)
                locationData
            } else {
                null
            }
        } catch (e: Exception) {
            null
        }
    }
    
    // ==================== 模拟数据 ====================
    
    /**
     * 启动模拟数据检查
     * 如果延迟时间内没有收到真实数据，则使用模拟数据
     */
    private fun startMockDataCheck() {
        mockDataJob = viewModelScope.launch {
            delay(MOCK_DATA_DELAY)  // 延迟 3 秒
            if (!hasReceivedRealData && !isUsingMockData) {
                useMockData()
            }
        }
    }
    
    /**
     * 使用模拟数据
     */
    private fun useMockData() {
        isUsingMockData = true
        // 模拟位置数据（北京天安门附近）
        val mockLocationData = LocationData(
            longitude = 116.397477,  // 北京天安门经度
            latitude = 39.909652,    // 北京天安门纬度
            altitude = 50.0,         // 高度 50 米
            isValid = true
        )
        
        _location.postValue(mockLocationData)

        // 启动模拟数据更新（每秒更新一次，让数据看起来更动态）
        startMockDataUpdates(mockLocationData)
    }
    
    /**
     * 启动模拟数据更新（让模拟数据看起来更动态）
     * 使用协程实现循环更新，代码更简洁且自动管理生命周期
     */
    private fun startMockDataUpdates(initialData: LocationData) {
        var currentLongitude = initialData.longitude
        var currentLatitude = initialData.latitude
        var currentAltitude = initialData.altitude
        
        mockUpdateJob = viewModelScope.launch {
            // 使用 while 循环配合 delay，代码更清晰
            while (isUsingMockData && !hasReceivedRealData) {
                delay(1000)  // 延迟 1 秒
                
                // 模拟微小的位置变化（模拟飞行中的位置变化）
                currentLongitude += (Math.random() - 0.5) * 0.0001  // 约 ±11 米
                currentLatitude += (Math.random() - 0.5) * 0.0001   // 约 ±11 米
                currentAltitude += (Math.random() - 0.5) * 0.5      // 高度变化 ±0.5 米
                
                // 确保高度不为负
                if (currentAltitude < 0) {
                    currentAltitude = 10.0
                }
                
                val mockData = LocationData(
                    longitude = currentLongitude,
                    latitude = currentLatitude,
                    altitude = currentAltitude,
                    isValid = true
                )
                
                _location.postValue(mockData)
            }
        }
    }
    
    /**
     * 停止模拟数据
     * 取消所有协程任务
     */
    private fun stopMockData() {
        isUsingMockData = false
        mockDataJob?.cancel()
        mockUpdateJob?.cancel()
        mockDataJob = null
        mockUpdateJob = null
    }
    
    /**
     * 手动启用模拟数据（用于测试）
     */
    fun enableMockData() {
        stopMockData()
        hasReceivedRealData = false
        useMockData()
    }

    // ==================== 清理资源 ====================
    
    override fun onCleared() {
        super.onCleared()
        compositeDisposable.clear()
        // 移除监听器
        DJISDKModel.getInstance().removeListener(this)
        // 停止模拟数据（取消协程任务）
        stopMockData()
        // viewModelScope 会在 onCleared() 时自动取消所有协程，无需手动 cancel
        // 但为了保险起见，我们已经在 stopMockData() 中显式取消了
        FileLogger.d(TAG, "FlightDataViewModel 资源已清理")
    }
}
