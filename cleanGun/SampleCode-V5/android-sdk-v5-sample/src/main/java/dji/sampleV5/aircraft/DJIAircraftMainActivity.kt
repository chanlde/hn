package dji.sampleV5.aircraft

import android.location.LocationManager
import android.util.Log
import androidx.lifecycle.lifecycleScope
import com.dji.network.GeneralUtils.rtmpUrl
import dji.sampleV5.aircraft.manager.LocationService
import dji.sampleV5.aircraft.util.LocationHelper
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
import dji.v5.ux.sample.showcase.defaultlayout.DefaultLayoutActivity
import kotlinx.coroutines.launch

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
    private var fcSn: String = ""
    private lateinit var locationManager: LocationManager
    private lateinit var locationService: LocationService

    override fun aircraftMainActivityInit() {
        // 从 Application 获取定位服务（不受 Activity 生命周期影响）
        val app = application as? DJIApplication
        locationHelper = app?.getLocationHelper()
        locationService = app?.getLocationService() ?: run {
            Log.e(TAG, "无法获取 LocationService，定位服务可能未初始化")
            throw IllegalStateException("LocationService 未初始化")
        }
        
        locationManager = getSystemService(LocationManager::class.java)

        initManagers()
        
        Log.d(TAG, "定位服务已从 Application 获取，不受 Activity 生命周期影响")
    }

    override fun prepareUxActivity() {

        UxSharedPreferencesUtil.initialize(this)
        GlobalPreferencesManager.initialize(DefaultGlobalPreferences(this))
        GeoidManager.getInstance().init(this)
        enableDefaultLayout(DefaultLayoutActivity::class.java)
        startRtmpStreaming()
        
        // 启动定位服务（Application 级别，不受 Activity 生命周期影响）
        startLocationService()
    }
    
    /**
     * 启动定位服务
     */
    private fun startLocationService() {
        try {
            locationService.startLocation { location ->
                Log.d(TAG, "位置更新: lat=${location.latitude}, lon=${location.longitude}")
                // 可以在这里处理位置更新，比如更新UI或发送到服务器
            }
            Log.d(TAG, "定位服务启动请求已发送")
        } catch (e: Exception) {
            Log.e(TAG, "启动定位服务失败: ${e.message}", e)
        }
    }

    private fun initManagers() {
        // LocationService 已在 Application 中初始化，这里不需要再次初始化
        Log.d(TAG, "LocationService 已从 Application 获取")
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