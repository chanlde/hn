package dji.sampleV5.aircraft.mqtthandle

import android.content.Context
import android.util.Log
import java.io.File
import java.text.SimpleDateFormat
import java.util.*

/**
 * 任务文件夹管理器
 * 职责：为每个任务创建和管理独立的文件夹结构
 */
class MissionFolderManager(private val context: Context) {
    
    companion object {
        private const val TAG = "MissionFolderManager"
        private const val FOLDER_PHOTOS = "照片"
        private const val FOLDER_VIDEOS = "视频"
    }
    
    /**
     * 创建任务文件夹
     * @return 任务文件夹路径，失败返回null
     */
    fun createMissionFolder(): String? {
        return try {
            // 获取遥控器存储目录（外部存储）
            val baseDir = "/storage/emulated/0/Camera"
            
            // 创建任务文件夹，命名格式：mission_yyyyMMdd_HHmmss
            val dateFormat = SimpleDateFormat("yyyyMMdd_HHmmss", Locale.getDefault())
            val folderName = "mission_${dateFormat.format(Date())}"
            val missionFolder = File(baseDir, folderName)
            
            if (!missionFolder.exists()) {
                missionFolder.mkdirs()
            }
            
            // 创建子文件夹：照片和视频
            val photosFolder = File(missionFolder, FOLDER_PHOTOS)
            val videosFolder = File(missionFolder, FOLDER_VIDEOS)
            
            photosFolder.mkdirs()
            videosFolder.mkdirs()
            
            Log.d(TAG, "任务文件夹创建成功: ${missionFolder.absolutePath}")
            missionFolder.absolutePath
        } catch (e: Exception) {
            Log.e(TAG, "创建任务文件夹失败: ${e.message}", e)
            null
        }
    }
    
    /**
     * 获取当前任务的照片文件夹路径
     */
    fun getPhotosFolder(missionFolderPath: String): String {
        return File(missionFolderPath, FOLDER_PHOTOS).absolutePath
    }
    
    /**
     * 获取当前任务的视频文件夹路径
     */
    fun getVideosFolder(missionFolderPath: String): String {
        return File(missionFolderPath, FOLDER_VIDEOS).absolutePath
    }
    
    /**
     * 检查任务文件夹是否存在
     */
    fun isMissionFolderExists(missionFolderPath: String): Boolean {
        return File(missionFolderPath).exists()
    }
}

