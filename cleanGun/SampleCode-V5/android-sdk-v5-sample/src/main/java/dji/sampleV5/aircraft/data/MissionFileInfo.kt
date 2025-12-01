package dji.sampleV5.aircraft.data

/**
 * 任务文件信息模型
 * 存储任务中照片和视频的文件信息
 */
data class MissionFileInfo(
    val fileName: String,
    val filePath: String,
    val fileType: MissionFileType,
    val fileSize: Long,
    val createTime: Long,
    val taskId: String
)

/**
 * 文件类型枚举
 */
enum class MissionFileType {
    PHOTO,    // 照片
    VIDEO     // 视频
}




