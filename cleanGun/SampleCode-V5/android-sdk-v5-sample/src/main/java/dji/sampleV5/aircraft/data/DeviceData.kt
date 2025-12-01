package dji.sampleV5.aircraft.data

import com.google.gson.Gson
import com.google.gson.GsonBuilder
import dji.sdk.keyvalue.key.DJIKeyInfo
import dji.sdk.keyvalue.key.KeyTools
import dji.sdk.keyvalue.value.common.LocationCoordinate3D
import dji.sdk.keyvalue.value.common.Velocity3D
import dji.sdk.keyvalue.value.flightcontroller.FlightMode
import dji.sdk.keyvalue.value.flightcontroller.GPSSignalLevel
import dji.sdk.keyvalue.value.flightcontroller.WindDirection
import dji.sdk.keyvalue.value.flightcontroller.WindWarning
import dji.v5.manager.KeyManager

// 通用的获取数据的函数
inline fun <reified T> getValueForKey(keyInfo: DJIKeyInfo<T>): T? {
    return try {
        val key = KeyTools.createKey(keyInfo)
        KeyManager.getInstance().getValue(key)
    } catch (e: Exception) {
        null
    }
}

data class BatteryData(
    var batteryPercentage: Int? = null, // 电池百分比
    var voltage: Int? = null,           // 电压
    var temperature: Double? = null     // 温度
)

// 遥控器数据
data class RemoteControllerData(
    var isConnected: Boolean? = null,   // 遥控器是否连接
    var batteryPercentage: Int? = null, // 遥控器电池百分比
    var signalQuality: Int? = null     // 遥控器信号质量
)

// 飞机数据
data class AircraftData(
    var tid: String? = null,             // 消息ID
    var isConnected: Boolean? = null,   // 遥控器是否连接
    var isFlying: Boolean? = null,
    var flightTimeInSeconds: Int? = null, // 飞行时间
    var aircraftLocation3D: LocationCoordinate3D? = null,
    var aircraftAttitude: Double? = null,
    var aircraftVelocity: Velocity3D? = null,
    var takeoffLocationAltitude: Double? = null, // 起飞点海拔
    var satelliteCount: Int? = null, // 卫星颗数
    var GNSSSignalLevel: GPSSignalLevel? = null, // GNSS信号等级
    var GNSSSatelliteMode: Int? = null, // GNSS启用卫星类型
    var compassHeading: Double? = null, // 指南针角度
    var compassHasError: Boolean? = null, // 罗盘是否异常
    var ultrasonicHeight: Int? = null, // 超声波测距高度
    var windWarning: WindWarning? = null, // 风速等级
    var windSpeed: Int? = null, // 当前风速
    var windDirection: WindDirection? = null, // 当前风向
    var currentWaypointIndex: Int? = null, // 当前航点
    var currentTaskStatus: Int? = null, // 当前任务状态
    var airCraftToHomeLocationDistance: Int? = null, // 飞行器与Home点的水平距离
    var flightMode: FlightMode? = null, // 飞行模式
    var authorityOwner: Int? = null, // 控制权
    var waypointMission: Int? = null, // 航线任务状态
    var uavOnOffStatus: Int? = null, // 飞行器开关机状态
    var isRTKDongleConnect: Boolean? = null, // RTK硬件连接状态
    var RTKConnected: Boolean? = null, // RTK服务连接状态
    var RTKHealthy: Boolean? = null, // RTK健康状态
    var RTKSignal: Int? = null, // RTK解算状态
    var RTKHeading: Float? = null, // RTK模块航向
    var RTKlatitude: Double? = null, // RTK纬度
    var RTKlongitude: Double? = null, // RTK经度
    var RTKaltitude: Float? = null, // RTK椭球高度
    var gimbalPitch: Float? = null, // 云台俯仰角
    var gimbalRoll: Float? = null, // 云台横滚角
    var gimbalYaw: Float? = null, // 云台偏航角
    var remainingFlightTime: Int? = null, // 剩余飞行时间
    var aircraftTotalFlightDistance: Double? = null, // 总飞行里程
    var aircraftTotalFlightDuration: Double? = null, // 飞行总时间
    var currentStep: String? = null // 当前执行步骤
)

// 飞行器姿态数据
data class AircraftAttitude(
    var pitch: Double? = null,  // 俯仰角
    var roll: Double? = null,   // 横滚角
    var yaw: Double? = null     // 偏航角
)

data class DeviceData(
    val battery: BatteryData = BatteryData(),
    val remoteController: RemoteControllerData = RemoteControllerData(),
    val aircraft: AircraftData = AircraftData()
) {
    fun toJsonString(): String = Gson().toJson(this)
    fun toPrettyJsonString(): String = GsonBuilder().setPrettyPrinting().create().toJson(this)
}

data class AlertMessage(
    val key: String,         // 设备id
    val tid: String,         // 消息id
    val time: String,        // 告警时间戳，格式为：YYYY-MM-DD hh:mm:ss
    val content: String,     // 警告的信息
    val type: Int,           // 警告的类型 0：预警 1：一般 2：严重 3：危急
    val code: String         // 错误码
) {
    // 可选：若需要处理时间戳转换，可以在此定义方法
    fun getFormattedTime(): String {
        // 假设 time 是字符串，若需要可以将其转换为 LocalDateTime
        // return LocalDateTime.parse(time).format(DateTimeFormatter.ofPattern("yyyy-MM-dd HH:mm:ss"))
        return time // 默认返回原时间戳
    }
}