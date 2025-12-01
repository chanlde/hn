package dji.sampleV5.aircraft.mqtthandle

import android.util.Log
import dji.sdk.keyvalue.key.FlightControllerKey
import dji.sdk.keyvalue.key.KeyTools
import dji.v5.common.callback.CommonCallbacks
import dji.v5.manager.KeyManager

/**
 * 任务状态监听器
 * 职责：监听飞行状态变化，自动管理任务生命周期（起飞时开始任务，降落时结束任务）
 */
class MissionStateListener(
    private val taskManager: MissionTaskManager
) {
    
    companion object {
        private const val TAG = "MissionStateListener"
    }

    private var flyingListenerRegistered = false
    private val isFlying by lazy { KeyTools.createKey(FlightControllerKey.KeyIsFlying) }

    init {
        enableAutoControl()
    }

    /**
     * 启用自动任务控制
     */
    fun enableAutoControl() {
        if (flyingListenerRegistered) return

        KeyManager.getInstance().listen(isFlying, this, object : CommonCallbacks.KeyListener<Boolean> {
            override fun onValueChange(oldValue: Boolean?, newValue: Boolean?) {
                when {
                    // 起飞：开始任务
                    newValue == true && oldValue != true -> {
                        Log.d(TAG, "✈️ 检测到起飞，开始任务")
                        taskManager.startMission()
                    }
                    // 降落：结束任务
                    oldValue == true && newValue == false -> {
                        Log.d(TAG, "🛬 检测到降落，结束任务")
                        taskManager.endMission()
                    }
                }
            }
        })
        flyingListenerRegistered = true
        Log.i(TAG, "✅ 已启用自动任务管理")
    }
    
    /**
     * 销毁监听器
     */
    fun destroy() {
        if (flyingListenerRegistered) {
            KeyManager.getInstance().cancelListen(isFlying, this)
            flyingListenerRegistered = false
            Log.d(TAG, "任务状态监听器已销毁")
        }
    }
}
