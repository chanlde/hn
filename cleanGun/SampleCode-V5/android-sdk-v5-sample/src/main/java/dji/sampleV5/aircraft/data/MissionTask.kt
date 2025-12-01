package dji.sampleV5.aircraft.data

/**
 * 任务数据模型
 * 存储任务的基本信息和状态
 */
data class MissionTask(
    val taskId: String,
    val startTime: Long,
    var endTime: Long? = null,
    val folderPath: String,
    var status: MissionTaskStatus = MissionTaskStatus.CREATED
)

/**
 * 任务状态枚举
 */
enum class MissionTaskStatus {
    CREATED,      // 已创建
    RUNNING,      // 运行中
    COMPLETED,    // 已完成
    UPLOADING,    // 上传中
    UPLOADED,     // 已上传
    FAILED        // 失败
}




