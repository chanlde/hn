package dji.sampleV5.aircraft

import android.util.Log
import android.widget.TextView
import androidx.activity.viewModels
import androidx.lifecycle.Observer
import androidx.recyclerview.widget.LinearLayoutManager
import androidx.recyclerview.widget.RecyclerView
import dji.sampleV5.aircraft.log.LogAdapter
import dji.sampleV5.aircraft.log.LogManager
import dji.sampleV5.aircraft.log.LogViewModel
import dji.sampleV5.aircraft.util.GeneralUtils.rtmpUrl
import dji.sampleV5.aircraft.video.streamer.FpvRtmpStreamer
import dji.sdk.keyvalue.key.KeyTools
import dji.v5.common.callback.CommonCallbacks
import dji.v5.common.error.IDJIError
import dji.v5.common.utils.GeoidManager
import dji.v5.manager.KeyManager
import dji.v5.ux.core.communication.DefaultGlobalPreferences
import dji.v5.ux.core.communication.GlobalPreferencesManager
import dji.v5.ux.core.util.UxSharedPreferencesUtil
import dji.sdk.keyvalue.key.FlightControllerKey

/**
 * Class Description
 *
 * @author Hoker
 * @date 2022/2/14
 *
 * Copyright (c) 2022, DJI All Rights Reserved.
 */
class DJIAircraftMainActivity : DJIMainActivity() {
    private val TAG = "DJIAircraftMainActivity"

    private var rtmpStreamer: FpvRtmpStreamer? = null

    //private var fcSn: String = "1581F6GKB24C400408TU"
    private var fcSn: String = ""

    private lateinit var logTextView: TextView
    private lateinit var recyclerView: RecyclerView
    private lateinit var logAdapter: LogAdapter
    private val logViewModel: LogViewModel by viewModels()

    override fun prepareUxActivity() {

        UxSharedPreferencesUtil.initialize(this)
        GlobalPreferencesManager.initialize(DefaultGlobalPreferences(this))
        GeoidManager.getInstance().init(this)

        startRtmpStreaming()

        // 初始化 RecyclerView 和适配器
        recyclerView = findViewById(R.id.logRecyclerView)
        logAdapter = LogAdapter(mutableListOf())  // 传入一个空的日志列表
        recyclerView.layoutManager = LinearLayoutManager(this)
        recyclerView.adapter = logAdapter

        // 观察 LiveData 中的日志数据，并更新 RecyclerView
        logViewModel.logData.observe(this, Observer { logs ->
            logAdapter.updateLogs(logs)  // 更新适配器中的日志
        })

        // 示例：模拟日志输出
        // 可以通过调用 LogManager.log() 来输出日志并自动更新 RecyclerView
        LogManager.log("MainActivity", "应用启动了")
        LogManager.log("MainActivity", "加载数据中...")
        LogManager.log("MainActivity", "数据加载完成")
    }

    // 更新 UI 中的日志
    private fun logToUI(message: String) {
        val currentText = logTextView.text.toString()
        logTextView.text = "$currentText\n$message"  // 在现有日志后追加新日志

        // 可选：自动滚动到底部
        logTextView.post {
            logTextView.scrollTo(0, logTextView.bottom)
        }
    }

    override fun getFcSn():String {
        val key = KeyTools.createKey(FlightControllerKey.KeySerialNumber)

        KeyManager.getInstance().getValue(key, object : CommonCallbacks.CompletionCallbackWithParam<String> {
            override fun onSuccess(data: String?) {
                fcSn = data!!
                fcSnTv.text = fcSn
            }

            override fun onFailure(error: IDJIError) {

            }
        })

        return fcSn
    }

    private fun startRtmpStreaming() {
        try {
            val rtmpUrl = rtmpUrl

            Log.d(TAG, "准备启动后台推流: $rtmpUrl")

            // 创建推流器
            rtmpStreamer = FpvRtmpStreamer.create(rtmpUrl)

            // 添加回调（可选，用于日志）
            rtmpStreamer?.addCallback(object : dji.sampleV5.aircraft.video.callback.StreamCallback {
                override fun onConnectionSuccess() {
                    Log.d(TAG, "推流连接成功")
                }

                override fun onConnectionFailed(reason: String) {
                    Log.e(TAG, "推流连接失败: $reason")
                }

                override fun onVideoConfigSet(width: Int, height: Int, mimeType: String) {
                    Log.d(TAG, "推流视频参数: ${width}x${height}, $mimeType")
                }
            })

            // 开始推流
            rtmpStreamer?.startStreaming()

            Log.d(TAG, "后台推流已启动")
        } catch (e: Exception) {
            Log.e(TAG, "启动推流失败: ${e.message}", e)
        }
    }

    /**
     * 停止 RTMP 推流
     */
    private fun stopRtmpStreaming() {
        rtmpStreamer?.stopStreaming()
        rtmpStreamer = null
        Log.d(TAG, "后台推流已停止")
    }
}