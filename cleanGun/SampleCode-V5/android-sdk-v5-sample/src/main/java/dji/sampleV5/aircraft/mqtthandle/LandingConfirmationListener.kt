package dji.sampleV5.aircraft.mqtthandle

import android.util.Log
import dji.sdk.keyvalue.key.KeyTools
import dji.sdk.keyvalue.key.co_z.KeyConfirmLanding
import dji.sdk.keyvalue.key.co_z.KeyIsLandingConfirmationNeeded
import dji.sdk.keyvalue.value.common.EmptyMsg
import dji.v5.common.callback.CommonCallbacks
import dji.v5.common.error.IDJIError
import dji.v5.manager.KeyManager

/**
 * 降落确认监听器
 * 职责：监听飞机降落确认需求，自动确认降落
 */
class LandingConfirmationListener {

    companion object {
        private const val TAG = "LandingConfirmationListener"
    }
    
    private val confirmLandingKey = KeyTools.createKey(KeyConfirmLanding)
    private val landingConfirmationNeededKey = KeyTools.createKey(KeyIsLandingConfirmationNeeded)

    private val landingConfirmationCallback = object : CommonCallbacks.CompletionCallbackWithParam<EmptyMsg> {
        override fun onSuccess(t: EmptyMsg) {
            Log.d(TAG, "降落确认成功")
        }

        override fun onFailure(error: IDJIError) {
            Log.e(TAG, "降落确认失败: ${error.description()}")
        }
    }

    private val landingConfirmationListener = object : CommonCallbacks.KeyListener<Boolean> {
        override fun onValueChange(oldValue: Boolean?, newValue: Boolean?) {
            if (newValue == true) {
                handleLandingConfirmation()
            }
        }
    }

    init {
        registerLandingListener()
    }

    private fun handleLandingConfirmation() {
        KeyManager.getInstance().performAction(
            confirmLandingKey, landingConfirmationCallback
        )
    }

    private fun registerLandingListener() {
        KeyManager.getInstance().listen(
            landingConfirmationNeededKey, this, landingConfirmationListener
        )
    }

    /**
     * 销毁监听器
     */
    fun destroy() {
        KeyManager.getInstance().cancelListen(
            landingConfirmationNeededKey, landingConfirmationListener
        )
        Log.d(TAG, "降落确认监听器已销毁")
    }
}
