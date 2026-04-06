package dji.sampleV5.aircraft.mqtthandle

import dji.sampleV5.aircraft.data.AircraftAttitudeData
import com.dji.util.FileLogger
import dji.sampleV5.aircraft.data.AircraftData
import dji.sampleV5.aircraft.data.AircraftLocation3DData
import dji.sampleV5.aircraft.data.AircraftVelocityData
import dji.sampleV5.aircraft.data.DeviceData
import dji.sampleV5.aircraft.data.FlightReportData
import dji.sampleV5.aircraft.data.getValueForKey
import dji.sdk.keyvalue.key.BatteryKey
import dji.sdk.keyvalue.key.CameraKey
import dji.sdk.keyvalue.key.DJIKeyInfo
import dji.sdk.keyvalue.key.FlightControllerKey
import dji.sdk.keyvalue.key.RemoteControllerKey
import dji.sdk.keyvalue.value.camera.CameraMode
import dji.sdk.keyvalue.value.common.ComponentIndexType
import dji.v5.common.error.IDJIError
import android.location.Location
import dji.sampleV5.aircraft.DJIApplication
import dji.sampleV5.aircraft.manager.LocationService
import dji.v5.manager.KeyManager
import dji.v5.manager.aircraft.waypoint3.WaylineExecutingInfoListener
import dji.v5.manager.aircraft.waypoint3.WaypointMissionManager
import dji.v5.manager.aircraft.waypoint3.WaypointMissionExecuteStateListener
import dji.v5.manager.aircraft.waypoint3.model.WaylineExecutingInfo
import dji.v5.manager.aircraft.waypoint3.model.WaypointMissionExecuteState
import dji.v5.common.callback.CommonCallbacks
import dji.sdk.keyvalue.key.KeyTools
import dji.sdk.keyvalue.value.flightcontroller.FCFlightMode
import dji.sdk.keyvalue.value.flightcontroller.FlightMode
import dji.sdk.keyvalue.value.flightcontroller.LowBatteryRTHInfo
import dji.v5.manager.datacenter.media.MediaFileListState
import dji.sdk.keyvalue.value.flightcontroller.LowBatteryRTHState
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
    private var waypointMissionExecuteStateListener: WaypointMissionExecuteStateListener? = null
    private var lowBatteryRTHListener: CommonCallbacks.KeyListener<LowBatteryRTHInfo>? = null
    private var flightModeListener: CommonCallbacks.KeyListener<FlightMode>? = null
    private var isFlyingListener: CommonCallbacks.KeyListener<Boolean>? = null
    private var cameraShootingModeListener: CommonCallbacks.KeyListener<CameraMode>? = null
    private val lock = Any()  // ← 对象锁
    val locationService = DJIApplication.getInstance()?.getLocationService()
    
    // ==================== 任务状态相关字段 ====================
    @Volatile
    private var currentWaypointMissionExecuteState: WaypointMissionExecuteState? = null
    @Volatile
    private var currentTaskStatus: Int = -1  // 当前任务状态：-1=未知, 0=待命, 2=执行航线, 3=返航, 6=降落完成, 8=下载媒体, 9=上传媒体, 10=任务中断
    
    @Volatile
    private var isMissionInterrupted: Boolean = false  // 任务是否中断
    
    @Volatile
    private var wasFlying: Boolean = false  // 上次飞行状态（用于判断降落完成）
    
    @Volatile
    private var lastLandingTime: Long = 0  // 降落完成时间（用于判断降落完成状态持续时间）
    
    @Volatile
    private var isLowBatteryRTH: Boolean = false  // 是否正在低电量返航
    
    // CameraService 引用（可选，用于查询上传状态）
    var cameraService: CameraService? = null
        set(value) {
            field = value
            FileLogger.logStateChange(TAG, "cameraServiceRef", if (value != null) "set" else "cleared")
        }
    
    // ==================== 初始化：注册监听器 ====================
    init {
        setupWaylineListener()
        setupWaypointMissionExecuteStateListener()
        setupStatusListeners()
        setupCameraShootingModeListener()
    }

    // ==================== 监听器设置（实时更新）====================
    
    /**
     * 设置航线执行监听器
     */
// 在 DeviceDataManager 类内部添加
    private inner class WaylineExecutingInfoListenerImpl : WaylineExecutingInfoListener {
        override fun onWaylineExecutingInfoUpdate(info: WaylineExecutingInfo) {
            synchronized(lock) {
                deviceData.aircraft.currentWaypointIndex = info.currentWaypointIndex
                flightReportData.currentWaypointIndex = info.currentWaypointIndex
                FileLogger.logStateChange(TAG, "currentWaypointIndex", info.currentWaypointIndex)
            }
        }

        override fun onWaylineExecutingInterruptReasonUpdate(error: IDJIError) {
            synchronized(lock) {
                isMissionInterrupted = true
                currentTaskStatus = 10
            }
            try {
                val description = error.description() ?: "未知错误"
                val errorCode = error.errorCode()
                FileLogger.w(TAG, "航线执行中断: $description code=$errorCode -> taskStatus=10")
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
    
    /**
     * 设置航线任务执行状态监听器
     */
    private fun setupWaypointMissionExecuteStateListener() {
        waypointMissionExecuteStateListener = object : WaypointMissionExecuteStateListener {
            override fun onMissionStateUpdate(missionState: WaypointMissionExecuteState) {
                synchronized(lock) {
                    val previousState = currentWaypointMissionExecuteState
                    currentWaypointMissionExecuteState = missionState
                    FileLogger.logStateChange(TAG, "waypointMissionExecuteState", missionState.name)

                    val wasActive = previousState != null && previousState != WaypointMissionExecuteState.READY
                    if (wasActive && missionState == WaypointMissionExecuteState.READY) {
                        FileLogger.i(TAG, "检测到任务结束 (${previousState?.name} -> READY)，重置任务状态")
                        resetMissionState()
                    }
                }
            }
        }
        
        waypointMissionExecuteStateListener?.let {
            WaypointMissionManager.getInstance().addWaypointMissionExecuteStateListener(it)
        }
    }
    
    /**
     * 移除航线任务执行状态监听器
     */
    private fun removeWaypointMissionExecuteStateListener() {
        waypointMissionExecuteStateListener?.let {
            WaypointMissionManager.getInstance().removeWaypointMissionExecuteStateListener(it)
            waypointMissionExecuteStateListener = null
        }
    }
    
    /**
     * 设置状态监听器（低电量返航、飞行模式、飞行状态）
     */
    private fun setupStatusListeners() {
        // 监听低电量返航信息
        val lowBatteryRTHKey = KeyTools.createKey(FlightControllerKey.KeyLowBatteryRTHInfo)
        lowBatteryRTHListener = object : CommonCallbacks.KeyListener<LowBatteryRTHInfo> {
            override fun onValueChange(oldValue: LowBatteryRTHInfo?, newValue: LowBatteryRTHInfo?) {
                synchronized(lock) {
                    val oldState = oldValue?.lowBatteryRTHStatus
                    val newState = newValue?.lowBatteryRTHStatus
                    
                    isLowBatteryRTH = when (newState) {
                        LowBatteryRTHState.COUNTING_DOWN,
                        LowBatteryRTHState.EXECUTED -> {
                            isMissionInterrupted = true
                            currentTaskStatus = 10  // 任务中断
                            true
                        }
                        else -> false
                    }
                    
                    if (newState != oldState) {
                        FileLogger.logStateChange(TAG, "lowBatteryRTH", "$oldState->$newState lowBatRTH=$isLowBatteryRTH")
                    }
                }
            }
        }
        KeyManager.getInstance().listen(lowBatteryRTHKey, this, lowBatteryRTHListener!!)
        
        // 监听飞行模式
        val flightModeKey = KeyTools.createKey(FlightControllerKey.KeyFlightMode)
        flightModeListener = object : CommonCallbacks.KeyListener<FlightMode> {
            override fun onValueChange(oldValue: FlightMode?, newValue: FlightMode?) {
                synchronized(lock) {
                    // 如果飞行模式是返航，且没有其他更高优先级的状态，设置为返航状态
                    if (newValue == FlightMode.GO_HOME && !isMissionInterrupted && !isLowBatteryRTH) {
                        if (deviceData.aircraft.isFlying == true) {
                            currentTaskStatus = 3
                            FileLogger.i(TAG, "飞行模式返航 $oldValue->$newValue -> taskStatus=3")
                        }
                    }
                }
            }
        }
        KeyManager.getInstance().listen(flightModeKey, this, flightModeListener!!)
        
        // 监听飞行状态（用于判断降落完成）
        val isFlyingKey = KeyTools.createKey(FlightControllerKey.KeyIsFlying)
        isFlyingListener = object : CommonCallbacks.KeyListener<Boolean> {
            override fun onValueChange(oldValue: Boolean?, newValue: Boolean?) {
                synchronized(lock) {
                    val oldFlying = oldValue ?: false
                    val newFlying = newValue ?: false
                    
                    // 检测降落完成：从飞行变为不飞行
                    if (oldFlying && !newFlying) {
                        lastLandingTime = System.currentTimeMillis()
                        FileLogger.i(TAG, "检测到降落 isFlying true->false t=$lastLandingTime")
                    }
                    
                    wasFlying = oldFlying
                }
            }
        }
        KeyManager.getInstance().listen(isFlyingKey, this, isFlyingListener!!)
        
        FileLogger.i(TAG, "状态监听器已注册")
    }

    /**
     * 监听主云台相机拍摄模式（与 UX Altitude 等一致使用 LEFT_OR_MAIN）
     */
    private fun setupCameraShootingModeListener() {
        val key = KeyTools.createKey(CameraKey.KeyCameraMode, ComponentIndexType.LEFT_OR_MAIN)
        cameraShootingModeListener = object : CommonCallbacks.KeyListener<CameraMode> {
            override fun onValueChange(oldValue: CameraMode?, newValue: CameraMode?) {
                synchronized(lock) {
                    deviceData.aircraft.cameraShootingModeName = newValue?.name
                    FileLogger.logStateChange(TAG, "cameraShootingMode", newValue?.name)
                }
            }
        }
        cameraShootingModeListener?.let {
            KeyManager.getInstance().listen(key, this, it)
        }
    }

    private fun removeCameraShootingModeListener() {
        val key = KeyTools.createKey(CameraKey.KeyCameraMode, ComponentIndexType.LEFT_OR_MAIN)
        cameraShootingModeListener?.let {
            KeyManager.getInstance().cancelListen(key, it)
            cameraShootingModeListener = null
        }
    }
    
    /**
     * 移除状态监听器
     */
    private fun removeStatusListeners() {
        val lowBatteryRTHKey = KeyTools.createKey(FlightControllerKey.KeyLowBatteryRTHInfo)
        lowBatteryRTHListener?.let {
            KeyManager.getInstance().cancelListen(lowBatteryRTHKey, it)
            lowBatteryRTHListener = null
        }
        
        val flightModeKey = KeyTools.createKey(FlightControllerKey.KeyFlightMode)
        flightModeListener?.let {
            KeyManager.getInstance().cancelListen(flightModeKey, it)
            flightModeListener = null
        }
        
        val isFlyingKey = KeyTools.createKey(FlightControllerKey.KeyIsFlying)
        isFlyingListener?.let {
            KeyManager.getInstance().cancelListen(isFlyingKey, it)
            isFlyingListener = null
        }
        
        FileLogger.i(TAG, "状态监听器已移除")
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
        ::relativeAltitudeFromTakeoff.updateFrom(FlightControllerKey.KeyAltitude)
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

    // ==================== 任务状态判断 ====================
    /**
     * 更新当前任务状态
     * 状态优先级：中断(10) > 执行航线(2) > 返航(3) > 降落完成(6) > 下载媒体(8) > 上传媒体(9) > 待命(0) > 未知(-1)
     */
    private fun updateCurrentTaskStatus() {
        synchronized(lock) {
            val aircraft = deviceData.aircraft
            val now = System.currentTimeMillis()
            val LANDING_FINISH_DURATION = 3000L  // 降落完成状态持续时间：3秒
            
            // 1. 状态 10 (任务中断) - 优先级最高
            if (isMissionInterrupted || isLowBatteryRTH) {
                currentTaskStatus = 10
                return
            }
            
            // 2. 状态 2 (执行航线) - 正在执行航线
            if (aircraft.currentWaypointIndex != null && aircraft.isFlying == true) {
                currentTaskStatus = 2
                return
            }
            
            // 3. 状态 3 (返航) - 飞行模式为返航
            if (aircraft.flightMode == FlightMode.GO_HOME && aircraft.isFlying == true) {
                currentTaskStatus = 3
                return
            }
            
            // 4. 状态 6 (降落完成) - 刚刚降落（3秒内）
            if (wasFlying && aircraft.isFlying == false && lastLandingTime > 0) {
                if (now - lastLandingTime < LANDING_FINISH_DURATION) {
                    currentTaskStatus = 6
                    return
                }
            }
            
            // 5. 状态 8 (下载媒体) - 媒体文件列表状态为更新中
            val mediaState = cameraService?.getMediaFileListState()
            if (mediaState == MediaFileListState.UPDATING) {
                currentTaskStatus = 8
                return
            }
            
            // 6. 状态 9 (上传媒体) - 正在上传文件
            if (cameraService?.getIsUploading() == true) {
                currentTaskStatus = 9
                return
            }
            
            // 7. 状态 0 (待命) - 在地面且没有执行任务
            // 到达此处时已排除所有活跃状态，不在飞行即为待命
            if (aircraft.isFlying == false) {
                currentTaskStatus = 0
                return
            }
            
            // 8. 状态 -1 (未知) - 其他情况
            currentTaskStatus = -1
        }
    }

    // ==================== 上报数据格式更新（严格按照示例顺序）====================
    private fun updateFlightReportData() {
        val aircraft = deviceData.aircraft
        
        // 先更新任务状态
        updateCurrentTaskStatus()
        
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
            relativeAltitudeFromTakeoff = aircraft.relativeAltitudeFromTakeoff
            val relAlt = aircraft.relativeAltitudeFromTakeoff
            val takeoffAlt = aircraft.takeoffLocationAltitude
            altitudeAMSL = if (relAlt != null && takeoffAlt != null) relAlt + takeoffAlt else null
            
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

            try {
                val lastLocation = locationService?.getLastLocation()

                if (lastLocation != null) {
                    handsetLatitude = lastLocation.latitude
                    handsetLongitude = lastLocation.longitude
                    FileLogger.throttledD(
                        TAG,
                        "handsetGps",
                        "遥控器GPS lat=$handsetLatitude lon=$handsetLongitude",
                        10_000L
                    )
                } else {
                    handsetLatitude = null
                    handsetLongitude = null
                }
            } catch (e: Exception) {
                FileLogger.throttledD(TAG, "handsetGpsFail", "遥控器GPS失败: ${e.message}", 15_000L)
                handsetLatitude = null
                handsetLongitude = null
            }
            
            // 21. currentTaskStatus - 当前任务状态
            currentTaskStatus = this@DeviceDataManager.currentTaskStatus
            
            // 22. waypointMissionExecuteState - 航线任务执行状态
            waypointMissionExecuteState = currentWaypointMissionExecuteState?.name ?: null

            // 23. cameraMode - 相机拍摄模式
            cameraMode = aircraft.cameraShootingModeName
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
     * 重置任务相关状态（任务结束后调用，避免残留状态影响下次任务）
     */
    fun resetMissionState() {
        synchronized(lock) {
            deviceData.aircraft.currentWaypointIndex = null
            flightReportData.currentWaypointIndex = null
            currentWaypointMissionExecuteState = null
            currentTaskStatus = 0
            isMissionInterrupted = false
            wasFlying = false
            lastLandingTime = 0
            isLowBatteryRTH = false
            FileLogger.i(TAG, "任务状态已重置: currentWaypointIndex=null, currentTaskStatus=0, isMissionInterrupted=false")
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
        removeWaypointMissionExecuteStateListener()
        removeStatusListeners()
        removeCameraShootingModeListener()
        clearAllData()
        FileLogger.i(TAG, "DeviceDataManager 已清理")
    }
}
