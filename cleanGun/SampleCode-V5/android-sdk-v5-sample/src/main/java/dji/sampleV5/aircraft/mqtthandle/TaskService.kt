package dji.sampleV5.aircraft.mqtthandle

import android.content.Context
import android.util.Log
import dji.sampleV5.aircraft.data.getValueForKey
import dji.sdk.keyvalue.key.FlightControllerKey
import dji.v5.common.callback.CommonCallbacks
import dji.v5.common.error.IDJIError
import dji.v5.manager.aircraft.waypoint3.WaypointMissionManager
import dji.v5.utils.common.FileUtils
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import java.io.ByteArrayOutputStream
import java.util.zip.ZipEntry
import java.util.zip.ZipFile
import kotlin.math.min

/**
 * 航线任务服务
 * 职责：处理航线任务的下载、上传、启动
 */
class TaskService(private val context: Context) {
    companion object {
        private const val TAG = "TaskService"
        private const val WAYPOINT_FILE_TAG = ".kmz"
        private const val KMZ_TEXT_ENTRY_MAX_BYTES = 512_000
    }

    /**
     * 打印飞机当前高度与 KMZ 内可能的高度字段，用于排查「航线高度过高」等启动失败。
     */
    private fun logAltitudeDiagnostics(reason: String, missionPath: String) {
        val relAlt = getValueForKey(FlightControllerKey.KeyAltitude)
        val takeoffLocAlt = getValueForKey(FlightControllerKey.KeyTakeoffLocationAltitude)
        val loc3d = getValueForKey(FlightControllerKey.KeyAircraftLocation3D)
        val isFlying = getValueForKey(FlightControllerKey.KeyIsFlying)
        val ultrasonic = getValueForKey(FlightControllerKey.KeyUltrasonicHeight)
        val amslApprox = if (relAlt != null && takeoffLocAlt != null) relAlt + takeoffLocAlt else null
        val kmzHint = tryExtractKmzAltitudeHints(missionPath)
        Log.w(
            TAG,
            "[$reason] 高度诊断 | 飞机 isFlying=$isFlying | KeyAltitude相对起飞=${relAlt}m " +
                "takeoffLocationAltitude=${takeoffLocAlt}m AMSL约=${amslApprox}m " +
                "| Location3D.altitude(常为椭球高)=${loc3d?.altitude}m ultrasonic=${ultrasonic}m " +
                "| KMZ线索: $kmzHint"
        )
    }

    /**
     * 从 KMZ(zip) 内 kml/xml 中粗提取与高度相关的数值（仅供参考，与 DJI 校验逻辑可能不完全一致）。
     */
    private fun readZipEntryTextCapped(zip: ZipFile, entry: ZipEntry, maxBytes: Int): String {
        if (entry.size > 0 && entry.size > maxBytes) return ""
        return zip.getInputStream(entry).use { ins ->
            val out = ByteArrayOutputStream(min(8192, maxBytes))
            val chunk = ByteArray(8192)
            var total = 0
            while (total < maxBytes) {
                val toRead = min(chunk.size, maxBytes - total)
                val n = ins.read(chunk, 0, toRead)
                if (n <= 0) break
                out.write(chunk, 0, n)
                total += n
            }
            out.toString(Charsets.UTF_8.name())
        }
    }

    private fun tryExtractKmzAltitudeHints(kmzPath: String): String {
        return try {
            val found = mutableListOf<String>()
            ZipFile(kmzPath).use { zip ->
                val entries = zip.entries().toList()
                for (e in entries) {
                    if (e.isDirectory) continue
                    val name = e.name.lowercase()
                    if (!name.endsWith(".kml") && !name.endsWith(".xml")) continue
                    val text = readZipEntryTextCapped(zip, e, KMZ_TEXT_ENTRY_MAX_BYTES)
                    if (text.isEmpty()) continue
                    val patterns = listOf(
                        Regex("executeHeight[^>]*>([0-9]+\\.?[0-9]*)", RegexOption.IGNORE_CASE),
                        Regex("heightMode[^>]*>([^<]+)<", RegexOption.IGNORE_CASE),
                        Regex("<wpml:height>([0-9]+\\.?[0-9]*)</wpml:height>", RegexOption.IGNORE_CASE),
                        Regex("<height>([0-9]+\\.?[0-9]*)</height>", RegexOption.IGNORE_CASE),
                        Regex("ellipsoidHeight[^>]*>([0-9]+\\.?[0-9]*)", RegexOption.IGNORE_CASE)
                    )
                    for (p in patterns) {
                        p.findAll(text).take(3).forEach { m ->
                            found.add("${e.name.takeLast(40)}:${m.groupValues.drop(1).joinToString()}")
                        }
                    }
                }
            }
            if (found.isEmpty()) "未在KMZ的kml/xml中匹配到常见高度标签"
            else found.distinct().take(12).joinToString(" | ")
        } catch (e: Exception) {
            "读取KMZ异常: ${e.message}"
        }
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
        CoroutineScope(Dispatchers.IO).launch {
            try {
                // 1. 只调用一次下载函数，接收 Result 对象
                val result = FileDownloader.downloadKMZFile(
                    fileUrl = request.fileUrl,
                    fileName = request.key,
                    context = context
                )

                // 2. 使用 when 分支直接处理结果
                when (result) {
                    is DownloadResult.Success -> {
                        // 下载成功，执行推送逻辑
                        pushKMZFileToAircraft(
                            missionPath = result.file.absolutePath,
                            onProgress = onProgress,
                            onSuccess = onSuccess,
                            onFailure = onFailure
                        )
                    }
                    is DownloadResult.Failure -> {
                        // 下载失败，直接回调具体的错误原因
                        Log.e(TAG, "下载失败原因: ${result.reason}")
                        onFailure("下载 KMZ 失败: ${result.reason}")
                    }
                }

            } catch (e: Exception) {
                // 这里的 catch 用于捕获协程执行过程中其他未预料的异常
                Log.e(TAG, "上传航线任务系统崩溃: ${e.message}", e)
                onFailure("系统异常: ${e.message}")
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

        Log.d(TAG, "航线开始上传: $missionPath")

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

        logAltitudeDiagnostics("启动航线任务前", missionPath)
        Log.d(TAG, "调用 startMission missionId=$missionId waylineIDs=$waylineIDs path=$missionPath")

        WaypointMissionManager.getInstance().startMission(
            missionId,
            waylineIDs,
            object : CommonCallbacks.CompletionCallback {
                override fun onSuccess() {
                    Log.d(TAG, "航线任务启动成功")
                    onSuccess("Mission Started Successfully")
                }

                override fun onFailure(error: IDJIError) {
                    val code = try {
                        error.errorCode()
                    } catch (_: Exception) {
                        null
                    }
                    logAltitudeDiagnostics("航线任务启动失败(对照用)", missionPath)
                    Log.e(
                        TAG,
                        "航线任务启动失败: ${error.description()} code=$code | 请对照上方 [高度诊断] 中「相对起飞高度」与 KMZ 航线高度(海拔/相对)是否一致"
                    )
                    onFailure("启动失败: ${error.description()}")
                }
            }
        )
    }
}