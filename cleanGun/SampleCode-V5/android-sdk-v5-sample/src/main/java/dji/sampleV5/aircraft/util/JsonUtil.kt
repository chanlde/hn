package dji.sampleV5.aircraft.util

import android.util.Log
import com.google.gson.Gson
import com.google.gson.GsonBuilder
import com.google.gson.reflect.TypeToken
import com.tji.network.MqttManager
import dji.sampleV5.aircraft.data.UavControlResponse
import org.json.JSONObject

object JsonUtil {
    private const val TAG = "JsonUtil"

    val gson: Gson by lazy {
        GsonBuilder()
            .disableHtmlEscaping()
            .create()
    }
}

// 扩展函数 - 序列化
fun Any.toJson(): String {
    return when (this) {
        is UavControlResponse -> this.toOrderedJson()
        else -> JsonUtil.gson.toJson(this)
    }
}

// UavControlResponse 的有序 JSON 序列化
fun UavControlResponse.toOrderedJson(): String {
    return JSONObject().apply {
        put("key", key)
        put("tid", tid)
        put("api", api)
        put("message", message)
        put("result", result)
    }.toString()
}

// 扩展函数 - 反序列化，带异常处理
inline fun <reified T> String.fromJson(): T? {
    return try {
        JsonUtil.gson.fromJson(this, T::class.java) // 尝试解析JSON
    } catch (e: Exception) {
        Log.e("JsonUtil", "反序列化失败: ${e.message}", e) // 记录错误
        null // 返回 null，避免崩溃
    }
}

// 错误响应方法 - 如果收到无法解析的消息，发送错误响应
private fun sendInvalidMessageResponse(mqttManager: MqttManager, message: String) {
    val response = JSONObject().apply {
        put("message", "Invalid message format")
        put("original_message", message)
    }.toString()

    mqttManager.publish(
        topic = "/api/error",
        message = response,
        onSuccess = { Log.d("MqttSubscription", "发送错误应答成功") },
        onError = { Log.e("MqttSubscription", "发送错误应答失败: ${it.message}") }
    )
}