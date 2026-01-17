package com.dji.network

import android.content.Context
import android.util.Log
import java.io.File
import java.util.Properties

/**
 * 配置管理器 - 外部配置文件方案
 *
 * 特点：
 * 1. 配置文件在外部存储，可以直接用文件管理器修改
 * 2. 不需要重新安装 App
 * 3. 向后兼容，不改 GeneralUtils 代码
 */
object ConfigManager {
    private const val TAG = "ConfigManager"
    private const val CONFIG_FILE_NAME = "app_config.properties"

    private lateinit var configFile: File
    private val properties = Properties()
    private var isInitialized = false


    /**
     * 初始化配置管理器
     * 在 Application.onCreate() 中调用
     */
    fun init(context: Context) {
        // 配置文件路径：/storage/emulated/0/Android/data/com.your.app/files/app_config.properties
        configFile = File(context.getExternalFilesDir(null), CONFIG_FILE_NAME)

        // 如果配置文件不存在，创建默认配置
        if (!configFile.exists()) {
            Log.i(TAG, "配置文件不存在，创建默认配置")
            createDefaultConfig()
        } else {
            Log.i(TAG, "加载现有配置文件")
        }

        // 加载配置
        loadConfig()
        isInitialized = true

        Log.i(TAG, "配置管理器初始化完成")
        Log.i(TAG, "配置文件路径: ${configFile.absolutePath}")
        Log.i(TAG, "当前配置:")
        printCurrentConfig()
    }

    /**
     * 创建默认配置文件（使用 GeneralUtils 的默认值）
     */
    private fun createDefaultConfig() {
        val defaultConfig = """
# ========================================
# DJI 无人机应用配置文件
# ========================================
# 修改此文件后，重启 App 即可生效
# 文件路径: ${configFile.absolutePath}
# ========================================

# RTMP 推流地址
rtmp_url=rtmp://192.168.0.117:1935/live/stream

# MinIO 配置
minio_endpoint=http://192.168.1.201:18005
minio_access_key=minio
minio_secret_key=UK13@ukdq
bucket_name=cloud-bucket-dji

# 场站配置
station_code=123456

# MQTT 配置
serverHost=192.168.0.122
serverPort=1883
clientId=987652
username=emqx
password=UK13@ukdq

        """.trimIndent()

        try {
            configFile.writeText(defaultConfig)
            Log.i(TAG, "默认配置文件创建成功: ${configFile.absolutePath}")
        } catch (e: Exception) {
            Log.e(TAG, "创建配置文件失败", e)
        }
    }

    /**
     * 加载配置文件
     */
    private fun loadConfig() {
        try {
            configFile.inputStream().use {
                properties.load(it)
            }
            Log.i(TAG, "配置加载成功，共 ${properties.size} 项配置")
        } catch (e: Exception) {
            Log.e(TAG, "加载配置文件失败", e)
        }
    }

    /**
     * 获取配置项（带默认值）
     */
    private fun getString(key: String, default: String): String {
        if (!isInitialized) {
            Log.w(TAG, "配置管理器未初始化，返回默认值")
            return default
        }
        return properties.getProperty(key, default).trim()
    }

    // ==================== 配置项访问器 ====================
    
    /**
     * 支持的设备 ID 列表（自动轮流匹配）
     * 应用会自动匹配列表中任何一个连接的设备
     */
    val fcDeviceIdList: List<String> = listOf(
        "1581F8DBW256500A2NKB",
        "1581F8DBW255D00A2LD4",
        "1581F6GKB24C400408TU",
        "1581F6GKB237F003001L",
        "1581F7K3325AV00AR049"
    )
    
    /**
     * 获取第一个设备 ID（向后兼容）
     * 注意：实际匹配会在 observeSDKManager 中使用列表匹配
     */
    val fcDeviceId: String
        get() = fcDeviceIdList.firstOrNull() ?: ""
    
    /**
     * 检查给定的设备 ID 是否在支持列表中
     */
    fun isSupportedDeviceId(deviceId: String): Boolean {
        return fcDeviceIdList.any { it.equals(deviceId, ignoreCase = true) }
    }
    
    /**
     * 获取匹配的设备 ID（如果存在）
     */
    fun getMatchedDeviceId(deviceId: String): String? {
        return fcDeviceIdList.firstOrNull { it.equals(deviceId, ignoreCase = true) }
    }

    val rtmpUrl: String
        get() = getString("rtmp_url", "rtmp://ukrd.synology.me:21935/live/test")

    val minioEndpoint: String
        get() = getString("minio_endpoint", "http://192.168.1.201:18005")

    val minioAccessKey: String
        get() = getString("minio_access_key", "minio")

    val minioSecretKey: String
        get() = getString("minio_secret_key", "UK13@ukdq")

    val bucketName: String
        get() = getString("bucket_name", "cloud-bucket-dji")

    val serverHost: String
        get() = getString("serverHost", "192.168.1.201")

    val serverPort: String
        get() = getString("serverPort", "1883")

    val clientId: String
        get() = getString("clientId", "987652")

    val username: String
        get() = getString("username", "emqx")

    val password: String
        get() = getString("password", "UK13@ukdq")

    val stationCode: String
        get() = getString("station_code", "")

    /**
     * 打印当前配置（用于调试）
     */
    fun printCurrentConfig() {
        Log.d(TAG, "----------------------------------------")
        Log.d(TAG, "rtmp_url        : $rtmpUrl")
        Log.d(TAG, "minio_endpoint  : $minioEndpoint")
        Log.d(TAG, "minio_access_key: $minioAccessKey")
        Log.d(TAG, "minio_secret_key: ${minioSecretKey.take(4)}****")
        Log.d(TAG, "bucket_name     : $bucketName")
        Log.d(TAG, "serverHost      : $serverHost")
        Log.d(TAG, "serverPort      : $serverPort")
        Log.d(TAG, "clientId        : $clientId")
        Log.d(TAG, "username        : $username")
        Log.d(TAG, "password        : ${password.take(4)}****")
        Log.d(TAG, "station_code    : $stationCode")
        Log.d(TAG, "支持的设备ID列表: ${fcDeviceIdList.joinToString(", ")}")
        Log.d(TAG, "----------------------------------------")
    }
}
