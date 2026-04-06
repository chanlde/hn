package dji.sampleV5.aircraft.video.streamer

import android.media.MediaCodec
import android.os.Handler
import android.os.Looper
import android.util.Log
import com.dji.util.FileLogger
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
import java.util.concurrent.atomic.AtomicBoolean
import kotlin.math.min

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
        /** 重连最大退避间隔（毫秒） */
        private const val RECONNECT_MAX_DELAY_MS: Long = 30_000L

        /**
         * 创建推流器实例
         */
        fun create(rtmpUrl: String): FpvRtmpStreamer {
            return FpvRtmpStreamer(StreamConfig(rtmpUrl = rtmpUrl))
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

    /** 用户已 start 且未 stop，用于断线后自动重连 */
    private val sessionActive = AtomicBoolean(false)
    private val mainHandler = Handler(Looper.getMainLooper())
    private var reconnectRunnable: Runnable? = null
    private var reconnectAttemptCount = 0
    
    // RTMP 连接检查器
    private val connectChecker = object : ConnectChecker {
        override fun onConnectionStarted(url: String) {
            if (config.enableVerboseLog) {
                Log.d(TAG, "连接中: $url")
            }
            callbacks.forEach { it.onConnectionStarted(url) }
        }
        
        override fun onConnectionSuccess() {
            FileLogger.i(TAG, "RTMP 已连接 url=${config.rtmpUrl}")
            stateManager.setConnected(true)
            reconnectAttemptCount = 0
            cancelReconnectSchedule()
            // 重连后需重新下发 SPS/PPS
            stateManager.setSpsSet(false)
            timestampManager.reset()
            callbacks.forEach { it.onConnectionSuccess() }
        }
        
        override fun onConnectionFailed(reason: String) {
            FileLogger.w(TAG, "RTMP 连接失败: $reason")
            stateManager.setConnected(false)
            callbacks.forEach { it.onConnectionFailed(reason) }
            scheduleRtmpReconnect("连接失败: $reason")
        }
        
        override fun onDisconnect() {
            FileLogger.w(TAG, "RTMP 已断开，将尝试重连")
            stateManager.setConnected(false)
            callbacks.forEach { it.onDisconnect() }
            scheduleRtmpReconnect("断开")
        }
        
        override fun onAuthError() {
            FileLogger.e(TAG, "RTMP 认证失败", null)
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
            FileLogger.e(TAG, "处理视频帧失败: ${e.message}", e)
            callbacks.forEach { it.onError(e) }
        }
    }
    
    init {
        // 设置帧处理器的回调
        frameProcessor.onFrameReady = { frameData, bufferInfo ->
            sendVideoFrame(frameData, bufferInfo)
        }
    }

    private fun cancelReconnectSchedule() {
        reconnectRunnable?.let { mainHandler.removeCallbacks(it) }
        reconnectRunnable = null
    }

    /**
     * RTMP 断线后指数退避重连。autoReconnectCount==0 表示不限制次数。
     */
    private fun scheduleRtmpReconnect(reason: String) {
        if (!sessionActive.get()) return
        val maxAttempts = config.autoReconnectCount
        if (maxAttempts > 0 && reconnectAttemptCount >= maxAttempts) {
            FileLogger.e(TAG, "RTMP 已达最大重连次数 $maxAttempts，停止 ($reason)", null)
            return
        }
        cancelReconnectSchedule()
        val exp = min(reconnectAttemptCount, 5)
        val delay = min(config.reconnectIntervalMs * (1L shl exp), RECONNECT_MAX_DELAY_MS).coerceAtLeast(500L)
        reconnectAttemptCount++
        FileLogger.w(TAG, "RTMP 重连调度 attempt=$reconnectAttemptCount delayMs=$delay reason=$reason")
        val runnable = Runnable {
            if (!sessionActive.get() || rtmpClient == null) return@Runnable
            try {
                FileLogger.i(TAG, "RTMP 执行重连 url=${config.rtmpUrl}")
                rtmpClient?.connect(config.rtmpUrl)
            } catch (e: Exception) {
                FileLogger.e(TAG, "RTMP 重连异常: ${e.message}", e)
                scheduleRtmpReconnect("connect异常: ${e.message}")
            }
        }
        reconnectRunnable = runnable
        mainHandler.postDelayed(runnable, delay)
    }
    
    /**
     * 开始推流
     */
    fun startStreaming() {
        if (stateManager.isStreaming()) {
            FileLogger.w(TAG, "RTMP 已在推流中，忽略重复 start")
            return
        }

        sessionActive.set(true)
        reconnectAttemptCount = 0
        cancelReconnectSchedule()
        
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
        
        FileLogger.i(TAG, "RTMP 推流启动 url=${config.rtmpUrl}")
    }
    
    /**
     * 停止推流
     */
    fun stopStreaming() {
        sessionActive.set(false)
        cancelReconnectSchedule()
        reconnectAttemptCount = 0

        stateManager.reset()
        timestampManager.reset()
        frameProcessor.reset()
        currentCodecInfo = null
        
        streamManager?.removeReceiveStreamListener(streamListener)
        streamManager = null
        
        rtmpClient?.disconnect()
        rtmpClient = null
        
        callbacks.forEach { it.onStreamingStopped() }
        FileLogger.i(TAG, "RTMP 推流已停止")
    }
    
    /**
     * 处理视频帧
     */
    private fun processFrame(data: ByteArray, info: StreamInfo) {
        // AI 分析（如果启用）
        aiAnalyzer?.takeIf { it.isEnabled() }?.analyzeFrame(data, info)

        // 处理帧数据（包含多个 NAL 单元）
        val result = frameProcessor.processFrame(data, info)
        when (result) {
            VideoFrameProcessor.ProcessResult.CodecInfoUpdated -> {
                val newCodecInfo = frameProcessor.getCodecInfo()
                if (newCodecInfo != null) {
                    if (newCodecInfo.isComplete()) {
                        checkAndUpdateVideoConfig(newCodecInfo)
                    } else {
                        FileLogger.throttledD(TAG, "codecIncomplete", "编码参数未齐 sps=${newCodecInfo.sps?.size} pps=${newCodecInfo.pps?.size} vps=${newCodecInfo.vps?.size}", 5_000L)
                    }
                } else {
                    FileLogger.w(TAG, "编码信息为 null")
                }
            }
            VideoFrameProcessor.ProcessResult.FrameReady -> {
                val currentInfo = frameProcessor.getCodecInfo()
                if (currentInfo != null && currentInfo.isComplete() && !stateManager.isSpsSet()) {
                    FileLogger.w(TAG, "补设 RTMP 视频参数 SPS=${currentInfo.sps?.size} PPS=${currentInfo.pps?.size}")
                    checkAndUpdateVideoConfig(currentInfo)
                }
            }
            VideoFrameProcessor.ProcessResult.WaitingForCodecInfo -> { }
            VideoFrameProcessor.ProcessResult.Invalid -> {
                FileLogger.throttledD(TAG, "frameInvalid", "无效视频帧", 10_000L)
            }
            VideoFrameProcessor.ProcessResult.Unsupported -> {
                FileLogger.w(TAG, "不支持的编码格式")
            }
        }
    }
    
    /**
     * 检查并更新视频配置
     */
    private fun checkAndUpdateVideoConfig(newCodecInfo: VideoCodecInfo) {
        val hasChanged = newCodecInfo.hasChanged(currentCodecInfo)
        if (!hasChanged) return

        timestampManager.setFrameRate(newCodecInfo.frameRate)
        val client = rtmpClient
        if (client == null) {
            FileLogger.e(TAG, "RTMP 客户端为 null，无法设置视频参数", null)
            return
        }
        client.setVideoResolution(newCodecInfo.width, newCodecInfo.height)
        if (newCodecInfo.mimeType == ICameraStreamManager.MimeType.H265) {
            client.setVideoInfo(
                ByteBuffer.wrap(newCodecInfo.sps!!),
                ByteBuffer.wrap(newCodecInfo.pps!!),
                ByteBuffer.wrap(newCodecInfo.vps!!)
            )
        } else {
            client.setVideoInfo(
                ByteBuffer.wrap(newCodecInfo.sps!!),
                ByteBuffer.wrap(newCodecInfo.pps!!),
                null
            )
        }

        stateManager.setSpsSet(true)
        currentCodecInfo = newCodecInfo

        val mimeTypeStr = when (newCodecInfo.mimeType) {
            ICameraStreamManager.MimeType.H264 -> "H.264"
            ICameraStreamManager.MimeType.H265 -> "H.265"
            else -> "Unknown"
        }
        FileLogger.i(
            TAG,
            "RTMP 视频参数已更新 ${newCodecInfo.width}x${newCodecInfo.height} $mimeTypeStr fps=${newCodecInfo.frameRate}"
        )
        callbacks.forEach {
            it.onVideoConfigSet(newCodecInfo.width, newCodecInfo.height, mimeTypeStr)
        }
    }
    
    /**
     * 发送视频帧
     */
    private fun sendVideoFrame(data: ByteArray, bufferInfo: MediaCodec.BufferInfo) {
        if (!stateManager.isSpsSet()) {
            FileLogger.throttledD(TAG, "skipNoSps", "SPS/PPS 未设置，跳过送帧", 15_000L)
            return
        }
        val frameCount: Long = timestampManager.getFrameCount()
        if (frameCount % CACHE_DIAGNOSIS_INTERVAL_FRAMES == 0L) {
            diagnoseCacheStatusDuringStreaming()
        }
        // 设置时间戳（使用动态帧率）
        bufferInfo.presentationTimeUs = timestampManager.getNextTimestampByActualInterval()
        
        // 创建 ByteBuffer
        val buffer = ByteBuffer.wrap(data)
        
        // 发送帧
        rtmpClient?.sendVideo(buffer, bufferInfo)
    }

    private fun diagnoseCacheStatusDuringStreaming() {
        val client: RtmpClient = rtmpClient ?: return
        val cacheSizeBytes: Int = client.cacheSize
        val itemsInCache: Int = client.getItemsInCache()
        val frameRate: Int = currentCodecInfo?.frameRate ?: 30
        val delaySeconds: Float =
            if (itemsInCache > 0 && frameRate > 0) itemsInCache.toFloat() / frameRate else 0f
        FileLogger.throttledD(
            TAG,
            "rtmpCache",
            "RTMP缓存 KB=${cacheSizeBytes / 1024} 帧数=$itemsInCache 估延时=${"%.2f".format(delaySeconds)}s",
            10_000L
        )
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


