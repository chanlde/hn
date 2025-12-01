package dji.v5.ux.data

import com.google.gson.Gson
import dji.sdk.keyvalue.key.BatteryKey
import dji.sdk.keyvalue.key.DJIActionKeyInfo
import dji.sdk.keyvalue.key.DJIKey
import dji.sdk.keyvalue.key.DJIKeyInfo
import dji.sdk.keyvalue.key.FlightControllerKey
import dji.sdk.keyvalue.key.KeyTools
import dji.sdk.keyvalue.key.RemoteControllerKey
import dji.sdk.keyvalue.value.common.LocationCoordinate3D
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

// 设备信息的基类
interface DeviceData {
    fun updateData()
    fun getData(): Map<String, Any?>
}

// 电池信息类
class BatteryInfo : DeviceData {
    private val data = mutableMapOf<String, Any?>()

    private fun getBatteryPercentage() {
        val percentage = getValueForKey(BatteryKey.KeyChargeRemainingInPercent)
        data["batteryPercentage"] = percentage
    }

    override fun updateData() {
        getBatteryPercentage()
    }

    override fun getData(): Map<String, Any?> = data
}


// 遥控器信息类
class RemoteControllerInfo : DeviceData {
    private val data = mutableMapOf<String, Any?>()

    private fun getIsConnected() {
        val connectionStatus = getValueForKey(RemoteControllerKey.KeyConnection)
        data["isConnected"] = connectionStatus
    }

    override fun updateData() {
        getIsConnected()
    }

    override fun getData(): Map<String, Any?> = data
}


// 无人机信息类
class AircraftInfo : DeviceData {
    private val data = mutableMapOf<String, Any?>()

    private fun getLocation3D() {
        val location = getValueForKey(FlightControllerKey.KeyAircraftLocation3D)
        data["location3D"] = location
    }

    private fun getIsFlying() {
        val flyingStatus = getValueForKey(FlightControllerKey.KeyIsFlying)
        data["isFlying"] = flyingStatus
    }

    override fun updateData() {
        getLocation3D()
        getIsFlying()
    }

    override fun getData(): Map<String, Any?> = data
}


// 集中管理所有设备信息的类
class DeviceManager {
    private val devices: List<DeviceData> = listOf(BatteryInfo(), RemoteControllerInfo(), AircraftInfo())

    fun updateAllData() {
        devices.forEach { it.updateData() }
    }

    // 获取所有设备的数据，并将其转换为 JSON 字符串
    fun getAllDataAsJson(): String {
        val allDeviceData = devices.associate {
            (it::class.simpleName ?: "Unknown") to it.getData()
        }

        // 使用 Gson 将 Map 转换为 JSON 字符串
        val gson = Gson()
        return gson.toJson(allDeviceData)
    }
}
