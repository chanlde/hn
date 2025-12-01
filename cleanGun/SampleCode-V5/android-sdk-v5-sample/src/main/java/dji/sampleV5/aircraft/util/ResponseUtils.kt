package dji.sampleV5.aircraft.util

import android.util.Log
import com.dji.flysafe.util.V_JsonUtil.gson
import com.tji.network.MqttManager
import dji.sampleV5.aircraft.data.UavControlResponse

// ==================== 工具方法 ====================

// 发送无效消息响应
private fun sendInvalidMessageResponse(
    response:UavControlResponse)
{
    sendResponse(response)
}

// 发送ACK响应
 fun sendResponse(
    response:UavControlResponse
) {
    val response = UavControlResponse(
        key = response.key,
        tid = response.tid,
        api = response.api,
        message = response.message,
        result = response.result
    )
    Log.d("response","$response")
    MqttManager.getInstance().publish(response.api, response.toJson())
}

 fun subscribeToTopic(
    mqttManager: MqttManager,
    topic: String,
    handler: (String) -> Unit
) {

    mqttManager.subscribe(
        topic = topic,
        onMessage = { message ->
            handler(message)
        }
    )
}