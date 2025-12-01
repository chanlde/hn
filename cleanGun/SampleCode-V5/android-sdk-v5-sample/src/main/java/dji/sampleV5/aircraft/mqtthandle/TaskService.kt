package dji.sampleV5.aircraft.mqtthandle

import android.content.Context
import android.util.Log
import dji.v5.common.callback.CommonCallbacks
import dji.v5.common.error.IDJIError
import dji.v5.manager.aircraft.waypoint3.WaypointMissionManager
import dji.v5.utils.common.FileUtils
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch

/**
 * 航线任务服务
 * 职责：处理航线任务的下载、上传、启动
 */
class TaskService(private val context: Context) {
    companion object {
        private const val TAG = "TaskService"
        private const val WAYPOINT_FILE_TAG = ".kmz"
    }

    /**
     * 上传航线任务
     */
    fun uploadWaypointMission(
        request: TaskFileRequest,
        onProgress: (Double) -> Unit,
        onSuccess: (String) -> Unit,
        onFailure: (String) -> Unit
    ) {
        // 使用协程异步下载文件
        CoroutineScope(Dispatchers.IO).launch {
            try {
                // 下载 KMZ 文件
                val localFile = FileDownloader.downloadKMZFile(
                    fileUrl = request.fileUrl,
                    fileName = request.key,
                    context = context
                )

                if (localFile == null) {
                    onFailure("下载 KMZ 文件失败")
                    return@launch
                }

                // 推送到飞机
                pushKMZFileToAircraft(
                    missionPath = localFile.absolutePath,
                    onProgress = onProgress,
                    onSuccess = onSuccess,
                    onFailure = onFailure
                )

            } catch (e: Exception) {
                Log.e(TAG, "上传航线任务失败: ${e.message}", e)
                onFailure("上传失败: ${e.message}")
            }
        }
    }

    /**
     * 推送 KMZ 文件到飞机
     */
    private fun pushKMZFileToAircraft(
        missionPath: String,
        onProgress: (Double) -> Unit,
        onSuccess: (String) -> Unit,
        onFailure: (String) -> Unit
    ) {
        WaypointMissionManager.getInstance().pushKMZFileToAircraft(
            missionPath,
            object : CommonCallbacks.CompletionCallbackWithProgress<Double> {
                override fun onProgressUpdate(progress: Double) {
                    onProgress(progress)
                    Log.d(TAG, "航线上传进度: $progress")
                }

                override fun onSuccess() {
                    Log.d(TAG, "航线上传成功")

                    // 自动启动任务
                    startMission(
                        missionPath = missionPath,
                        onSuccess = onSuccess,
                        onFailure = onFailure
                    )
                }

                override fun onFailure(error: IDJIError) {
                    Log.e(TAG, "航线上传失败: ${error.description()}")
                    onFailure("航线上传失败: ${error.description()}")
                }
            }
        )
    }

    /**
     * 启动航线任务
     */
    private fun startMission(
        missionPath: String,
        onSuccess: (String) -> Unit,
        onFailure: (String) -> Unit
    ) {
        val missionId = FileUtils.getFileName(missionPath, WAYPOINT_FILE_TAG)
        // 假设 KMZ 只包含一条航线，使用默认 ID [0]
        val waylineIDs: List<Int> = listOf(0)

        WaypointMissionManager.getInstance().startMission(
            missionId,
            waylineIDs,
            object : CommonCallbacks.CompletionCallback {
                override fun onSuccess() {
                    Log.d(TAG, "航线任务启动成功")
                    onSuccess("Mission Started Successfully")
                }

                override fun onFailure(error: IDJIError) {
                    Log.e(TAG, "航线任务启动失败: ${error.description()}")
                    onFailure("启动失败: ${error.description()}")
                }
            }
        )
    }
}