package dji.sampleV5.aircraft.mqtthandle

import android.util.Log
import org.json.JSONArray
import org.json.JSONObject

/**
 * MQTT 消息解析器
 * 职责：将 JSON 消息解析为数据类
 */
object MessageParser {
    private const val TAG = "MessageParser"

    /**
     * 解析基础请求（包含 key、tid）
     */
    fun parseBaseRequest(message: String, apiPrefix: String): BaseRequest? {
        return try {
            if (!isValidJson(message)) {
                Log.e(TAG, "无效的 JSON 消息: $message")
                return null
            }

            val json = JSONObject(message)
            BaseRequest(
                key = json.getString("key"),
                tid = json.getString("tid"),
                api = "${apiPrefix}_${json.getString("key")}"
            )
        } catch (e: Exception) {
            Log.e(TAG, "解析基础请求失败: ${e.message}", e)
            null
        }
    }

    /**
     * 解析航线任务请求
     */
    fun parseTaskFileRequest(message: String): TaskFileRequest? {
        return try {
            if (!isValidJson(message)) {
                Log.e(TAG, "无效的 JSON 消息: $message")
                return null
            }

            val json = JSONObject(message)
            TaskFileRequest(
                key = json.getString("key"),
                tid = json.getString("tid"),
                fileUrl = json.getString("filePath"),
                taskType = json.getInt("taskType"),
                taskId = json.getString("taskId"),
                autoUpload = json.getString("autoUpload") == "TRUE",
                returnHeight = json.getDouble("returnHeight"),
                totalPoint = json.getInt("totalPoint"),
                api = "/api/work/setTaskFile_${json.getString("key")}"
            )
        } catch (e: Exception) {
            Log.e(TAG, "解析航线任务请求失败: ${e.message}", e)
            null
        }
    }

    /**
     * 解析起飞请求
     */
    fun parseTakeoffRequest(message: String): TakeoffRequest? {
        return try {
            if (!isValidJson(message)) {
                Log.e(TAG, "无效的 JSON 消息: $message")
                return null
            }

            val json = JSONObject(message)
            TakeoffRequest(
                key = json.getString("key"),
                tid = json.getString("tid"),
                altitude = json.optDouble("altitude", 10.0),
                api = "/api/control/takeoff_${json.getString("key")}"
            )
        } catch (e: Exception) {
            Log.e(TAG, "解析起飞请求失败: ${e.message}", e)
            null
        }
    }

    /**
     * 解析云台控制请求
     */
    fun parseGimbalRequest(message: String): GimbalRequest? {
        return try {
            if (!isValidJson(message)) {
                Log.e(TAG, "无效的 JSON 消息: $message")
                return null
            }

            val json = JSONObject(message)
            GimbalRequest(
                key = json.getString("key"),
                tid = json.getString("tid"),
                pitch = json.optDouble("pitch", 0.0),
                roll = json.optDouble("roll", 0.0),
                yaw = json.optDouble("yaw", 0.0),
                api = "/api/gimbal/control_${json.getString("key")}"
            )
        } catch (e: Exception) {
            Log.e(TAG, "解析云台控制请求失败: ${e.message}", e)
            null
        }
    }

    /**
     * 检查是否是有效的 JSON 字符串
     */
    private fun isValidJson(message: String): Boolean {
        return try {
            JSONObject(message)
            true
        } catch (e: Exception) {
            try {
                JSONArray(message)
                true
            } catch (e: Exception) {
                false
            }
        }
    }
}

// ==================== 数据类 ====================

/**
 * 基础请求
 */
data class BaseRequest(
    val key: String,
    val tid: String,
    val api: String
)

/**
 * 航线任务请求
 */
data class TaskFileRequest(
    val key: String,
    val tid: String,
    val fileUrl: String,
    val taskType: Int,
    val taskId: String,
    val autoUpload: Boolean,
    val returnHeight: Double,
    val totalPoint: Int,
    val api: String
)

/**
 * 起飞请求
 */
data class TakeoffRequest(
    val key: String,
    val tid: String,
    val altitude: Double,
    val api: String
)

/**
 * 云台控制请求
 */
data class GimbalRequest(
    val key: String,
    val tid: String,
    val pitch: Double,
    val roll: Double,
    val yaw: Double,
    val api: String
)