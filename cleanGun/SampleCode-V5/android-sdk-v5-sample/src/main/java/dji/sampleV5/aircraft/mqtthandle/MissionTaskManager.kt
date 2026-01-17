package dji.sampleV5.aircraft.mqtthandle

import android.content.Context
import android.util.Log
import dji.sampleV5.aircraft.data.MissionTask
import dji.sampleV5.aircraft.data.MissionTaskStatus

/**
 * 任务生命周期管理器
 * 职责：监听起飞/降落事件，管理任务的开始和结束
 */
class MissionTaskManager(
    private val context: Context,
    private val folderManager: MissionFolderManager,
    private val cameraService: CameraService,
    private val fileUploader: MissionFileUploader
) {
    
    companion object {
        private const val TAG = "MissionTaskManager"
    }
    
    private var currentTask: MissionTask? = null
    
    /**
     * 开始任务（在起飞时调用）
     */
    fun startMission() {
        if (currentTask != null) {
            Log.w(TAG, "任务已存在，忽略重复开始")
            return
        }
        
        // 创建任务文件夹
        val folderPath = folderManager.createMissionFolder() ?: run {
            Log.e(TAG, "创建任务文件夹失败")
            return
        }
        
        // 创建任务实例
        val taskId = generateTaskId()
        currentTask = MissionTask(
            taskId = taskId,
            startTime = System.currentTimeMillis(),
            folderPath = folderPath,
            status = MissionTaskStatus.RUNNING
        )
        
        // 设置相机服务的任务文件夹路径和任务ID
        cameraService.setMissionFolderPath(folderPath, taskId)
        
        Log.d(TAG, "任务已开始: $taskId, 文件夹: $folderPath")
    }
    
    /**
     * 结束任务（在降落时调用）
     */
    fun endMission() {
        val task = currentTask ?: run {
            Log.w(TAG, "没有正在运行的任务")
            return
        }
        
        // 更新任务状态
        currentTask = task.copy(
            endTime = System.currentTimeMillis(),
            status = MissionTaskStatus.COMPLETED
        )
        
        // 清除相机服务的任务文件夹路径
        cameraService.clearMissionFolderPath()
        
        Log.d(TAG, "任务已结束: ${task.taskId}")
        
        // 触发文件上传
        uploadMissionFiles(task)
    }
    
    /**
     * 上传任务文件
     */
    private fun uploadMissionFiles(task: MissionTask) {
        currentTask = task.copy(status = MissionTaskStatus.UPLOADING)
        
        fileUploader.uploadMissionFolder(
            taskId = task.taskId,
            folderPath = task.folderPath,
            onSuccess = {
                currentTask = task.copy(status = MissionTaskStatus.UPLOADED)
                Log.d(TAG, "任务文件上传成功: ${task.taskId}")
            },
            onFailure = { error ->
                currentTask = task.copy(status = MissionTaskStatus.FAILED)
                Log.e(TAG, "任务文件上传失败: ${task.taskId}, 错误: $error")
            }
        )
    }
    
    /**
     * 生成任务ID
     */
    private fun generateTaskId(): String {
        return "task_${System.currentTimeMillis()}"
    }
    
    /**
     * 获取当前任务
     */
    fun getCurrentTask(): MissionTask? = currentTask
    
    /**
     * 检查是否有正在运行的任务
     */
    fun hasRunningTask(): Boolean {
        return currentTask?.status == MissionTaskStatus.RUNNING
    }
}

