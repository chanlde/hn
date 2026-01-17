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
        Log.d(TAG, "==== 开始解析航线任务 ====")
        Log.d(TAG, "原始消息内容: $message")

        return try {
            if (!isValidJson(message)) {
                Log.e(TAG, "❌ 校验失败: 无效的 JSON 格式")
                return null
            }

            val json = JSONObject(message)

            // --- 深度打印所有存在的字段名，排查“货不对板”的问题 ---
            val keys = json.keys()
            val keyList = mutableListOf<String>()
            while (keys.hasNext()) {
                keyList.add(keys.next())
            }
            Log.d(TAG, "当前 JSON 包含的字段: $keyList")

            // --- 关键字段预检 ---
            if (!json.has("filePath")) {
                Log.w(TAG, "⚠️ 关键警告: 消息中缺少 'filePath' 字段，这可能是一条回执(Ack)消息而非指令")
                // 打印出类似 result 或 message 的内容辅助判断
                Log.d(TAG, "检测到其他字段 - result: ${json.optString("result")}, message: ${json.optString("message")}")
                return null
            }

            // --- 开始提取字段 ---
            val key = json.optString("key")
            val tid = json.optString("tid")
            val filePath = json.optString("filePath")
            val taskType = json.optInt("taskType")
            val taskId = json.optString("taskId")
            val autoUploadStr = json.optString("autoUpload")
            val returnHeight = json.optDouble("returnHeight", 0.0)
            val totalPoint = json.optInt("totalPoint", 0)

            Log.d(TAG, """
            ✅ 字段提取成功:
            - key: $key
            - filePath: $filePath
            - taskId: $taskId
            - taskType: $taskType
            - autoUpload: $autoUploadStr
        """.trimIndent())

            TaskFileRequest(
                key = key,
                tid = tid,
                fileUrl = filePath,
                taskType = taskType,
                taskId = taskId,
                autoUpload = autoUploadStr == "TRUE",
                returnHeight = returnHeight,
                totalPoint = totalPoint,
                api = "/api/work/setTaskFile_$key"
            )
        } catch (e: Exception) {
            Log.e(TAG, "❌ 解析异常中止!")
            Log.e(TAG, "异常原因: ${e.message}")
            e.printStackTrace()
            null
        } finally {
            Log.d(TAG, "==== 解析流程结束 ====")
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