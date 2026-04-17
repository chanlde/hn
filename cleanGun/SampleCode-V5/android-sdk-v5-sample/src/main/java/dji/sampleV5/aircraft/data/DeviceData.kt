package dji.sampleV5.aircraft.data

import com.google.gson.Gson
import com.google.gson.GsonBuilder
import com.google.gson.JsonObject
import dji.sdk.keyvalue.key.DJIKeyInfo
import dji.sdk.keyvalue.key.KeyTools
import dji.v5.manager.KeyManager
import java.util.UUID

// 通用的获取数据的函数
inline fun <reified T> getValueForKey(keyInfo: DJIKeyInfo<T>): T? {
    return try {
        val key = KeyTools.createKey(keyInfo)
        KeyManager.getInstance().getValue(key)
    } catch (e: Exception) {
        null
    }
}

/**
 * 仅当值非 null 时写入 JSON，避免后端收到 null 字段导致下游误判为"状态丢失"。
 * 标量字段（Boolean / Number / String）各自提供一个重载。
 */
fun JsonObject.addIfNotNull(name: String, value: Boolean?) {
    if (value != null) addProperty(name, value)
}

fun JsonObject.addIfNotNull(name: String, value: Number?) {
    if (value != null) addProperty(name, value)
}

fun JsonObject.addIfNotNull(name: String, value: String?) {
    if (value != null) addProperty(name, value)
}

/**
 * 飞机位置数据（匹配示例格式）
 * 顺序：longitude, latitude, height
 */
data class AircraftLocation3DData(
    var longitude: Double? = null,
    var latitude: Double? = null,
    var height: Double? = null
) {
    fun toJsonObject(): JsonObject = JsonObject().apply {
        addProperty("longitude", longitude)
        addProperty("latitude", latitude)
        addProperty("height", height)
    }
}

/**
 * 飞机姿态数据（匹配示例格式）
 * 顺序：pitch, roll, yaw
 */
data class AircraftAttitudeData(
    var pitch: Double? = null,
    var roll: Double? = null,
    var yaw: Double? = null
) {
    fun toJsonObject(): JsonObject = JsonObject().apply {
        addProperty("pitch", pitch)
        addProperty("roll", roll)
        addProperty("yaw", yaw)
    }
}

/**
 * 飞机速度数据（匹配示例格式）
 * 顺序：horizonVelocity, verticalVelocity
 */
data class AircraftVelocityData(
    var horizonVelocity: Double? = null,
    var verticalVelocity: Double? = null
) {
    fun toJsonObject(): JsonObject = JsonObject().apply {
        addProperty("horizonVelocity", horizonVelocity)
        addProperty("verticalVelocity", verticalVelocity)
    }
}

/**
 * 飞行数据上报格式（严格按照示例消息顺序）
 * 
 * 注意：字段顺序必须与示例完全一致！
 */
data class FlightReportData(
    // 1. 消息ID
    var tid: String = UUID.randomUUID().toString(),
    // 2. 连接状态
    var connection: Boolean? = null,
    // 3. 是否飞行
    var isFlying: Boolean? = null,
    // 4. 飞行时间（秒）
    var flightTimeInSeconds: Int? = null,
    // 5. 飞机位置
    var aircraftLocation3D: AircraftLocation3DData? = null,
    // 6. 飞机姿态
    var aircraftAttitude: AircraftAttitudeData? = null,
    // 7. 飞机速度
    var aircraftVelocity: AircraftVelocityData? = null,
    // 8. 起飞点海拔
    var takeoffLocationAltitude: Double? = null,
    // 8a. 相对起飞点高度（KeyAltitude，米）
    var relativeAltitudeFromTakeoff: Double? = null,
    // 8b. 当前海拔高度 AMSL ≈ 相对高度 + 起飞点海拔（米）
    var altitudeAMSL: Double? = null,
    // 9. 卫星数量
    var satelliteCount: Int? = null,
    // 10. GNSS信号等级
    var GNSSSignalLevel: Int? = null,
    // 11. compassHeading - 罗盘航向
    var compassHeading: Double? = null,
    // 12. compassHasError - 罗盘错误
    var compassHasError: Boolean? = null,
    // 13. ultrasonicHeight - 超声波高度
    var ultrasonicHeight: Int? = null,
    // 14. windWarning - 风力警告
    var windWarning: Int? = null,
    // 15. windSpeed - 风速
    var windSpeed: Int? = null,
    // 16. windDirection - 风向
    var windDirection: Int? = null,
    // 17. currentWaypointIndex - 当前航点索引
    var currentWaypointIndex: Int? = null,
    // 18. flightMode - 飞行模式
    var flightMode: Int? = null,
    // 19. handsetLatitude - 遥控器纬度（GPS获取）
    var handsetLatitude: Double? = null,
    // 20. handsetLongitude - 遥控器经度（GPS获取）
    var handsetLongitude: Double? = null,
    // 21. currentTaskStatus - 当前任务状态
    var currentTaskStatus: Int? = null,
    // 22. waypointMissionExecuteState - 航线任务执行状态
    var waypointMissionExecuteState: String? = null,
    // 23. cameraMode - 相机拍摄模式（如 PHOTO_NORMAL / VIDEO_NORMAL）
    var cameraMode: String? = null,

    var UAVBatteryRemaining : Int? = null
) {
    /**
     * 生成新的 tid 并按严格顺序转换为 JSON 字符串
     * 
     * 注意：Gson 默认按字母顺序序列化，这里手动构建保证顺序
     */
    fun toJsonString(): String {
        tid = UUID.randomUUID().toString()

        val json = JsonObject().apply {
            // 1. tid（始终写入，作为消息ID）
            addProperty("tid", tid)
            // 其余字段仅在非 null 时写入，避免 SDK 瞬时返回 null 时被下游误判
            addIfNotNull("connection", connection)
            addIfNotNull("isFlying", isFlying)
            addIfNotNull("flightTimeInSeconds", flightTimeInSeconds)
            aircraftLocation3D?.let { add("aircraftLocation3D", it.toJsonObject()) }
            aircraftAttitude?.let { add("aircraftAttitude", it.toJsonObject()) }
            aircraftVelocity?.let { add("aircraftVelocity", it.toJsonObject()) }
            addIfNotNull("takeoffLocationAltitude", takeoffLocationAltitude)
            addIfNotNull("relativeAltitudeFromTakeoff", relativeAltitudeFromTakeoff)
            addIfNotNull("altitudeAMSL", altitudeAMSL)
            addIfNotNull("satelliteCount", satelliteCount)
            addIfNotNull("GNSSSignalLevel", GNSSSignalLevel)
            addIfNotNull("compassHeading", compassHeading)
            addIfNotNull("compassHasError", compassHasError)
            addIfNotNull("ultrasonicHeight", ultrasonicHeight)
            addIfNotNull("windWarning", windWarning)
            addIfNotNull("windSpeed", windSpeed)
            addIfNotNull("windDirection", windDirection)
            addIfNotNull("currentWaypointIndex", currentWaypointIndex)
            addIfNotNull("flightMode", flightMode)
            addIfNotNull("handsetLatitude", handsetLatitude)
            addIfNotNull("handsetLongitude", handsetLongitude)
            addIfNotNull("currentTaskStatus", currentTaskStatus)
            addIfNotNull("waypointMissionExecuteState", waypointMissionExecuteState)
            addIfNotNull("cameraMode", cameraMode)
            addIfNotNull("UAVBatteryRemaining", UAVBatteryRemaining)
        }

        return json.toString()
    }

    fun toPrettyJsonString(): String {
        tid = UUID.randomUUID().toString()
        val gson = GsonBuilder().setPrettyPrinting().create()

        val json = JsonObject().apply {
            addProperty("tid", tid)
            addIfNotNull("connection", connection)
            addIfNotNull("isFlying", isFlying)
            addIfNotNull("flightTimeInSeconds", flightTimeInSeconds)
            aircraftLocation3D?.let { add("aircraftLocation3D", it.toJsonObject()) }
            aircraftAttitude?.let { add("aircraftAttitude", it.toJsonObject()) }
            aircraftVelocity?.let { add("aircraftVelocity", it.toJsonObject()) }
            addIfNotNull("takeoffLocationAltitude", takeoffLocationAltitude)
            addIfNotNull("relativeAltitudeFromTakeoff", relativeAltitudeFromTakeoff)
            addIfNotNull("altitudeAMSL", altitudeAMSL)
            addIfNotNull("satelliteCount", satelliteCount)
            addIfNotNull("GNSSSignalLevel", GNSSSignalLevel)
            addIfNotNull("compassHeading", compassHeading)
            addIfNotNull("compassHasError", compassHasError)
            addIfNotNull("ultrasonicHeight", ultrasonicHeight)
            addIfNotNull("windWarning", windWarning)
            addIfNotNull("windSpeed", windSpeed)
            addIfNotNull("windDirection", windDirection)
            addIfNotNull("currentWaypointIndex", currentWaypointIndex)
            addIfNotNull("flightMode", flightMode)
            addIfNotNull("cameraMode", cameraMode)
        }

        return gson.toJson(json)
    }
}

// ==================== 以下保留旧的数据类，用于其他功能 ====================

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

// 飞机数据（完整版，用于内部）
data class AircraftData(
    var tid: String? = null,
    var isConnected: Boolean? = null,
    var isFlying: Boolean? = null,
    var flightTimeInSeconds: Int? = null,
    var aircraftLocation3D: dji.sdk.keyvalue.value.common.LocationCoordinate3D? = null,
    var aircraftAttitude: dji.sdk.keyvalue.value.common.Attitude? = null,
    var aircraftVelocity: dji.sdk.keyvalue.value.common.Velocity3D? = null,
    var takeoffLocationAltitude: Double? = null,
    var satelliteCount: Int? = null,
    var GNSSSignalLevel: dji.sdk.keyvalue.value.flightcontroller.GPSSignalLevel? = null,
    var compassHeading: Double? = null,
    var compassHasError: Boolean? = null,
    var ultrasonicHeight: Int? = null,
    var windWarning: dji.sdk.keyvalue.value.flightcontroller.WindWarning? = null,
    var windSpeed: Int? = null,
    var windDirection: dji.sdk.keyvalue.value.flightcontroller.WindDirection? = null,
    var currentWaypointIndex: Int? = null,
    var flightMode: dji.sdk.keyvalue.value.flightcontroller.FlightMode? = null,
    /** 相对起飞点高度（米），FlightControllerKey.KeyAltitude */
    var relativeAltitudeFromTakeoff: Double? = null,
    /** 主云台相机拍摄模式名称（监听 LEFT_OR_MAIN） */
    var cameraShootingModeName: String? = null,
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
    val key: String,
    val tid: String,
    val time: String,
    val content: String,
    val type: Int,
    val code: String
) {
    fun getFormattedTime(): String {
        return time
    }
}
