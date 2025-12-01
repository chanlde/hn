package dji.sampleV5.aircraft.mqtthandle

import android.util.Log
import dji.sampleV5.aircraft.data.UavControlResponse
import dji.sampleV5.aircraft.util.sendResponse
import dji.sdk.keyvalue.key.KeyTools
import dji.sdk.keyvalue.key.co_z.KeyConfirmLanding
import dji.sdk.keyvalue.key.co_z.KeyStartAutoLanding
import dji.sdk.keyvalue.key.co_z.KeyStartGoHome
import dji.sdk.keyvalue.key.co_z.KeyStartTakeoff
import dji.sdk.keyvalue.value.common.EmptyMsg
import dji.v5.common.callback.CommonCallbacks
import dji.v5.common.error.IDJIError
import dji.v5.manager.KeyManager

/**
 * 飞行控制服务
 * 职责：处理起飞、降落、返航等飞行控制指令
 */
class FlightControlService {
    companion object {
        private const val TAG = "FlightControlService"
    }

    private val keyStartTakeoff = KeyTools.createKey(KeyStartTakeoff)
    private val keyStartAutoLanding = KeyTools.createKey(KeyStartAutoLanding)
    private val keyStartGoHome = KeyTools.createKey(KeyStartGoHome)

    /**
     * 起飞
     */
    fun takeoff(
        response: UavControlResponse
    ) {
        KeyManager.getInstance().performAction(keyStartTakeoff, object : CommonCallbacks.CompletionCallbackWithParam<EmptyMsg> {
            override fun onSuccess(t: EmptyMsg) {
                Log.d(TAG, "takeoff successfully")
                response.message="起飞成功"
                response.result=true.toString()
                sendResponse(response)
            }

            override fun onFailure(error: IDJIError) {
                Log.e(TAG, "takeoff failed: ${error.description()}")
                response.message = "起飞失败: ${error.description()}"
                response.result = false.toString()
                sendResponse(response)
            }
        })
    }

    /**
     * 降落
     */
    fun land(
        response:UavControlResponse
    ) {
        KeyManager.getInstance().performAction(keyStartAutoLanding, object : CommonCallbacks.CompletionCallbackWithParam<EmptyMsg> {
            override fun onSuccess(t: EmptyMsg) {
                Log.d(TAG, "Landing confirmed successfully")
                response.message="降落成功"
                response.result=true.toString()
                sendResponse(response)
            }

            override fun onFailure(error: IDJIError) {
                Log.e(TAG, "Landing confirmation failed: ${error.description()}")
                response.message = "降落失败: ${error.description()}"
                response.result = false.toString()
                sendResponse(response)
            }
        })
    }

    /**
     * 返航
     */
    fun returnHome(
       response:UavControlResponse
    ) {
        KeyManager.getInstance().performAction(keyStartGoHome, object : CommonCallbacks.CompletionCallbackWithParam<EmptyMsg> {
            override fun onSuccess(t: EmptyMsg) {
                Log.d(TAG, "Landing confirmed successfully")
                response.message="返航成功"
                response.result=true.toString()
                sendResponse(response)
            }

            override fun onFailure(error: IDJIError) {
                Log.e(TAG, "Landing confirmation failed: ${error.description()}")
                response.message = "返航失败: ${error.description()}"
                response.result = false.toString()
                sendResponse(response)
            }
        })
    }

    /**
     * 取消返航
     */
    fun cancelReturnHome(
        onSuccess: () -> Unit,
        onFailure: (String) -> Unit
    ) {
        try {
            Log.d(TAG, "取消返航")

            // TODO: 调用 MSDK 取消返航
            // FlightController.getInstance().cancelGoHome(...)

            onSuccess()

        } catch (e: Exception) {
            Log.e(TAG, "取消返航失败: ${e.message}", e)
            onFailure("取消返航异常: ${e.message}")
        }
    }
}