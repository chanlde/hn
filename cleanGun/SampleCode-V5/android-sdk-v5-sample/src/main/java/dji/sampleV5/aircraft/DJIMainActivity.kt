package dji.sampleV5.aircraft

import android.Manifest

import android.content.Intent
import android.net.Uri

import android.os.Build
import android.os.Bundle
import android.os.Environment
import android.os.Handler
import android.os.Looper
import android.provider.Settings
import android.util.Log
import android.view.View
import androidx.activity.result.contract.ActivityResultContracts
import androidx.appcompat.app.AppCompatActivity
import dji.sampleV5.aircraft.databinding.ActivityMainBinding
import dji.sampleV5.aircraft.models.MSDKManagerVM
import dji.sampleV5.aircraft.models.globalViewModels
import dji.sampleV5.aircraft.util.ToastUtils
import dji.v5.utils.common.PermissionUtil

import android.widget.TextView
import dji.sampleV5.aircraft.BuildConfig
import android.widget.Toast
import androidx.lifecycle.lifecycleScope
import com.amap.api.maps.MapsInitializer
import com.amap.api.services.core.ServiceSettings
import com.tji.network.MqttManager
import dji.sampleV5.aircraft.mqtthandle.CameraService
import dji.sampleV5.aircraft.mqtthandle.FlightControlService
import dji.sampleV5.aircraft.mqtthandle.FlightDataReport
import dji.sampleV5.aircraft.mqtthandle.LandingConfirmationListener
import dji.sampleV5.aircraft.mqtthandle.MissionControlService
import dji.sampleV5.aircraft.mqtthandle.MqttMessageHandler
import dji.sampleV5.aircraft.mqtthandle.TaskService
import com.dji.network.ConfigManager
import com.dji.network.ConfigManager.fcDeviceId
import com.dji.util.FileLogger
import dji.sampleV5.aircraft.manager.LocationService
import dji.sampleV5.aircraft.util.LocationHelper
import dji.v5.ux.sample.showcase.defaultlayout.DefaultLayoutActivity
import kotlinx.coroutines.launch


/**
 * Class Description
 *
 * @author Hoker
 * @date 2022/2/10
 *
 * Copyright (c) 2022, DJI All Rights Reserved.
 */
abstract class DJIMainActivity : AppCompatActivity() {
    private val handler: Handler = Handler(Looper.getMainLooper())
    private val TAG = "MainActivity"

    lateinit var fcSnTv: TextView
    protected var locationHelper: LocationHelper? = null  // 从 Application 获取，可能为空


    protected var isGetLocation = false

    private val permissionArray = arrayListOf(
        Manifest.permission.RECORD_AUDIO,
        Manifest.permission.KILL_BACKGROUND_PROCESSES,
        Manifest.permission.ACCESS_COARSE_LOCATION,
        Manifest.permission.ACCESS_FINE_LOCATION,
    )

    init {
        permissionArray.apply {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
//                add(Manifest.permission.READ_MEDIA_IMAGES)
//                add(Manifest.permission.READ_MEDIA_VIDEO)
//                add(Manifest.permission.READ_MEDIA_AUDIO)
            } else {
                add(Manifest.permission.READ_EXTERNAL_STORAGE)
                add(Manifest.permission.WRITE_EXTERNAL_STORAGE)
            }
        }
    }

    private val msdkManagerVM: MSDKManagerVM by globalViewModels()
    private lateinit var binding: ActivityMainBinding

    private lateinit var landingConfirmationListener: LandingConfirmationListener
    // 创建所有服务实例
    private lateinit var mqttManager: MqttManager

    val taskService = TaskService(this)
    val flightControlService = FlightControlService()
    val missionControlService = MissionControlService()

    val cameraService = CameraService(this, fcDeviceId)

    // 飞行数据上报服务（延迟初始化，在MQTT连接成功后创建）
    private var flightDataReport: FlightDataReport? = null

    // 创建消息处理器
    val messageHandler = MqttMessageHandler(
        taskService = taskService,
        flightControlService = flightControlService,
        missionControlService = missionControlService,
        cameraService = cameraService,
        context = this
    )

    abstract fun prepareUxActivity()
    abstract fun getFcSn():String

    abstract fun aircraftMainActivityInit()

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        
        // 日志系统已在 Application 中初始化，这里直接使用即可
        FileLogger.i(TAG, "DJIMainActivity.onCreate")

        ConfigManager.init(this)

        binding = ActivityMainBinding.inflate(layoutInflater)
        setContentView(binding.root)

        // 需要校验这种情况，业界标准做法，基本所有app都需要这个
        if (!isTaskRoot && intent.hasCategory(Intent.CATEGORY_LAUNCHER) && Intent.ACTION_MAIN == intent.action) {
            finish()
            return
        }

        window.decorView.apply {
            systemUiVisibility =
                View.SYSTEM_UI_FLAG_HIDE_NAVIGATION or View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY or View.SYSTEM_UI_FLAG_FULLSCREEN or View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
        }
        aircraftMainActivityInit()
        observeSDKManager()
        checkPermissionAndRequest()
        updateSearchPrivacyCompliance()
        updateMapPrivacyCompliance()
        checkAndRequestAllFilesAccess()

        mqttManager = MqttManager.getInstance()

        fcSnTv = findViewById(R.id.fc_sn_tv)

        findViewById<TextView>(R.id.app_version_tv).text = BuildConfig.VERSION_NAME
    }


    // ✅ 检查并请求所有文件访问权限（Android 11+）
    private fun checkAndRequestAllFilesAccess() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            if (!Environment.isExternalStorageManager()) {
                Log.w(TAG, "⚠️ 没有所有文件访问权限，正在请求...")

                try {
                    val intent = Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION).apply {
                        data = Uri.parse("package:$packageName")
                    }
                    startActivity(intent)  // ← 必须要这一行！
                } catch (e: Exception) {
                    Log.e(TAG, "无法打开权限设置页面", e)
                    // 备用方案：打开通用设置页面
                    val intent = Intent(Settings.ACTION_MANAGE_ALL_FILES_ACCESS_PERMISSION)
                    startActivity(intent)  // ← 这里也要加！
                }
            } else {
                Log.i(TAG, "✅ 已有所有文件访问权限")
            }
        } else {
            Log.d(TAG, "Android 10 及以下，不需要 MANAGE_EXTERNAL_STORAGE")
        }
    }
    private fun updateSearchPrivacyCompliance() {
        try { // 显示隐私政策
            ServiceSettings.updatePrivacyShow(this, true, true)  // 参数：context, isContains, isShow

            // 更新用户同意隐私政策
            ServiceSettings.updatePrivacyAgree(this, true)  // 参数：context, isAgree
        } catch (e: Exception) {
            Toast.makeText(this, "搜索合规设置失败", Toast.LENGTH_SHORT).show()
        }
    }

    // 更新地图隐私合规
    private fun updateMapPrivacyCompliance() {
        try {
            // 显示隐私政策
            MapsInitializer.updatePrivacyShow(this, true, true)  // 参数：context, isContains, isShow

            // 更新用户同意隐私政策
            MapsInitializer.updatePrivacyAgree(this, true)  // 参数：context, isAgree
        } catch (e: Exception) {
            Toast.makeText(this, "地图合规设置失败", Toast.LENGTH_SHORT).show()
        }
    }

    private fun observeSDKManager() {
        msdkManagerVM.lvRegisterState.observe(this) { resultPair ->

            if (resultPair.first) {
                handler.postDelayed({
                    // 等待获取飞机SN
                    waitForFcSn { deviceId ->
                        if (deviceId.isNullOrBlank()) {
                            FileLogger.e(TAG, "无法获取飞机SN，停止执行", null)
                            showToast("无法获取飞机SN，请检查设备连接")
                            return@waitForFcSn
                        }

                        FileLogger.i(TAG, "成功获取飞机SN: $deviceId")

                        prepareUxActivity()
                        landingConfirmationListener = LandingConfirmationListener()
                        cameraService.initialize()

                        mqttManager.connect (
                            onConnected = {
                                messageHandler.setupSubscriptions(mqttManager, deviceId)
                                flightDataReport = FlightDataReport(deviceId, cameraService)
                                FileLogger.i(TAG, "MQTT 已连接并完成订阅与 FlightDataReport 初始化 deviceId=$deviceId")
                            },

                            onFailed = { throwable ->
                                FileLogger.e(TAG, "MQTT 连接失败: ${throwable.message}", throwable)
                            }
                        )
                        startActivity(Intent(this, DefaultLayoutActivity::class.java))
                        FileLogger.i(TAG, "已启动 DefaultLayoutActivity")
                    }
                }, 1000)

            } else {
                showToast("Register Failure: ${resultPair.second}")
            }
        }
    }

    /**
     * 等待获取飞机SN，直到获取成功或超时
     * @param maxRetries 最大重试次数（默认30次）
     * @param retryInterval 重试间隔（毫秒，默认200ms）
     * @param onSuccess 获取成功回调，参数为SN
     */
    private fun waitForFcSn(
        maxRetries: Int = 30,
        retryInterval: Long = 200,
        onSuccess: (String) -> Unit
    ) {
        var retryCount = 0

        fun checkFcSn() {
            val deviceId = getFcSn()
//            deviceId = "cccc";

            if (deviceId.isNotBlank()) {
                // 获取到SN，直接继续执行
                FileLogger.i(TAG, "成功获取飞机SN: $deviceId")
                onSuccess(deviceId)
            } else {
                retryCount++
                if (retryCount < maxRetries) {
                    FileLogger.throttledD(TAG, "waitFcSn", "等待飞机SN 重试 $retryCount/$maxRetries", 3_000L)
                    handler.postDelayed({ checkFcSn() }, retryInterval)
                } else {
                    FileLogger.e(TAG, "获取飞机SN超时，已重试 $maxRetries 次", null)
                    showToast("获取飞机SN超时，请检查设备连接")
                    onSuccess("") // 返回空字符串表示失败
                }
            }
        }

        // 开始检查
        checkFcSn()
    }

    fun <T> enableDefaultLayout(cl: Class<T>) {
        enableShowCaseButton(binding.defaultLayoutButton, cl)
    }
    private fun <T> enableShowCaseButton(view: View, cl: Class<T>) {
        view.setOnClickListener {
            startActivity(Intent(this, cl))
        }
    }
    private fun showToast(content: String) {
        ToastUtils.showToast(content)
    }
    private fun checkPermissionAndRequest() {
        if (!checkPermission()) {
            requestPermission()
        }
    }

    private fun checkPermission(): Boolean {
        for (i in permissionArray.indices) {
            if (!PermissionUtil.isPermissionGranted(this, permissionArray[i])) {
                return false
            }
        }
        return true
    }

    private val requestPermissionLauncher = registerForActivityResult(
        ActivityResultContracts.RequestMultiplePermissions()
    ) { result ->
        result?.entries?.forEach {
            if (!it.value) {
                requestPermission()
                return@forEach
            }
        }
    }

    private fun requestPermission() {
        requestPermissionLauncher.launch(permissionArray.toArray(arrayOf()))
    }

    override fun onDestroy() {
        FileLogger.i(TAG, "DJIMainActivity.onDestroy 开始清理")
        
        try {
            handler.removeCallbacksAndMessages(null)

            if (::landingConfirmationListener.isInitialized) {
                landingConfirmationListener.destroy()
            } else {
                FileLogger.w(TAG, "LandingConfirmationListener 未初始化，跳过销毁")
            }

            cameraService.destroy()
            flightDataReport?.destroy()
            FileLogger.i(TAG, "DJIMainActivity 资源清理完成")
        } catch (e: Exception) {
            FileLogger.e(TAG, "onDestroy 清理资源时发生异常", e)
        }
        
        super.onDestroy()
    }
}