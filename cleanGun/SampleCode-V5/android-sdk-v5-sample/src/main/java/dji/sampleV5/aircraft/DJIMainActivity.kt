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
import android.widget.Toast
import com.amap.api.maps.MapsInitializer
import com.amap.api.services.core.ServiceSettings
import com.dji.network.MQTTConfig
import com.tji.network.MqttManager
import dji.sampleV5.aircraft.mqtthandle.CameraService
import dji.sampleV5.aircraft.mqtthandle.FlightControlService
import dji.sampleV5.aircraft.mqtthandle.FlightDataReport
import dji.sampleV5.aircraft.mqtthandle.LandingConfirmationListener
import dji.sampleV5.aircraft.mqtthandle.MissionControlService
import dji.sampleV5.aircraft.mqtthandle.MqttMessageHandler
import dji.sampleV5.aircraft.mqtthandle.TaskService
import dji.sampleV5.aircraft.util.GeneralUtils.fcDeviceId

import dji.sdk.keyvalue.key.KeyTools
import dji.sdk.keyvalue.key.ProductKey

import dji.v5.common.utils.RxUtil.getValue
import dji.v5.ux.sample.showcase.defaultlayout.DefaultLayoutActivity


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

    // 设备ID（遥控器序列号）
    //val deviceId = "1581F6GKB24C400408TU"

    //var deviceId = "123456"  // TODO: 从配置或SDK动态获取

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
        cameraService = cameraService
    )

    abstract fun prepareUxActivity()
    abstract fun getFcSn():String

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)


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

        observeSDKManager()
        checkPermissionAndRequest()
        updateSearchPrivacyCompliance()
        updateMapPrivacyCompliance()
        checkAndRequestAllFilesAccess()
        mqttManager = MqttManager.getInstance()
        fcSnTv = findViewById(R.id.fc_sn_tv)
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
                    val sn = getFcSn()
                    Log.d("MainActivity", "sn: $sn $fcDeviceId")

                    prepareUxActivity()
                    landingConfirmationListener = LandingConfirmationListener()
                    cameraService.initialize()

                    mqttManager.connect (
                        onConnected = {
                            messageHandler.setupSubscriptions(mqttManager, fcDeviceId)
                            // 创建飞行数据上报服务
                            flightDataReport = FlightDataReport(fcDeviceId)
                        },

                        onFailed = { throwable ->
                            Log.d("MqttManager", "Failed to connect: ${throwable.message}")
                        }
                    )


                    //startActivity(Intent(this, DefaultLayoutActivity::class.java))

                }, 1000)

            } else {
                showToast("Register Failure: ${resultPair.second}")
            }
        }
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
        super.onDestroy()
        handler.removeCallbacksAndMessages(null)

        // 销毁降落确认监听器
        landingConfirmationListener.destroy()

        // 销毁 CameraService 资源
        cameraService.destroy()

        // 销毁飞行数据上报服务
        flightDataReport?.destroy()

        Log.d(TAG, "所有资源已清理")
    }
}