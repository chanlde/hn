package dji.sampleV5.aircraft.data

/**
 * 任务上传进度信息
 * 用于上报上传进度
 */
data class MissionUploadProgress(
    val taskId: String,
    val progress: Int,  // 0-100
    val uploadedBytes: Long,
    val totalBytes: Long,
    val status: String  // "uploading", "success", "failed"
)




