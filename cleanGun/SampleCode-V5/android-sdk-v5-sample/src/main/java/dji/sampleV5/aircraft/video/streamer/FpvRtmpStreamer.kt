package dji.sampleV5.aircraft.video.streamer

import android.os.Handler
import android.os.Looper
import android.os.SystemClock
import com.dji.util.FileLogger
import dji.sdk.keyvalue.value.common.ComponentIndexType
import dji.v5.common.callback.CommonCallbacks
import dji.v5.common.error.IDJIError
import dji.v5.manager.datacenter.MediaDataCenter
import dji.v5.manager.datacenter.livestream.LiveStreamSettings
import dji.v5.manager.datacenter.livestream.LiveStreamStatus
import dji.v5.manager.datacenter.livestream.LiveStreamStatusListener
import dji.v5.manager.datacenter.livestream.LiveStreamType
import dji.v5.manager.datacenter.livestream.StreamQuality
import dji.v5.manager.datacenter.livestream.settings.RtmpSettings
import dji.v5.manager.interfaces.ILiveStreamManager
import java.util.concurrent.atomic.AtomicBoolean
import kotlin.math.min

/**
 * 基于 DJI ILiveStreamManager 的 RTMP 推流器。
 *
 * SDK 内部完成编解码、封装、发送；飞控断连/重连后由 SDK 自行恢复视频通道，**不在此**监听飞控 KeyConnection 或主动 stop/start。
 * 仅保留：会话级 start/stop、[LiveStreamStatusListener] 掉线/错误时的指数退避重试。
 */
class FpvRtmpStreamer private constructor(
    private val rtmpUrl: String,
    private val cameraIndex: ComponentIndexType = ComponentIndexType.LEFT_OR_MAIN,
    private val quality: StreamQuality = StreamQuality.FULL_HD,
) {
    companion object {
        private const val TAG = "FpvRtmpStreamer"
        private const val RETRY_MAX_DELAY_MS = 30_000L
        private const val RETRY_BASE_MS = 2_000L
        private const val RETRY_EXP_CAP = 5
        private const val RETRY_COUNT_CAP = 64

        fun create(rtmpUrl: String): FpvRtmpStreamer {
            return FpvRtmpStreamer(rtmpUrl = rtmpUrl)
        }
    }

    private val liveStreamManager: ILiveStreamManager =
        MediaDataCenter.getInstance().liveStreamManager

    private val sessionActive = AtomicBoolean(false)
    private val mainHandler = Handler(Looper.getMainLooper())
    private var retryRunnable: Runnable? = null
    private var retryCount = 0

    @Volatile
    private var wasStreaming = false

    private val statusListener = object : LiveStreamStatusListener {
        override fun onLiveStreamStatusUpdate(status: LiveStreamStatus?) {
            status ?: return
            mainHandler.post {
                FileLogger.throttledD(
                    TAG,
                    "liveStatus",
                    "直播状态: streaming=${status.isStreaming}" +
                        " vbps=${status.vbps}" +
                        " fps=${status.fps}" +
                        " rtt=${status.rtt}" +
                        " loss=${status.packetLoss}" +
                        " cache=${status.packetCacheLen}" +
                        " res=${status.resolution}",
                    5_000L
                )

                if (status.isStreaming) {
                    wasStreaming = true
                } else if (wasStreaming && sessionActive.get()) {
                    wasStreaming = false
                    FileLogger.w(TAG, "检测到直播中断 (isStreaming 变为 false)，调度重试")
                    scheduleRetry("status: streaming dropped")
                }
            }
        }

        override fun onError(error: IDJIError?) {
            val desc = error?.description() ?: "unknown"
            mainHandler.post {
                FileLogger.w(TAG, "直播错误回调: $desc streaming=${liveStreamManager.isStreaming}")
                if (sessionActive.get() && !liveStreamManager.isStreaming) {
                    scheduleRetry("onError: $desc")
                }
            }
        }
    }

    fun startStreaming() {
        if (sessionActive.getAndSet(true)) {
            FileLogger.w(TAG, "已在推流会话中，忽略重复 start")
            return
        }
        retryCount = 0
        wasStreaming = false
        cancelRetry()
        doStart(isRetry = false)
    }

    fun stopStreaming() {
        sessionActive.set(false)
        wasStreaming = false
        cancelRetry()

        liveStreamManager.removeLiveStreamStatusListener(statusListener)

        if (liveStreamManager.isStreaming) {
            liveStreamManager.stopStream(object : CommonCallbacks.CompletionCallback {
                override fun onSuccess() {
                    FileLogger.i(TAG, "推流已停止")
                }

                override fun onFailure(error: IDJIError) {
                    FileLogger.w(TAG, "停止推流失败: ${error.description()}")
                }
            })
        }
        FileLogger.i(TAG, "FpvRtmpStreamer session 结束")
    }

    fun isStreaming(): Boolean = liveStreamManager.isStreaming

    private fun attachLiveStreamStatusListener() {
        liveStreamManager.removeLiveStreamStatusListener(statusListener)
        liveStreamManager.addLiveStreamStatusListener(statusListener)
    }

    private fun doStart(isRetry: Boolean = false) {
        if (liveStreamManager.isStreaming) {
            FileLogger.w(
                TAG,
                "SDK 报告已在推流中，跳过重复启动 | isRetry=$isRetry"
            )
            retryCount = 0
            wasStreaming = true
            cancelRetry()
            attachLiveStreamStatusListener()
            return
        }

        val rtmpSettings = RtmpSettings.Builder()
            .setUrl(rtmpUrl)
            .build()
        val settings = LiveStreamSettings.Builder()
            .setLiveStreamType(LiveStreamType.RTMP)
            .setRtmpSettings(rtmpSettings)
            .build()

        liveStreamManager.setLiveStreamSettings(settings)
        liveStreamManager.setCameraIndex(cameraIndex)
        liveStreamManager.setLiveStreamQuality(quality)

        attachLiveStreamStatusListener()

        liveStreamManager.startStream(object : CommonCallbacks.CompletionCallback {
            override fun onSuccess() {
                retryCount = 0
                wasStreaming = true
                cancelRetry()
                FileLogger.i(TAG, "推流成功 url=$rtmpUrl camera=$cameraIndex quality=$quality")
            }

            override fun onFailure(error: IDJIError) {
                val code = error.errorCode()
                if (liveStreamManager.isStreaming) {
                    FileLogger.i(TAG, "startStream 返回失败但已在推流 (code=$code)，视为正常")
                    retryCount = 0
                    wasStreaming = true
                    cancelRetry()
                    attachLiveStreamStatusListener()
                    return
                }
                FileLogger.w(TAG, "推流启动失败: ${error.description()} code=$code")
                scheduleRetry("startStream: ${error.description()}")
            }
        })
    }

    private fun scheduleRetry(reason: String) {
        if (!sessionActive.get()) return
        val enqueue = Runnable {
            if (!sessionActive.get()) return@Runnable
            if (retryRunnable != null) {
                FileLogger.w(TAG, "重试任务已在队列，跳过重复 scheduleRetry reason=$reason")
                return@Runnable
            }
            cancelRetry()
            val exp = min(retryCount, RETRY_EXP_CAP)
            val delay = min(RETRY_BASE_MS * (1L shl exp), RETRY_MAX_DELAY_MS).coerceAtLeast(1_000L)
            retryCount = (retryCount + 1).coerceAtMost(RETRY_COUNT_CAP)
            FileLogger.w(TAG, "重试调度 attempt=$retryCount delayMs=$delay reason=$reason")
            val r = object : Runnable {
                override fun run() {
                    retryRunnable = null
                    if (!sessionActive.get()) return
                    FileLogger.i(TAG, "执行重试 url=$rtmpUrl t=${SystemClock.elapsedRealtime()}")
                    doStart(isRetry = true)
                }
            }
            retryRunnable = r
            mainHandler.postDelayed(r, delay)
        }
        if (Looper.myLooper() == Looper.getMainLooper()) {
            enqueue.run()
        } else {
            mainHandler.post(enqueue)
        }
    }

    private fun cancelRetry() {
        retryRunnable?.let { mainHandler.removeCallbacks(it) }
        retryRunnable = null
    }
}
