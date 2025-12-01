package dji.sampleV5.aircraft.video.streamer

import android.media.MediaCodec
import android.util.Log
import com.pedro.common.ConnectChecker
import com.pedro.rtmp.rtmp.RtmpClient
import dji.sampleV5.aircraft.video.ai.VideoFrameAnalyzer
import dji.sampleV5.aircraft.video.callback.StreamCallback
import dji.sampleV5.aircraft.video.config.StreamConfig
import dji.sampleV5.aircraft.video.config.VideoCodecInfo
import dji.sampleV5.aircraft.video.processor.VideoFrameProcessor
import dji.sampleV5.aircraft.video.timestamp.TimestampManager
import dji.sdk.keyvalue.value.common.ComponentIndexType
import dji.v5.manager.datacenter.MediaDataCenter
import dji.v5.manager.datacenter.camera.StreamInfo
import dji.v5.manager.interfaces.ICameraStreamManager
import java.nio.ByteBuffer
import java.util.concurrent.CopyOnWriteArrayList

/**
 * FPV RTMP 推流器（重构版）
 * 
 * 使用模块化设计，支持：
 * - 动态帧率检测
 * - 线程安全
 * - SPS/PPS 自动更新
 * - AI 识别扩展
 * - 自定义回调
 */
class FpvRtmpStreamer private constructor(private val config: StreamConfig) {
    
    companion object {
        private const val TAG = "FpvRtmpStreamer"

        private const val CACHE_DIAGNOSIS_INTERVAL_FRAMES: Long = 30L

        /**
         * 创建推流器实例
         */
        fun create(rtmpUrl: String): FpvRtmpStreamer {
            return FpvRtmpStreamer(StreamConfig(rtmpUrl = rtmpUrl))
        }
        
        /**
         * 使用配置创建推流器实例
         */
        fun create(config: StreamConfig): FpvRtmpStreamer {
            return FpvRtmpStreamer(config)
        }
    }
    
    private var streamManager: ICameraStreamManager? = null
    private var rtmpClient: RtmpClient? = null
    
    // 模块化组件
    private val stateManager = StreamStateManager()
    private val timestampManager = TimestampManager()
    private val frameProcessor = VideoFrameProcessor()
    
    // 回调列表（支持多个监听器）
    private val callbacks = CopyOnWriteArrayList<StreamCallback>()
    
    // AI 分析器（可选）
    private var aiAnalyzer: VideoFrameAnalyzer? = null
    
    // 当前编码信息
    var currentCodecInfo: VideoCodecInfo? = null
    
    // RTMP 连接检查器
    private val connectChecker = object : ConnectChecker {
        override fun onConnectionStarted(url: String) {
            if (config.enableVerboseLog) {
                Log.d(TAG, "连接中: $url")
            }
            callbacks.forEach { it.onConnectionStarted(url) }
        }
        
        override fun onConnectionSuccess() {
            Log.d(TAG, "连接成功")
            stateManager.setConnected(true)
            timestampManager.reset()
            callbacks.forEach { it.onConnectionSuccess() }
        }
        
        override fun onConnectionFailed(reason: String) {
            Log.e(TAG, "连接失败: $reason")
            stateManager.setConnected(false)
            callbacks.forEach { it.onConnectionFailed(reason) }
        }
        
        override fun onDisconnect() {
            Log.d(TAG, "已断开")
            stateManager.setConnected(false)
            callbacks.forEach { it.onDisconnect() }
        }
        
        override fun onAuthError() {
            Log.e(TAG, "认证失败")
            callbacks.forEach { it.onAuthError() }
        }
        
        override fun onAuthSuccess() {
            if (config.enableVerboseLog) {
                Log.d(TAG, "认证成功")
            }
            callbacks.forEach { it.onAuthSuccess() }
        }
        
        override fun onNewBitrate(bitrate: Long) {
            if (config.enableVerboseLog) {
                Log.d(TAG, "当前码率: ${bitrate / 1000} kbps")
            }
            callbacks.forEach { it.onBitrateChanged(bitrate) }
        }
    }
    
    // DJI 流监听器
    private val streamListener = ICameraStreamManager.ReceiveStreamListener { data, offset, length, info ->
        if (!stateManager.isConnected()) return@ReceiveStreamListener
        
        try {
            val frameData = data.copyOfRange(offset, offset + length)
            processFrame(frameData, info)
        } catch (e: Exception) {
            Log.e(TAG, "处理帧失败: ${e.message}", e)
            callbacks.forEach { it.onError(e) }
        }
    }
    
    init {
        // 设置帧处理器的回调
        frameProcessor.onFrameReady = { frameData, bufferInfo ->
            sendVideoFrame(frameData, bufferInfo)
        }
    }
    
    /**
     * 开始推流
     */
    fun startStreaming() {
        if (stateManager.isStreaming()) {
            Log.w(TAG, "已经在推流中")
            return
        }
        
        // 初始化 RTMP 客户端
        rtmpClient = RtmpClient(connectChecker).apply {
            setOnlyVideo(config.videoOnly)
        }
        
        // 连接 RTMP 服务器
        rtmpClient?.connect(config.rtmpUrl)
        stateManager.setStreaming(true)
        
        // 获取 DJI 相机流管理器并开始监听
        streamManager = MediaDataCenter.getInstance().cameraStreamManager

        streamManager?.addReceiveStreamListener(ComponentIndexType.LEFT_OR_MAIN, streamListener)
        
        Log.d(TAG, "开始推流到: ${config.rtmpUrl}")
    }
    
    /**
     * 停止推流
     */
    fun stopStreaming() {
        stateManager.reset()
        timestampManager.reset()
        frameProcessor.reset()
        currentCodecInfo = null
        
        streamManager?.removeReceiveStreamListener(streamListener)
        streamManager = null
        
        rtmpClient?.disconnect()
        rtmpClient = null
        
        callbacks.forEach { it.onStreamingStopped() }
        Log.d(TAG, "推流已停止")
    }
    
    /**
     * 处理视频帧
     */
    private fun processFrame(data: ByteArray, info: StreamInfo) {
        // AI 分析（如果启用）
        aiAnalyzer?.takeIf { it.isEnabled() }?.analyzeFrame(data, info)

        // 处理帧数据（包含多个 NAL 单元）
        val result = frameProcessor.processFrame(data, info)
        Log.d(TAG, "========== processFrame 结果: $result ==========")
        
        when (result) {
            VideoFrameProcessor.ProcessResult.CodecInfoUpdated -> {
                Log.d(TAG, "编码信息已更新，检查是否完整...")
                val newCodecInfo = frameProcessor.getCodecInfo()
                Log.d(TAG, "获取到的编码信息: ${if (newCodecInfo != null) "非空" else "null"}")
                if (newCodecInfo != null) {
                    val isComplete = newCodecInfo.isComplete()
                    Log.d(TAG, "编码信息是否完整: $isComplete")
                    Log.d(TAG, "SPS: ${newCodecInfo.sps?.size ?: "null"}, PPS: ${newCodecInfo.pps?.size ?: "null"}, VPS: ${newCodecInfo.vps?.size ?: "null"}")
                    if (isComplete) {
                        Log.d(TAG, "编码信息完整，准备设置视频配置...")
                        checkAndUpdateVideoConfig(newCodecInfo, info)
                    } else {
                        Log.w(TAG, "编码信息不完整，等待更多数据...")
                    }
                } else {
                    Log.e(TAG, "编码信息为 null！")
                }
            }
            VideoFrameProcessor.ProcessResult.FrameReady -> {
                Log.d(TAG, "视频帧已准备好，准备发送...")
                // 检查编码信息是否已设置，如果没有设置但已完整，则设置
                val currentInfo = frameProcessor.getCodecInfo()
                Log.d(TAG, "检查编码信息: currentInfo=${if (currentInfo != null) "非空" else "null"}, " +
                        "isComplete=${currentInfo?.isComplete()}, isSpsSet=${stateManager.isSpsSet()}")
                
                if (currentInfo != null && currentInfo.isComplete() && !stateManager.isSpsSet()) {
                    Log.e(TAG, "⚠️⚠️⚠️ 编码信息完整但未设置，立即设置...")
                    Log.e(TAG, "SPS: ${currentInfo.sps?.size ?: "null"}, PPS: ${currentInfo.pps?.size ?: "null"}")
                    checkAndUpdateVideoConfig(currentInfo, info)
                } else {
                    if (currentInfo == null) {
                        Log.w(TAG, "编码信息为 null，无法设置")
                    } else if (!currentInfo.isComplete()) {
                        Log.w(TAG, "编码信息不完整: SPS=${currentInfo.sps?.size ?: "null"}, PPS=${currentInfo.pps?.size ?: "null"}")
                    } else if (stateManager.isSpsSet()) {
                        Log.d(TAG, "编码信息已设置，继续发送帧")
                    }
                }
                // 帧已准备好，会在 onFrameReady 回调中发送
            }
            VideoFrameProcessor.ProcessResult.WaitingForCodecInfo -> {
                Log.d(TAG, "等待编码信息，跳过此帧")
                // 等待编码信息，不处理
            }
            VideoFrameProcessor.ProcessResult.Invalid -> {
                Log.w(TAG, "无效的帧数据")
            }
            VideoFrameProcessor.ProcessResult.Unsupported -> {
                Log.w(TAG, "不支持的编码格式")
            }
        }
    }
    
    /**
     * 检查并更新视频配置
     */
    private fun checkAndUpdateVideoConfig(newCodecInfo: VideoCodecInfo, info: StreamInfo) {
        Log.d(TAG, "========== checkAndUpdateVideoConfig 开始 ==========")
        Log.d(TAG, "当前编码信息: ${if (currentCodecInfo != null) "存在" else "null"}")
        Log.d(TAG, "新编码信息: ${newCodecInfo.width}x${newCodecInfo.height}, ${newCodecInfo.mimeType}")
        Log.d(TAG, "新编码信息 SPS: ${newCodecInfo.sps?.size ?: "null"}, PPS: ${newCodecInfo.pps?.size ?: "null"}, VPS: ${newCodecInfo.vps?.size ?: "null"}")
        
        // 检查编码信息是否变化
        val hasChanged = newCodecInfo.hasChanged(currentCodecInfo)
        Log.d(TAG, "编码信息是否变化: $hasChanged")

        
        if (hasChanged) {
            Log.d(TAG, "编码信息已变化，开始设置视频参数...")
            
            // 更新帧率
            timestampManager.setFrameRate(newCodecInfo.frameRate)
            Log.d(TAG, "帧率已更新: ${newCodecInfo.frameRate}fps")
            
            // 设置视频参数
            if (rtmpClient == null) {
                Log.e(TAG, "RTMP 客户端为 null，无法设置视频参数！")
            } else {
                Log.d(TAG, "设置视频分辨率: ${newCodecInfo.width}x${newCodecInfo.height}")
                rtmpClient?.setVideoResolution(newCodecInfo.width, newCodecInfo.height)
                
                if (newCodecInfo.mimeType == ICameraStreamManager.MimeType.H265) {
                    Log.d(TAG, "设置 H.265 视频信息: SPS=${newCodecInfo.sps?.size}, PPS=${newCodecInfo.pps?.size}, VPS=${newCodecInfo.vps?.size}")
                    rtmpClient?.setVideoInfo(
                        ByteBuffer.wrap(newCodecInfo.sps!!),
                        ByteBuffer.wrap(newCodecInfo.pps!!),
                        ByteBuffer.wrap(newCodecInfo.vps!!)
                    )
                } else {
                    Log.d(TAG, "设置 H.264 视频信息: SPS=${newCodecInfo.sps?.size}, PPS=${newCodecInfo.pps?.size}")
                    rtmpClient?.setVideoInfo(
                        ByteBuffer.wrap(newCodecInfo.sps!!),
                        ByteBuffer.wrap(newCodecInfo.pps!!),
                        null
                    )
                }
                Log.d(TAG, "视频信息已设置到 RTMP 客户端")
            }
            
            stateManager.setSpsSet(true)
            currentCodecInfo = newCodecInfo
            
            val mimeTypeStr = when (newCodecInfo.mimeType) {
                ICameraStreamManager.MimeType.H264 -> "H.264"
                ICameraStreamManager.MimeType.H265 -> "H.265"
                else -> "Unknown"
            }
            
            Log.e(TAG, "========== ✅ 视频参数已设置: ${newCodecInfo.width}x${newCodecInfo.height}, 编码: $mimeTypeStr, 帧率: ${newCodecInfo.frameRate}fps ==========")
            
            callbacks.forEach { 
                it.onVideoConfigSet(newCodecInfo.width, newCodecInfo.height, mimeTypeStr) 
            }
        } else {
            Log.d(TAG, "编码信息未变化，跳过设置（可能已经设置过了）")
        }
        Log.d(TAG, "========== checkAndUpdateVideoConfig 结束 ==========")
    }
    
    /**
     * 发送视频帧
     */
    private fun sendVideoFrame(data: ByteArray, bufferInfo: MediaCodec.BufferInfo) {
        if (!stateManager.isSpsSet()) {
            Log.w(TAG, "SPS/PPS 未设置，跳过发送视频帧")
            return
        }
        val frameCount: Long = timestampManager.getFrameCount()
        if (frameCount % CACHE_DIAGNOSIS_INTERVAL_FRAMES == 0L) {
            diagnoseCacheStatusDuringStreaming(data.size)
        }
        // 设置时间戳（使用动态帧率）
        bufferInfo.presentationTimeUs = timestampManager.getNextTimestampByActualInterval()
        
        // 创建 ByteBuffer
        val buffer = ByteBuffer.wrap(data)
        
        val isKeyFrame = (bufferInfo.flags and MediaCodec.BUFFER_FLAG_KEY_FRAME) != 0
        Log.d(TAG, "发送视频帧: 大小=${data.size} 字节, 时间戳=${bufferInfo.presentationTimeUs}μs, 关键帧=$isKeyFrame")
        
        // 发送帧
        rtmpClient?.sendVideo(buffer, bufferInfo)
    }

    private fun diagnoseCacheStatusDuringStreaming(frameSizeBytes: Int): Unit {
        val client: RtmpClient = rtmpClient ?: return
        val cacheSizeBytes: Int = client.cacheSize
        val itemsInCache: Int = client.getItemsInCache()
        val frameRate: Int = currentCodecInfo?.frameRate ?: 30
        val delaySeconds: Float =
            if (itemsInCache > 0 && frameRate > 0) itemsInCache.toFloat() / frameRate else 0f
        val avgFrameBytes: Int =
            if (itemsInCache > 0 && cacheSizeBytes > 0) cacheSizeBytes / itemsInCache else frameSizeBytes
        Log.d("cache", "========== 📊 缓存监控 ==========")
        Log.d("cache", "缓存大小: ${cacheSizeBytes / 1024} KB")
        Log.d("cache", "缓存帧数: $itemsInCache")
        Log.d("cache", "平均帧大小: ${avgFrameBytes / 1024} KB")
        Log.d("cache", "估算延时: ${"%.2f".format(delaySeconds)}秒")
        Log.d("cache", "==================================")
    }
    /**
     * 添加回调监听器
     */
    fun addCallback(callback: StreamCallback) {
        callbacks.add(callback)
    }
    
    /**
     * 移除回调监听器
     */
    fun removeCallback(callback: StreamCallback) {
        callbacks.remove(callback)
    }
    
    /**
     * 设置 AI 分析器
     */
    fun setAiAnalyzer(analyzer: VideoFrameAnalyzer?) {
        this.aiAnalyzer = analyzer
    }
    
    /**
     * 是否正在推流
     */
    fun isStreaming(): Boolean = stateManager.isStreaming()
    
    /**
     * 获取当前编码信息
     */
    fun getCodecInfo(): VideoCodecInfo? = currentCodecInfo
    
    /**
     * 获取实际帧率
     */
    fun getActualFrameRate(): Float = timestampManager.getActualFrameRate()
}


