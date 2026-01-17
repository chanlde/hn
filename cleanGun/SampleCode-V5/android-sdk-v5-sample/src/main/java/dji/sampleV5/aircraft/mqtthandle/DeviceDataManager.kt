package dji.sampleV5.aircraft.mqtthandle

import android.util.Log
import dji.sampleV5.aircraft.data.AircraftAttitudeData
import com.dji.util.FileLogger
import dji.sampleV5.aircraft.data.AircraftData
import dji.sampleV5.aircraft.data.AircraftLocation3DData
import dji.sampleV5.aircraft.data.AircraftVelocityData
import dji.sampleV5.aircraft.data.DeviceData
import dji.sampleV5.aircraft.data.FlightReportData
import dji.sampleV5.aircraft.data.getValueForKey
import dji.sdk.keyvalue.key.BatteryKey
import dji.sdk.keyvalue.key.DJIKeyInfo
import dji.sdk.keyvalue.key.FlightControllerKey
import dji.sdk.keyvalue.key.RemoteControllerKey
import dji.v5.common.error.IDJIError
import dji.v5.manager.KeyManager
import dji.v5.manager.aircraft.waypoint3.WaylineExecutingInfoListener
import dji.v5.manager.aircraft.waypoint3.WaypointMissionManager
import dji.v5.manager.aircraft.waypoint3.model.WaylineExecutingInfo
import kotlin.math.sqrt
import kotlin.reflect.KMutableProperty0

/**
 * 设备数据管理器
 * 
 * 职责：
 * 1. 通过 Key 轮询更新设备数据
 * 2. 通过监听器实时更新航点任务数据
 * 3. 提供统一的数据访问接口
 * 4. 提供符合上报格式的数据
 */
class DeviceDataManager {

    companion object {
        private const val TAG = "DeviceDataManager"
    }

    // ==================== 数据容器 ====================
    private var deviceData = DeviceData()
    private var flightReportData = FlightReportData()

    // ==================== 监听器引用（用于取消注册）====================
    private var waylineExecutingInfoListener: WaylineExecutingInfoListener? = null
    private val lock = Any()  // ← 对象锁

    // ==================== 初始化：注册监听器 ====================
    init {
        setupWaylineListener()
    }

    // ==================== 监听器设置（实时更新）====================
    
    /**
     * 设置航线执行监听器
     */
// 在 DeviceDataManager 类内部添加
    private inner class WaylineExecutingInfoListenerImpl : WaylineExecutingInfoListener {
        override fun onWaylineExecutingInfoUpdate(info: WaylineExecutingInfo) {
            FileLogger.thread(TAG, "航线监听器回调 - onWaylineExecutingInfoUpdate")
            synchronized(lock) {
                val oldIndex = deviceData.aircraft.currentWaypointIndex
                deviceData.aircraft.currentWaypointIndex = info.currentWaypointIndex
                flightReportData.currentWaypointIndex = info.currentWaypointIndex
                FileLogger.dataUpdate(
                    TAG,
                    "航线监听器更新航点索引",
                    "old=$oldIndex, new=${info.currentWaypointIndex}"
                )
            }
        }

        override fun onWaylineExecutingInterruptReasonUpdate(error: IDJIError) {
            FileLogger.thread(TAG, "航线监听器回调 - onWaylineExecutingInterruptReasonUpdate")
            try {
                val description = error.description() ?: "未知错误"
                val errorCode = error.errorCode()
                FileLogger.w(TAG, "航线执行中断: $description, 错误码: $errorCode")
            } catch (e: Exception) {
                FileLogger.e(TAG, "处理航线中断错误时异常: ${e.message}", e)
            }
        }
    }

    // 修改 setupWaylineListener 方法
    private fun setupWaylineListener() {
        waylineExecutingInfoListener = WaylineExecutingInfoListenerImpl()

        waylineExecutingInfoListener?.let {
            WaypointMissionManager.getInstance().addWaylineExecutingInfoListener(it)
        }
    }

    /**
     * 移除航线执行监听器
     */
    private fun removeWaylineListener() {
        waylineExecutingInfoListener?.let {
            WaypointMissionManager.getInstance().removeWaylineExecutingInfoListener(it)
            waylineExecutingInfoListener = null
        }
    }

    // ==================== Key轮询更新（按需调用）====================
    
    /**
     * 更新所有数据并返回上报格式的数据
     */
    fun updateAllData() {
        synchronized(lock) {
            val startTime = System.currentTimeMillis()
            try {
                updateBatteryData()
                updateRemoteControllerData()
                updateAircraftData()
                updateFlightReportData()
                val duration = System.currentTimeMillis() - startTime

            } catch (e: Exception) {
                FileLogger.e(TAG, "数据更新失败", e)
                throw e
            }
        }
    }

    /**
     * 高级扩展函数：自动从Key更新属性
     */
    private inline fun <reified T> KMutableProperty0<T?>.updateFrom(key: DJIKeyInfo<T>) {
        this.set(getValueForKey(key))
    }

    // ==================== 电池数据更新 ====================
    private fun updateBatteryData() = deviceData.battery.run {
        ::batteryPercentage.updateFrom(BatteryKey.KeyChargeRemainingInPercent)
        ::voltage.updateFrom(BatteryKey.KeyVoltage)
    }

    // ==================== 遥控器数据更新 ====================
    private fun updateRemoteControllerData() = deviceData.remoteController.run {
        ::isConnected.updateFrom(RemoteControllerKey.KeyConnection)
    }

    // ==================== 飞机数据更新 ====================
    private fun updateAircraftData() = deviceData.aircraft.run {
        ::isConnected.updateFrom(FlightControllerKey.KeyConnection)
        ::isFlying.updateFrom(FlightControllerKey.KeyIsFlying)
        ::flightTimeInSeconds.updateFrom(FlightControllerKey.KeyFlightTimeInSeconds)
        ::aircraftLocation3D.updateFrom(FlightControllerKey.KeyAircraftLocation3D)
        ::aircraftAttitude.updateFrom(FlightControllerKey.KeyAircraftAttitude)
        ::aircraftVelocity.updateFrom(FlightControllerKey.KeyAircraftVelocity)
        ::takeoffLocationAltitude.updateFrom(FlightControllerKey.KeyTakeoffLocationAltitude)
        ::satelliteCount.updateFrom(FlightControllerKey.KeyGPSSatelliteCount)
        ::GNSSSignalLevel.updateFrom(FlightControllerKey.KeyGPSSignalLevel)
        ::compassHeading.updateFrom(FlightControllerKey.KeyCompassHeading)
        ::compassHasError.updateFrom(FlightControllerKey.KeyCompassHasError)
        ::ultrasonicHeight.updateFrom(FlightControllerKey.KeyUltrasonicHeight)
        ::windWarning.updateFrom(FlightControllerKey.KeyWindWarning)
        ::windSpeed.updateFrom(FlightControllerKey.KeyWindSpeed)
        ::windDirection.updateFrom(FlightControllerKey.KeyWindDirection)
        ::flightMode.updateFrom(FlightControllerKey.KeyFlightMode)
    }

    // ==================== 上报数据格式更新（严格按照示例顺序）====================
    private fun updateFlightReportData() {
        val aircraft = deviceData.aircraft
        
        flightReportData.apply {
            // 1. tid - 自动在 toJsonString() 时生成
            
            // 2. connection
            connection = aircraft.isConnected
            
            // 3. isFlying
            isFlying = aircraft.isFlying
            
            // 4. flightTimeInSeconds
            flightTimeInSeconds = aircraft.flightTimeInSeconds
            
            // 5. aircraftLocation3D
            aircraft.aircraftLocation3D?.let { loc ->
                aircraftLocation3D = AircraftLocation3DData(
                    longitude = loc.longitude,
                    latitude = loc.latitude,
                    height = loc.altitude
                )
            }
            
            // 6. aircraftAttitude
            aircraft.aircraftAttitude?.let { att ->
                aircraftAttitude = AircraftAttitudeData(
                    pitch = att.pitch,
                    roll = att.roll,
                    yaw = att.yaw
                )
            }
            
            // 7. aircraftVelocity
            aircraft.aircraftVelocity?.let { vel ->
                val horizonVel = sqrt(vel.x * vel.x + vel.y * vel.y)
                aircraftVelocity = AircraftVelocityData(
                    horizonVelocity = horizonVel,
                    verticalVelocity = vel.z
                )
            }
            
            // 8. takeoffLocationAltitude
            takeoffLocationAltitude = aircraft.takeoffLocationAltitude
            
            // 9. satelliteCount
            satelliteCount = aircraft.satelliteCount
            
            // 10. GNSSSignalLevel
            GNSSSignalLevel = aircraft.GNSSSignalLevel?.value()
            
            // 11. compassHeading
            compassHeading = aircraft.compassHeading
            
            // 12. compassHasError
            compassHasError = aircraft.compassHasError
            
            // 13. ultrasonicHeight
            ultrasonicHeight = aircraft.ultrasonicHeight
            
            // 14. windWarning
            windWarning = aircraft.windWarning?.value()
            
            // 15. windSpeed
            windSpeed = aircraft.windSpeed
            
            // 16. windDirection
            windDirection = aircraft.windDirection?.value()
            
            // 17. currentWaypointIndex（由监听器实时更新）
            currentWaypointIndex = aircraft.currentWaypointIndex
            
            // 18. flightMode
            flightMode = aircraft.flightMode?.value()
        }
    }

    // ==================== 对外接口 ====================
    
    /**
     * 获取完整设备数据（内部使用）
     */
    fun getData(): DeviceData = deviceData

    /**
     * 获取上报格式的飞行数据
     */
    fun getFlightReportData(): FlightReportData {
        synchronized(lock) {
            return flightReportData
        }
    }
    /**
     * 清空所有数据
     */
    fun clearAllData() {
        deviceData = DeviceData()
        flightReportData = FlightReportData()
    }
    
    /**
     * 销毁资源，移除所有监听器
     */
    fun destroy() {
        removeWaylineListener()
        clearAllData()
        Log.d(TAG, "✅ DeviceDataManager 资源已清理")
    }
}
