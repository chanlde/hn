package dji.sampleV5.aircraft.video.streamer

import java.util.concurrent.atomic.AtomicBoolean
import java.util.concurrent.atomic.AtomicReference

/**
 * 推流状态管理器
 */
class StreamStateManager {
    
    private val isStreaming = AtomicBoolean(false)
    private val isConnected = AtomicBoolean(false)
    private val isSpsSet = AtomicBoolean(false)
    
    /**
     * 是否正在推流
     */
    fun isStreaming(): Boolean = isStreaming.get() && isConnected.get()
    
    /**
     * 是否已连接
     */
    fun isConnected(): Boolean = isConnected.get()
    
    /**
     * SPS/PPS 是否已设置
     */
    fun isSpsSet(): Boolean = isSpsSet.get()
    
    /**
     * 设置推流状态
     */
    fun setStreaming(streaming: Boolean) {
        isStreaming.set(streaming)
    }
    
    /**
     * 设置连接状态
     */
    fun setConnected(connected: Boolean) {
        isConnected.set(connected)
    }
    
    /**
     * 设置 SPS/PPS 状态
     */
    fun setSpsSet(spsSet: Boolean) {
        isSpsSet.set(spsSet)
    }
    
    /**
     * 重置所有状态
     */
    fun reset() {
        isStreaming.set(false)
        isConnected.set(false)
        isSpsSet.set(false)
    }
}


