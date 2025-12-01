package dji.sampleV5.aircraft.mqtthandle

import dji.sampleV5.aircraft.data.DeviceData
import dji.sampleV5.aircraft.data.getValueForKey
import dji.sdk.keyvalue.key.BatteryKey
import dji.sdk.keyvalue.key.DJIKeyInfo
import dji.sdk.keyvalue.key.FlightControllerKey
import dji.sdk.keyvalue.key.RemoteControllerKey
import dji.v5.common.error.IDJIError
import dji.v5.manager.aircraft.waypoint3.WaylineExecutingInfoListener
import dji.v5.manager.aircraft.waypoint3.WaypointMissionManager
import dji.v5.manager.aircraft.waypoint3.model.WaylineExecutingInfo
import kotlin.reflect.KMutableProperty0

/**
 * 设备数据管理器
 * 
 * 职责：
 * 1. 通过 Key 轮询更新设备数据（电池、遥控器、飞机状态）
 * 2. 通过监听器实时更新航点任务数据
 * 3. 提供统一的数据访问接口
 */
class DeviceDataManager {

    companion object {
        private const val TAG = "DeviceDataManager"
    }

    // ==================== 数据容器 ====================
    private var deviceData = DeviceData()

    // ==================== 监听器引用（用于取消注册）====================
    private var waylineExecutingInfoListener: WaylineExecutingInfoListener? = null

    // ==================== 初始化：注册监听器 ====================
    init {
        setupWaylineListener()
    }

    // ==================== 监听器设置（实时更新）====================
    
    /**
     * 设置航线执行监听器
     * 
     * 注意：航点索引等数据无法通过Key获取，只能通过监听器实时更新
     */
    private fun setupWaylineListener() {
        // 创建监听器实例并保存引用
        waylineExecutingInfoListener = object : WaylineExecutingInfoListener {
            override fun onWaylineExecutingInfoUpdate(info: WaylineExecutingInfo) {
                // 实时更新当前航点索引
                deviceData.aircraft.currentWaypointIndex = info.currentWaypointIndex
            }

            override fun onWaylineExecutingInterruptReasonUpdate(error: IDJIError?) {
                // 可以在这里处理任务中断原因
            }
        }
        
        // 注册监听器
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
     * 更新所有通过Key可获取的数据
     * 
     * 注意：此方法仅更新Key字段，航点索引等监听器字段由监听器自动更新
     */
    fun updateAllData() {
        updateBatteryData()
        updateRemoteControllerData()
        updateAircraftData()
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
        // 连接状态
        ::isConnected.updateFrom(FlightControllerKey.KeyConnection)
        ::isFlying.updateFrom(FlightControllerKey.KeyIsFlying)
        ::flightTimeInSeconds.updateFrom(FlightControllerKey.KeyFlightTimeInSeconds)
        
        // 位置和姿态
        ::aircraftLocation3D.updateFrom(FlightControllerKey.KeyAircraftLocation3D)
        ::aircraftAttitude.updateFrom(FlightControllerKey.KeyAltitude)
        ::aircraftVelocity.updateFrom(FlightControllerKey.KeyAircraftVelocity)
        ::takeoffLocationAltitude.updateFrom(FlightControllerKey.KeyTakeoffLocationAltitude)
        
        // GPS信息
        ::satelliteCount.updateFrom(FlightControllerKey.KeyGPSSatelliteCount)
        ::GNSSSignalLevel.updateFrom(FlightControllerKey.KeyGPSSignalLevel)
        
        // 传感器数据
        ::compassHeading.updateFrom(FlightControllerKey.KeyCompassHeading)
        ::compassHasError.updateFrom(FlightControllerKey.KeyCompassHasError)
        ::ultrasonicHeight.updateFrom(FlightControllerKey.KeyUltrasonicHeight)
        
        // 风速信息
        ::windWarning.updateFrom(FlightControllerKey.KeyWindWarning)
        ::windSpeed.updateFrom(FlightControllerKey.KeyWindSpeed)
        ::windDirection.updateFrom(FlightControllerKey.KeyWindDirection)
        
        // 飞行模式
        ::flightMode.updateFrom(FlightControllerKey.KeyFlightMode)
        
        // 注意：以下字段由监听器实时更新，无需在此处理：
        // - currentWaypointIndex (由 setupWaylineListener 更新)
    }

    // ==================== 对外接口 ====================
    
    /**
     * 获取设备数据
     * 
     * @return 完整的设备数据，包含Key轮询的数据和监听器实时更新的数据
     */
    fun getData(): DeviceData = deviceData

    /**
     * 清空所有数据（直接重新创建对象）
     */
    fun clearAllData() {
        deviceData = DeviceData()
    }
    
    /**
     * 销毁资源，移除所有监听器
     * 
     * 注意：在Activity/Fragment的onDestroy()或不再使用此管理器时调用
     * 避免内存泄漏
     */
    fun destroy() {
        removeWaylineListener()
        clearAllData()
    }
}
