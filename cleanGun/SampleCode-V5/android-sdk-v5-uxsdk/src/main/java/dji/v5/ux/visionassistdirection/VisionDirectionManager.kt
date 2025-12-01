package dji.v5.ux.swing

import android.util.Log
import dji.sdk.keyvalue.key.KeyTools
import dji.sdk.keyvalue.key.RemoteControllerKey
import dji.v5.common.callback.CommonCallbacks
import dji.v5.common.error.IDJIError
import dji.v5.manager.datacenter.MediaDataCenter
import dji.sdk.keyvalue.value.flightassistant.VisionAssistDirection
import dji.v5.manager.KeyManager

/**
 * 视觉辅助方向管理器
 * 管理前后左右上下六个方向的切换
 */
class VisionDirectionManager {

    // 定义可用的方向列表（循环顺序）
    private val directions = listOf(
        VisionAssistDirection.FRONT,
        VisionAssistDirection.RIGHT,
        VisionAssistDirection.BACK,
        VisionAssistDirection.LEFT,
        VisionAssistDirection.UP,
        VisionAssistDirection.DOWN
    )

    // 当前方向索引
    private var currentIndex = 0

    // 当前方向
    val currentDirection: VisionAssistDirection
        get() = directions[currentIndex]

    init {
        initC1ButtonListener()
        initC2ButtonListener()
    }
    /**
     * 获取下一个方向
     * @return 下一个方向
     */
    fun getNextDirection(): VisionAssistDirection {
        currentIndex = (currentIndex + 1) % directions.size
        return currentDirection
    }

    /**
     * 获取上一个方向
     * @return 上一个方向
     */
    fun getPreviousDirection(): VisionAssistDirection {
        currentIndex = if (currentIndex == 0) {
            directions.size - 1
        } else {
            currentIndex - 1
        }
        return currentDirection
    }

    /**
     * 切换到下一个方向并设置
     */
    fun switchToNext() {
        val nextDirection = getNextDirection()
        setVisionAssistViewDirection(nextDirection)
        Log.d("VisionDirectionManager", "切换到下一个方向: $nextDirection (索引: $currentIndex)")
    }

    /**
     * 切换到上一个方向并设置
     */
    fun switchToPrevious() {
        val prevDirection = getPreviousDirection()
        setVisionAssistViewDirection(prevDirection)
        Log.d("VisionDirectionManager", "切换到上一个方向: $prevDirection (索引: $currentIndex)")
    }

    /**
     * 设置指定方向
     * @param direction 要设置的方向
     */
    fun setDirection(direction: VisionAssistDirection) {
        val index = directions.indexOf(direction)
        if (index != -1) {
            currentIndex = index
            setVisionAssistViewDirection(direction)
            Log.d("VisionDirectionManager", "设置方向: $direction (索引: $currentIndex)")
        } else {
            Log.w("VisionDirectionManager", "不支持的方向: $direction")
        }
    }

    /**
     * 获取方向名称（用于UI显示）
     */
    fun getDirectionName(direction: VisionAssistDirection = currentDirection): String {
        return when (direction) {
            VisionAssistDirection.FRONT -> "前方"
            VisionAssistDirection.BACK -> "后方"
            VisionAssistDirection.LEFT -> "左侧"
            VisionAssistDirection.RIGHT -> "右侧"
            VisionAssistDirection.UP -> "上方"
            VisionAssistDirection.DOWN -> "下方"
            else -> "未知"
        }
    }

    /**
     * 获取所有可用方向
     */
    fun getAllDirections(): List<VisionAssistDirection> {
        return directions.toList()
    }

    /**
     * 获取当前方向在列表中的位置
     */
    fun getCurrentIndex(): Int {
        return currentIndex
    }

    /**
     * 重置到第一个方向
     */
    fun reset() {
        currentIndex = 0
        setVisionAssistViewDirection(currentDirection)
        Log.d("VisionDirectionManager", "重置到初始方向: $currentDirection")
    }

    /**
     * 设置视觉辅助视图方向的内部方法
     */
    private fun setVisionAssistViewDirection(direction: VisionAssistDirection) {
        MediaDataCenter.getInstance().cameraStreamManager.setVisionAssistViewDirection(
            direction,
            object : CommonCallbacks.CompletionCallback {
                override fun onSuccess() {
                    Log.d("VisionDirectionManager", "设置方向成功: $direction")
                }

                override fun onFailure(error: IDJIError) {
                    Log.e("VisionDirectionManager", "设置方向失败: $direction, 错误: $error")
                }
            }
        )
    }

    companion object {
        // 单例实例
        @Volatile
        private var instance: VisionDirectionManager? = null

        @JvmStatic
        fun getInstance(): VisionDirectionManager {
            return instance ?: synchronized(this) {
                instance ?: VisionDirectionManager().also { instance = it }
            }
        }
    }

    private fun initC1ButtonListener() {
        // 创建C1按键的Key
        val c1Key = KeyTools.createKey(RemoteControllerKey.KeyCustomButton1Down)

        // 使用 KeyManager 监听 C1 按键变化
        KeyManager.getInstance().listen(c1Key, this, object : CommonCallbacks.KeyListener<Boolean> {
            override fun onValueChange(oldValue: Boolean?, newValue: Boolean?) {
                if (newValue == true) {
                    Log.i("CustomButton", "C1 按键被按下！ 当前dddddddddddddddddd")
                    switchToPrevious()

                }
            }
        })
    }

    private fun initC2ButtonListener() {
        // 创建C2按键的Key
        val c2Key = KeyTools.createKey(RemoteControllerKey.KeyCustomButton2Down)

        // 使用 KeyManager 监听 C2 按键变化
        KeyManager.getInstance().listen(c2Key, this, object : CommonCallbacks.KeyListener<Boolean> {
            override fun onValueChange(oldValue: Boolean?, newValue: Boolean?) {
                if (newValue == true) {

                    Log.i("CustomButton", "C1 按键被按下！ sssssssssssssssssssssss")
                    switchToNext()

                }
            }
        })
    }
}
