package dji.sampleV5.aircraft.video.timestamp

import android.util.Log
import java.util.concurrent.atomic.AtomicLong

/**
 * 时间戳管理器
 * 负责生成连续递增的视频帧时间戳
 */
class TimestampManager {
    companion object {
        private const val TAG = "TimestampManager"
        private const val DEFAULT_FRAME_RATE = 30
        private const val MIN_FRAME_RATE = 15
        private const val MAX_FRAME_RATE = 60
    }
    
    private val frameTimestamp = AtomicLong(0)
    private var frameRate: Int = DEFAULT_FRAME_RATE
    private var timePerFrameUs: Long = 1_000_000L / DEFAULT_FRAME_RATE
    
    // 用于动态计算帧间隔
    private var lastFrameTimeUs: Long = 0
    private var frameCount: Long = 0
    private var actualFrameRate: Float = DEFAULT_FRAME_RATE.toFloat()
    
    /**
     * 重置时间戳计数器
     */
    fun reset() {
        frameTimestamp.set(0)
        lastFrameTimeUs = 0
        frameCount = 0
        actualFrameRate = frameRate.toFloat()
        Log.d(TAG, "时间戳已重置，帧率: ${frameRate}fps")
    }
    
    /**
     * 设置帧率
     */
    fun setFrameRate(rate: Int) {
        val validRate = rate.coerceIn(MIN_FRAME_RATE, MAX_FRAME_RATE)
        if (validRate != frameRate) {
            frameRate = validRate
            timePerFrameUs = 1_000_000L / validRate
            Log.d(TAG, "帧率已更新: ${frameRate}fps, 每帧间隔: ${timePerFrameUs}μs")
        }
    }
    
    /**
     * 获取下一个时间戳（基于固定帧率）
     */
    fun getNextTimestamp(): Long {
        return frameTimestamp.getAndAdd(timePerFrameUs)
    }
    
    /**
     * 获取下一个时间戳（基于实际帧间隔，更准确）
     */
    fun getNextTimestampByActualInterval(): Long {
        val currentTimeUs = System.nanoTime() / 1000
        
        if (lastFrameTimeUs == 0L) {
            // 第一帧
            lastFrameTimeUs = currentTimeUs
            frameCount = 1
            return 0
        }
        
        // 计算实际帧间隔
        val actualIntervalUs = currentTimeUs - lastFrameTimeUs
        lastFrameTimeUs = currentTimeUs
        
        // 平滑处理：如果间隔异常（太大或太小），使用理论值
        val expectedIntervalUs = timePerFrameUs
        val intervalUs = when {
            actualIntervalUs > expectedIntervalUs * 2 -> expectedIntervalUs  // 间隔太大，可能是丢帧
            actualIntervalUs < expectedIntervalUs / 2 -> expectedIntervalUs  // 间隔太小，可能是异常
            else -> actualIntervalUs
        }
        
        // 更新实际帧率统计
        frameCount++
        if (frameCount % 30 == 0L) {  // 每30帧更新一次统计
            actualFrameRate = 1_000_000f / intervalUs
        }
        
        // 返回累计时间戳
        return frameTimestamp.getAndAdd(intervalUs)
    }
    
    /**
     * 获取当前实际帧率
     */
    fun getActualFrameRate(): Float = actualFrameRate
    
    /**
     * 获取当前帧计数
     */
    fun getFrameCount(): Long = frameCount
}


