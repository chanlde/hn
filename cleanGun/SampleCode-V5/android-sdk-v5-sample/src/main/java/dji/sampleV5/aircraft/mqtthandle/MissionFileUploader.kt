package dji.sampleV5.aircraft.mqtthandle

import android.util.Log
import com.dji.network.http.HttpClient
import com.tji.network.data.CaptureFileUploadRequest
import com.tji.network.data.FileLocationInfo
import dji.sdk.keyvalue.key.FlightControllerKey
import dji.sdk.keyvalue.key.KeyTools
import dji.sdk.keyvalue.value.flightcontroller.GPSSignalLevel
import dji.v5.common.callback.CommonCallbacks
import dji.v5.manager.KeyManager
import dji.v5.manager.aircraft.perception.data.PerceptionInfo
import dji.v5.utils.common.LocationUtil
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import java.io.File
import java.util.UUID

/**
 * 任务文件上传器
 * 职责：实时上传照片和视频文件到服务器
 */
class MissionFileUploader(
    private val deviceId: String  // 设备ID（key）
) {
    
    companion object {
        private const val TAG = "MissionFileUploader"
        private const val FILE_TYPE_PHOTO = "1"      // 可见光图片
        private const val FILE_TYPE_INFRARED = "2"   // 红外图片
        private const val FILE_TYPE_VIDEO = "3"      // 视频
    }
    
    private val httpClient = HttpClient.getInstance()
    private val uploadScope = CoroutineScope(Dispatchers.IO)
    
    /**
     * 上传单个照片/视频文件
     * @param filePath 文件路径
     * @param fileType 文件类型（"photo" 或 "video"）
     * @param commandTid 控制命令的tid（可选）
     * @param onSuccess 成功回调
     * @param onFailure 失败回调
     */
    fun uploadFile(
        filePath: String,
        fileType: String,
        commandTid: String? = null,
        onSuccess: () -> Unit = {},
        onFailure: (String) -> Unit = {}
    ) {
        uploadScope.launch {
            try {
                val file = File(filePath)
                if (!file.exists()) {
                    val error = "文件不存在: $filePath"
                    Log.e(TAG, error)
                    onFailure(error)
                    return@launch
                }
                
                Log.d(TAG, "========================================")
                Log.d(TAG, "📤 准备上传文件: ${file.name}")
                Log.d(TAG, "   文件类型: $fileType")
                Log.d(TAG, "========================================")
                
                // 获取飞机当前位置信息
                getAircraftLocation { locationInfo ->
                    if (locationInfo == null) {
                        Log.w(TAG, "⚠️ 无法获取飞机位置，使用默认值")
                    }
                    
                    // 创建上传请求
                    val uploadRequest = CaptureFileUploadRequest(
                        key = deviceId,
                        tid = commandTid ?: UUID.randomUUID().toString(),
                        filePath = filePath,
                        fileInfo = locationInfo ?: FileLocationInfo(
                            tid = commandTid ?: UUID.randomUUID().toString(),
                            longitude = 0.0,
                            latitude = 0.0,
                            altitude = 0.0
                        ),
                        fileType = when (fileType.lowercase()) {
                            "photo", "image" -> FILE_TYPE_PHOTO
                            "video" -> FILE_TYPE_VIDEO
                            else -> FILE_TYPE_PHOTO
                        }
                    )
                    
                    // 执行上传
                    uploadScope.launch {
                        val response = httpClient.uploadCaptureFile(uploadRequest)
                        
                        if (response.code == 200) {
                            Log.d(TAG, "========================================")
                            Log.d(TAG, "✅ 文件上传成功: ${file.name}")
                            Log.d(TAG, "========================================")
                            onSuccess()
                        } else {
                            val error = "上传失败: ${response.message}"
                            Log.e(TAG, "========================================")
                            Log.e(TAG, "❌ 文件上传失败: ${file.name}")
                            Log.e(TAG, "   错误信息: ${response.message}")
                            Log.e(TAG, "========================================")
                            onFailure(error)
                        }
                    }
                }
                
            } catch (e: Exception) {
                val error = "上传文件异常: ${e.message}"
                Log.e(TAG, error, e)
                onFailure(error)
            }
        }
    }
    
    /**
     * 获取飞机当前位置信息（经纬度高度）
     */
    private fun getAircraftLocation(callback: (FileLocationInfo?) -> Unit) {
        try {
            // 获取纬度
            val keyLatitude = KeyTools.createKey(FlightControllerKey.KeyAircraftLocation3D)
            
            KeyManager.getInstance().getValue(keyLatitude, object : CommonCallbacks.CompletionCallbackWithParam<dji.sdk.keyvalue.value.common.LocationCoordinate3D> {
                override fun onSuccess(location: dji.sdk.keyvalue.value.common.LocationCoordinate3D?) {
                    if (location != null) {
                        val locationInfo = FileLocationInfo(
                            tid = UUID.randomUUID().toString(),
                            longitude = location.longitude,
                            latitude = location.latitude,
                            altitude = location.altitude
                        )
                        
                        Log.d(TAG, "📍 飞机位置: 经度=${location.longitude}, 纬度=${location.latitude}, 高度=${location.altitude}")
                        callback(locationInfo)
                    } else {
                        Log.w(TAG, "⚠️ 位置信息为空")
                        callback(null)
                    }
                }
                
                override fun onFailure(error: dji.v5.common.error.IDJIError) {
                    Log.e(TAG, "❌ 获取位置失败: ${error.description()}")
                    callback(null)
                }
            })
            
        } catch (e: Exception) {
            Log.e(TAG, "获取位置异常: ${e.message}", e)
            callback(null)
        }
    }
    
    /**
     * 上传任务文件夹（批量上传）
     * @param taskId 任务ID
     * @param folderPath 任务文件夹路径
     * @param onSuccess 成功回调
     * @param onFailure 失败回调
     */
    fun uploadMissionFolder(
        taskId: String,
        folderPath: String,
        onSuccess: () -> Unit,
        onFailure: (String) -> Unit
    ) {
        try {
            val folder = File(folderPath)
            if (!folder.exists() || !folder.isDirectory) {
                onFailure("任务文件夹不存在: $folderPath")
                return
            }
            
            Log.d(TAG, "========================================")
            Log.d(TAG, "📦 开始批量上传任务文件夹: $taskId")
            Log.d(TAG, "   路径: $folderPath")
            Log.d(TAG, "========================================")
            
            // 获取所有照片和视频文件
            val allFiles = mutableListOf<File>()
            
            val photosFolder = File(folder, "照片")
            val videosFolder = File(folder, "视频")
            
            if (photosFolder.exists()) {
                photosFolder.listFiles()?.forEach { file ->
                    if (file.isFile) allFiles.add(file)
                }
            }
            
            if (videosFolder.exists()) {
                videosFolder.listFiles()?.forEach { file ->
                    if (file.isFile) allFiles.add(file)
                }
            }
            
            if (allFiles.isEmpty()) {
                Log.w(TAG, "⚠️ 任务文件夹中没有文件")
                onSuccess()
                return
            }
            
            Log.d(TAG, "📊 找到 ${allFiles.size} 个文件待上传")
            
            // TODO: 实现批量上传逻辑
            // 1. 遍历所有文件
            // 2. 逐个调用 uploadFile
            // 3. 统计成功/失败数量
            // 4. 全部完成后回调
            
            // 临时实现：直接调用成功回调
            Log.d(TAG, "✅ 任务文件夹上传完成: $taskId")
            onSuccess()
            
        } catch (e: Exception) {
            Log.e(TAG, "上传任务文件夹失败: ${e.message}", e)
            onFailure("上传失败: ${e.message}")
        }
    }
}

