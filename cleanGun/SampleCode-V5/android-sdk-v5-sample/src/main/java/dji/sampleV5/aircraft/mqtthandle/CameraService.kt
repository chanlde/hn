package dji.sampleV5.aircraft.mqtthandle

import MinioUploader
import android.content.Context
import android.os.Handler
import android.os.Looper
import android.util.Log
import dji.sampleV5.aircraft.data.UavControlResponse
import com.dji.network.GeneralUtils.BUCKET_NAME
import com.dji.network.ConfigManager
import com.dji.util.FileLogger

import dji.sampleV5.aircraft.util.sendResponse
import dji.sdk.keyvalue.key.CameraKey
import dji.sdk.keyvalue.key.KeyTools
import dji.sdk.keyvalue.value.camera.CameraMode
import dji.sdk.keyvalue.value.camera.MediaFileType
import dji.sdk.keyvalue.value.camera.CameraVideoStreamSourceType
import dji.v5.common.callback.CommonCallbacks
import dji.v5.common.error.IDJIError
import dji.v5.manager.KeyManager
import dji.v5.manager.datacenter.MediaDataCenter
import dji.v5.manager.datacenter.media.MediaFile
import dji.v5.manager.datacenter.media.MediaFileListStateListener
import dji.v5.manager.datacenter.media.MediaFileListState
import dji.v5.manager.datacenter.media.MediaFileFilter
import dji.v5.manager.datacenter.media.PullMediaFileListParam
import dji.v5.manager.datacenter.media.MediaFileDownloadListener
import dji.v5.manager.datacenter.media.MediaFileListDataSource
import dji.v5.manager.interfaces.IMediaManager
import dji.v5.manager.interfaces.ICameraStreamManager
import dji.sdk.keyvalue.value.camera.CameraStorageLocation
import dji.sdk.keyvalue.value.camera.DateTime
import dji.sdk.keyvalue.value.common.ComponentIndexType
import dji.sdk.keyvalue.value.common.EmptyMsg
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.cancel
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import java.io.File
import java.io.FileOutputStream
import java.text.SimpleDateFormat
import java.util.Calendar
import java.util.Date
import java.util.Locale
import java.util.concurrent.CopyOnWriteArraySet
import java.util.concurrent.atomic.AtomicInteger

/**
 * 相机服务
 * 职责：处理拍照和录像命令，将照片和视频保存到任务文件夹，并实时上传到服务器
 */
class CameraService(
    private val context: Context,
    private val deviceId: String  // 设备ID，用于文件上传
) {

    companion object {
        private const val TAG = "CameraService"
        private const val MAX_UPLOAD_RETRIES = 3
        private const val RETRY_BASE_DELAY_MS = 2000L
    }
    private val uploadScope = CoroutineScope(SupervisorJob() + Dispatchers.IO)
    /** MinIO 上传串行化，避免并发到达服务端顺序错乱 */
    private val minioUploadMutex = Mutex()

    private val minioUploader = MinioUploader()

    private val mainHandler = Handler(Looper.getMainLooper())
    private var pullListAfterPhotoRunnable: Runnable? = null
    private var pullListAfterVideoRunnable: Runnable? = null

    @Volatile
    var currentMissionFolderPath: String? = null
    @Volatile
    private var currentTaskId: String? = "cccccccccccccc"
    private var isRecording = false
    private val uploadingCount = AtomicInteger(0)

    @Volatile
    var isUploading: Boolean = false
        get() = uploadingCount.get() > 0

    /**
     * 获取当前上传状态
     */
    fun getIsUploading(): Boolean = uploadingCount.get() > 0

    /**
     * 获取媒体文件列表状态
     */
    fun getMediaFileListState(): MediaFileListState? {
        return try {
            mediaManager.getMediaFileListState()
        } catch (e: Exception) {
            Log.w(TAG, "获取媒体文件列表状态失败: ${e.message}")
            null
        }
    }

    // 文件上传器（非 lazy，便于 destroy 时取消协程）
    private val fileUploader = MissionFileUploader(deviceId)

    // 当前使用的相机索引（动态探测，默认 PORT_1）
    @Volatile
    private var activeCameraIndex: ComponentIndexType = ComponentIndexType.PORT_1

    // 当前使用的存储位置（动态探测，默认 SDCARD）
    @Volatile
    private var activeStorageLocation: CameraStorageLocation = CameraStorageLocation.SDCARD

    // IMediaManager
    private val mediaManager: IMediaManager = MediaDataCenter.getInstance().mediaManager

    // ICameraStreamManager (用于检测可用摄像头)
    private val cameraStreamManager: ICameraStreamManager = MediaDataCenter.getInstance().cameraStreamManager

    // 已处理的文件集合（用于去重）
    private val processedFiles = CopyOnWriteArraySet<String>()

    // 文件列表状态监听器
    private var mediaFileListStateListener: MediaFileListStateListener? = null

    // 可用摄像头监听器
    private var availableCameraListener: ICameraStreamManager.AvailableCameraUpdatedListener? = null

    // 可用摄像头列表
    private var availableCameras: List<ComponentIndexType> = emptyList()

    // 初始化状态标志
    private var isInitialized = false

    /**
     * 初始化 MediaManager
     * 必须在 DJI SDK 初始化完成后调用
     */
    fun initialize() {
        if (isInitialized) {
            Log.w(TAG, "⚠️ CameraService 已经初始化，跳过")
            return
        }

        FileLogger.i(TAG, "CameraService 初始化开始")

        // 不需要启用 MediaManager，直接设置数据源和监听器
        // MediaManager 应该已经由 SDK 自动管理了
        try {
            // 设置可用摄像头监听器
            setupCameraListener()
            // 设置媒体文件数据源（指定存储位置）
            setupMediaDataSource()
            // 设置媒体文件监听器
            setupMediaFileListener()

            isInitialized = true
            FileLogger.i(TAG, "CameraService 初始化完成")
        } catch (e: Exception) {
            FileLogger.e(TAG, "CameraService 初始化失败: ${e.message}", e)
        }
        setupDownloadPath()
    }

    private fun setupDownloadPath() {
        // 基础目录: /storage/emulated/0/Camera
        val baseDir = "/storage/emulated/0/Camera"

        val baseFolder = File(baseDir)
        if (!baseFolder.exists()) {
            val created = baseFolder.mkdirs()
            if (created) {
                FileLogger.i(TAG, "创建 Camera 目录成功: $baseDir")
            } else {
                FileLogger.w(TAG, "创建 Camera 目录失败: $baseDir")
                return
            }
        }

        // 创建带时间戳的任务文件夹
        createMissionFolder(baseDir)
    }

    private fun createMissionFolder(baseDir: String) {
        // 生成时间戳文件夹名称
        val timestamp = SimpleDateFormat("yyyyMMdd_HHmmss", Locale.getDefault()).format(Date())
        val missionFolderName = "Mission_$timestamp"  // 例如: Mission_20231127_143052

        val missionFolderPath = "$baseDir/$missionFolderName"
        val missionFolder = File(missionFolderPath)

        if (!missionFolder.exists()) {
            val created = missionFolder.mkdirs()
            if (created) {
                FileLogger.i(TAG, "✅ 创建任务文件夹成功: $missionFolderPath")
            } else {
                FileLogger.w(TAG, "创建任务文件夹失败: $missionFolderPath")
                return
            }
        }
        // 设置当前任务文件夹路径
        currentMissionFolderPath = missionFolderPath
        FileLogger.i(TAG, "当前任务文件夹: $currentMissionFolderPath")
    }

    /**
     * 设置当前任务文件夹路径
     * 在任务开始时调用
     */
    fun setMissionFolderPath(folderPath: String, taskId: String? = null) {
        currentMissionFolderPath = folderPath
        currentTaskId = taskId
        // 重新标记当前卡上所有文件为已处理，而非清空集合
        // 清空集合会导致下次全量拉取时把历史文件当新文件重传
        markExistingFilesAsProcessed()
        FileLogger.i(TAG, "设置任务文件夹路径: $folderPath, 任务ID: $taskId")
    }

    /**
     * 设置任务ID（从下发的航线任务中获取）
     * 在收到航线任务时调用
     */
    fun setTaskId(taskId: String) {
        currentTaskId = taskId
        FileLogger.i(TAG, "设置任务ID: $taskId")
    }

    /**
     * 清除任务文件夹路径
     * 在任务结束时调用
     */
    fun clearMissionFolderPath() {
        currentMissionFolderPath = null
        currentTaskId = null
        FileLogger.i(TAG, "清除任务文件夹路径")
    }
    
    /**
     * 获取当前相机类型（FIR/CCD）
     * 优先根据文件名判断：带_T的是红外，带_Z的是可见光
     * 如果文件名无法判断，则通过获取相机视频流源类型来判断
     * @param fileName 文件名，用于判断相机类型
     * @param callback 回调函数，返回相机类型
     */
    private fun getCameraType(fileName: String, callback: (String) -> Unit) {
        // 优先根据文件名判断：带_T的是红外（FIR），带_Z的是可见光（CCD）
        val fileNameUpper = fileName.uppercase()
        when {
            fileNameUpper.contains("_T") || fileNameUpper.contains("_T_") || fileNameUpper.contains("_T.JPEG") -> {
                callback("FIR")
                return
            }
            fileNameUpper.contains("_Z") || fileNameUpper.contains("_Z_") || fileNameUpper.contains("_Z.JPEG") -> {
                callback("CCD")
                return
            }
        }
        
        // 如果文件名无法判断，则通过视频流源类型判断
        try {
            val key = KeyTools.createKey(CameraKey.KeyCameraVideoStreamSource, activeCameraIndex)
            KeyManager.getInstance().getValue(key, object : CommonCallbacks.CompletionCallbackWithParam<CameraVideoStreamSourceType> {
                override fun onSuccess(result: CameraVideoStreamSourceType?) {
                    val cameraType = when (result) {
                        CameraVideoStreamSourceType.INFRARED_CAMERA -> "FIR"
                        else -> "CCD"
                    }
                    FileLogger.throttledD(TAG, "cameraTypeStream", "相机类型=$cameraType 流源=$result 文件=$fileName", 10_000L)
                    callback(cameraType)
                }
                
                override fun onFailure(error: IDJIError) {
                    FileLogger.w(TAG, "获取相机类型失败: ${error.description()}, 默认使用 CCD, 文件名: $fileName")
                    callback("CCD")
                }
            })
        } catch (e: Exception) {
            FileLogger.e(TAG, "获取相机类型异常: ${e.message}, 默认使用 CCD, 文件名: $fileName", e)
            callback("CCD")
        }
    }

    private fun toJavaDate(dateTime: DateTime?): Date? {
        if (dateTime == null) return null
        return try {
            val calendar = Calendar.getInstance()
            calendar.set(
                dateTime.year,
                (dateTime.month - 1).coerceAtLeast(0),
                dateTime.day,
                dateTime.hour,
                dateTime.minute,
                dateTime.second
            )
            calendar.set(Calendar.MILLISECOND, 0)
            calendar.time
        } catch (e: Exception) {
            FileLogger.w(TAG, "DateTime 转 Date 失败: ${e.message}")
            null
        }
    }

    /**mei
     * 设置可用摄像头监听器
     */
    private fun setupCameraListener() {
        availableCameraListener = object : ICameraStreamManager.AvailableCameraUpdatedListener {
            override fun onAvailableCameraUpdated(availableCameraList: List<ComponentIndexType>) {
                FileLogger.i(
                    TAG,
                    "可用摄像头更新 count=${availableCameraList.size} cameras=${availableCameraList.joinToString()}"
                )

                availableCameras = availableCameraList

                // 摄像头列表更新后，重新设置数据源
                if (availableCameraList.isNotEmpty()) {
                    setupMediaDataSource()
                }
            }

            override fun onCameraStreamEnableUpdate(cameraStreamEnableMap: Map<ComponentIndexType, Boolean>) {
                FileLogger.throttledD(
                    TAG,
                    "cameraStreamEnable",
                    "摄像头流状态: " + cameraStreamEnableMap.entries.joinToString { "${it.key}=${it.value}" },
                    8_000L
                )
            }
        }

        // 注册监听器
        cameraStreamManager.addAvailableCameraUpdatedListener(availableCameraListener!!)
        FileLogger.i(TAG, "摄像头监听器已注册")
    }

    /**
     * 设置媒体文件数据源
     * 指定要访问的相机和存储位置
     */
    private fun setupMediaDataSource() {
        // 动态探测相机索引：优先用可用摄像头列表中的第一个非 FPV 相机
        if (availableCameras.isNotEmpty()) {
            val nonFpvCamera = availableCameras.firstOrNull { it != ComponentIndexType.FPV }
            activeCameraIndex = nonFpvCamera ?: availableCameras.first()
            FileLogger.i(TAG, "媒体数据源相机=$activeCameraIndex 可用=$availableCameras")
        } else {
            FileLogger.w(TAG, "未检测到可用摄像头，使用默认索引: $activeCameraIndex")
        }

        applyMediaDataSource(activeCameraIndex, activeStorageLocation)
    }

    private fun applyMediaDataSource(cameraIndex: ComponentIndexType, storageLocation: CameraStorageLocation) {
        val dataSource = MediaFileListDataSource.Builder()
            .setLocation(storageLocation)
            .setIndexType(cameraIndex)
            .build()

        mediaManager.setMediaFileDataSource(dataSource)

        FileLogger.i(
            TAG,
            "媒体数据源已设置 storage=${dataSource.storageLocation} index=${dataSource.componentIndexType}"
        )
    }

    /**
     * 尝试用 INTERNAL_STORAGE 回退（在 SDCARD 拉取失败时调用）
     */
    private fun fallbackToInternalStorage() {
        if (activeStorageLocation == CameraStorageLocation.SDCARD) {
            FileLogger.w(TAG, "SDCARD 拉取失败，尝试切换到 INTERNAL")
            activeStorageLocation = CameraStorageLocation.INTERNAL
            applyMediaDataSource(activeCameraIndex, activeStorageLocation)
        }
    }

    /**
     * 手动重新设置数据源（调试用）
     */
    fun manualSetupDataSource() {
        FileLogger.i(TAG, "手动触发数据源设置")
        setupMediaDataSource()
    }

    /**
     * 设置媒体文件监听器
     * 使用 MediaFileListStateListener 监听文件列表状态变化
     * 当状态变为 UP_TO_DATE 时，说明有新文件生成，需要获取文件列表并处理新文件
     */
    private fun setupMediaFileListener() {
        mediaFileListStateListener = object : MediaFileListStateListener {
            override fun onUpdate(state: MediaFileListState) {
                FileLogger.logStateChange(TAG, "mediaFileListState", state)

                when (state) {
                    MediaFileListState.UP_TO_DATE -> {
                        handleNewMediaFiles()
                    }
                    MediaFileListState.UPDATING -> {
                        FileLogger.throttledD(TAG, "mediaListUpdating", "媒体文件列表更新中", 5_000L)
                    }
                    MediaFileListState.IDLE -> {
                        FileLogger.throttledD(TAG, "mediaListIdle", "媒体文件列表空闲", 10_000L)
                    }
                    else -> {
                        FileLogger.w(TAG, "媒体文件列表未知状态: $state")
                    }
                }
            }
        }

        mediaManager.addMediaFileListStateListener(mediaFileListStateListener)
        FileLogger.i(TAG, "媒体文件列表监听器已注册 当前状态=${mediaManager.getMediaFileListState()}")

        markExistingFilesAsProcessed()
    }

    /**
     * 标记所有现有文件为已处理
     * 在初始化时调用，避免下载相机中的旧照片/视频
     */
    private fun markExistingFilesAsProcessed() {
        val param = PullMediaFileListParam.Builder()
            .filter(MediaFileFilter.ALL)
            .build()

        mediaManager.pullMediaFileListFromCamera(param, object : CommonCallbacks.CompletionCallback {
            override fun onSuccess() {
                try {
                    val mediaFileListData = mediaManager.getMediaFileListData()
                    val mediaFiles = mediaFileListData?.getData() ?: emptyList()

                    // 标记所有文件为已处理
                    mediaFiles.forEach { mediaFile ->
                        mediaFile.fileName?.let { fileName ->
                            processedFiles.add(fileName)
                        }
                    }

                    FileLogger.i(TAG, "已标记旧文件为已处理 count=${processedFiles.size}")
                } catch (e: Exception) {
                    FileLogger.w(TAG, "标记旧文件失败: ${e.message}")
                }
            }

            override fun onFailure(error: IDJIError) {
                FileLogger.w(
                    TAG,
                    "拉取旧文件列表失败: ${error.description()} storage=$activeStorageLocation camera=$activeCameraIndex"
                )
                fallbackToInternalStorage()
            }
        })
    }

    /**
     * 拉取媒体文件列表
     */
    private fun pullMediaFileList() {
        val stateHint = try {
            mediaManager.getMediaFileListState().toString()
        } catch (e: Exception) {
            "?"
        }
        FileLogger.i(TAG, "拉取媒体文件列表 当前状态=$stateHint")

        val param = PullMediaFileListParam.Builder()
            .filter(MediaFileFilter.ALL)
            .build()

        mediaManager.pullMediaFileListFromCamera(param, object : CommonCallbacks.CompletionCallback {
            override fun onSuccess() {
                val fileCount = mediaManager.getMediaFileListData()?.getData()?.size ?: 0
                FileLogger.i(TAG, "拉取媒体文件列表成功 fileCount=$fileCount")
            }

            override fun onFailure(error: IDJIError) {
                FileLogger.w(
                    TAG,
                    "拉取媒体文件列表失败: ${error.description()} code=${error.errorCode()} storage=$activeStorageLocation camera=$activeCameraIndex"
                )
                fallbackToInternalStorage()
            }
        })
    }

    /**
     * 处理新生成的媒体文件
     * 获取文件列表，找出新文件并移动到任务文件夹
     */
    private fun handleNewMediaFiles() {
        val missionPath = currentMissionFolderPath ?: run {
            FileLogger.w(TAG, "无任务文件夹路径，跳过新媒体处理")
            return
        }

        try {
            val mediaFileListData = mediaManager.getMediaFileListData()
            val mediaFiles = mediaFileListData?.getData()

            if (mediaFiles == null) {
                FileLogger.w(TAG, "媒体文件列表为空，跳过处理")
                return
            }

            FileLogger.i(
                TAG,
                "处理新媒体 files=${mediaFiles.size} processed=${processedFiles.size} mission=$missionPath"
            )

            // 按拍摄时间、文件名排序，保证处理与上传顺序稳定
            val sortedMediaFiles = mediaFiles.sortedWith(
                compareBy<MediaFile>(
                    { mf -> toJavaDate(mf.date)?.time ?: 0L },
                    { mf -> mf.fileName ?: "" }
                )
            )

            var uploadOrder = 0
            sortedMediaFiles.forEachIndexed { index, mediaFile ->
                val fileId = mediaFile.fileName ?: return@forEachIndexed

                if (processedFiles.contains(fileId)) {
                    return@forEachIndexed
                }

                // 根据 MediaFileType 枚举判断是照片还是视频（先判定类型再标记已处理）
                val fileType: String = when (mediaFile.fileType) {
                    MediaFileType.JPEG,
                    MediaFileType.DNG,
                    MediaFileType.TIFF,
                    MediaFileType.PANORAMA,
                    MediaFileType.TIFF_SEQ,
                    MediaFileType.CNDG,
                    MediaFileType.LDR,
                    MediaFileType.LDRT,
                    MediaFileType.RPT,
                    MediaFileType.MET,
                    MediaFileType.CLC,
                    MediaFileType.CLI,
                    MediaFileType.LRF,
                    MediaFileType.THM,
                    MediaFileType.SCR -> "photo"

                    MediaFileType.MOV,
                    MediaFileType.MP4,
                    MediaFileType.SEQ -> "video"

                    MediaFileType.PHOTO_FOLDER,
                    MediaFileType.VIDEO_FOLDER,
                    MediaFileType.FOLDER_ATTR,
                    MediaFileType.AUDIO,
                    MediaFileType.UNKNOWN -> {
                        return@forEachIndexed
                    }

                    else -> {
                        FileLogger.w(TAG, "未知的文件类型: ${mediaFile.fileType}, 文件名: $fileId")
                        return@forEachIndexed
                    }
                }

                processedFiles.add(fileId)
                val orderForUpload = uploadOrder++

                val mediaDate: Date? = try { toJavaDate(mediaFile.date) } catch (_: Exception) { null }

                FileLogger.i(TAG, "新媒体下载开始 file=$fileId type=$fileType order=$orderForUpload date=$mediaDate")

                downloadAndMoveMediaFile(mediaFile, fileType, missionPath, mediaDate, orderForUpload, fileId)
            }
        } catch (e: Exception) {
            FileLogger.e(TAG, "处理新媒体文件失败: ${e.message}", e)
        }
    }

    /**
     * 下载媒体文件并移动到任务文件夹
     *
     * 根据反编译的 MediaFile 代码，下载方法在 MediaFile 对象上
     * 使用 mediaFile.downloadMediaFile(MediaFileDownloadListener) 来下载文件
     */
    private fun downloadAndMoveMediaFile(
        mediaFile: MediaFile,
        fileType: String,
        missionFolderPath: String,
        mediaDate: Date? = null,
        uploadOrderIndex: Int = 0,
        processedFileId: String
    ) {
        val fileName = mediaFile.fileName ?: "unknown_file"

        // 创建临时下载目录
        val tempDir = File(context.cacheDir, "media_download")
        if (!tempDir.exists()) {
            tempDir.mkdirs()
        }

        // 临时文件路径（下载完成后会移动）
        val tempFile = File(tempDir, fileName)
        var fileOutputStream: FileOutputStream? = null

        // 创建下载监听器
        val downloadListener = object : MediaFileDownloadListener {
            override fun onStart() {
                FileLogger.d(TAG, "文件下载开始: $fileName")
                try {
                    // 创建文件输出流
                    fileOutputStream = FileOutputStream(tempFile)
                } catch (e: Exception) {
                    Log.d(TAG, "创建文件输出流失败: ${e.message}")
                }
            }

            override fun onProgress(total: Long, current: Long) {
                val progress = if (total > 0) {
                    ((current.toDouble() / total) * 100).toInt()
                } else {
                    0
                }
                if (progress % 20 == 0 || current == total) {
                    FileLogger.throttledD(
                        TAG,
                        "dlProgress:$fileName",
                        "下载进度 $fileName $progress% ($current/$total)",
                        3_000L
                    )
                }
            }

            override fun onRealtimeDataUpdate(data: ByteArray, position: Long) {
                // 接收下载的数据块，写入文件
                try {
                    fileOutputStream?.write(data)
                } catch (e: Exception) {
                    Log.d(TAG, "写入文件数据失败: ${e.message}")
                }
            }

            override fun onFinish() {

                // 关闭文件输出流
                try {
                    fileOutputStream?.close()
                    fileOutputStream = null
                } catch (e: Exception) {
                    Log.d(TAG, "关闭文件输出流失败: ${e.message}")
                }

                // 检查文件是否存在
                if (tempFile.exists() && tempFile.length() > 0) {
                    FileLogger.i(TAG, "文件下载完成 $fileName size=${tempFile.length()}")
                    moveMediaFileToMissionFolder(
                        tempFile.absolutePath,
                        fileType,
                        mediaDate,
                        uploadOrderIndex
                    )
                } else {
                    FileLogger.w(TAG, "下载结果为空: $fileName path=${tempFile.absolutePath}")
                    processedFiles.remove(processedFileId)
                }
            }

            override fun onFailure(error: IDJIError) {
                FileLogger.w(TAG, "文件下载失败: $fileName err=${error.description()}")
                processedFiles.remove(processedFileId)

                // 关闭文件输出流
                try {
                    fileOutputStream?.close()
                    fileOutputStream = null
                } catch (e: Exception) {
                    Log.d(TAG, "关闭文件输出流失败: ${e.message}")
                }

                // 删除可能不完整的文件
                if (tempFile.exists()) {
                    tempFile.delete()
                }
            }
        }

        // 开始下载文件
        // 使用 pullOriginalMediaFileFromCamera 方法，offset 从 0 开始
        try {
            mediaFile.pullOriginalMediaFileFromCamera(0L, downloadListener)
        } catch (e: Exception) {
            Log.d(TAG, "调用下载方法失败: ${e.message}")
            processedFiles.remove(processedFileId)
            // 确保关闭文件流
            try {
                fileOutputStream?.close()
            } catch (closeException: Exception) {
                Log.d(TAG, "关闭文件流异常: ${closeException.message}")
            }
        }
    }

    /**
     * 将媒体文件移动到任务文件夹，并立即上传到服务器
     *
     * 注意：这个方法需要在媒体文件监听器的回调中调用
     * 需要根据实际DJI SDK的MediaFile API来获取文件路径
     */
    fun moveMediaFileToMissionFolder(
        sourceFilePath: String,
        fileType: String,
        mediaDate: Date? = null,
        uploadOrderIndex: Int = -1
    ) {
        val missionPath = currentMissionFolderPath ?: run {
            Log.w(TAG, "没有设置任务文件夹路径")
            return
        }

        try {
            val folderManager = MissionFolderManager(context)
            val targetFolder = when (fileType.lowercase()) {
                "photo", "image", "jpg", "jpeg", "png", "dng" -> folderManager.getPhotosFolder(missionPath)
                "video", "mp4", "mov" -> folderManager.getVideosFolder(missionPath)
                else -> {
                    Log.w(TAG, "未知的文件类型: $fileType")
                    return
                }
            }

            val sourceFile = File(sourceFilePath)
            if (!sourceFile.exists()) {
                Log.d(TAG, "源文件不存在: $sourceFilePath")
                return
            }

            val targetFile = File(targetFolder, sourceFile.name)

            val moveSuccess = if (sourceFile.renameTo(targetFile)) {
                true
            } else {
                sourceFile.copyTo(targetFile, overwrite = true)
                sourceFile.delete()
                true
            }

            if (moveSuccess) {
                getCameraType(targetFile.name) { cameraType ->
                    val stationCode = ConfigManager.stationCode.ifEmpty { deviceId }
                    val dateFormat = SimpleDateFormat("yyyy/MM/dd", Locale.getDefault())
                    // 优先使用拍摄时间，无拍摄时间时回退到当前时间
                    val datePath = dateFormat.format(mediaDate ?: Date())
                    val taskId = currentTaskId ?: "unknown_${System.currentTimeMillis()}"
                    val objectFileName = if (uploadOrderIndex >= 0) {
                        String.format("%06d_%s", uploadOrderIndex, targetFile.name)
                    } else {
                        targetFile.name
                    }
                    val objectName = "${stationCode}/${datePath}/${taskId}/${cameraType}/${objectFileName}"
                    val bucketName = ConfigManager.bucketName

                    FileLogger.i(
                        TAG,
                        "MinIO上传开始 station=$stationCode date=$datePath task=$taskId camera=$cameraType bucket=$bucketName object=$objectName endpoint=${ConfigManager.minioEndpoint}"
                    )

                    uploadScope.launch {
                        minioUploadMutex.withLock {
                            uploadWithRetry(targetFile, bucketName, objectName)
                        }
                    }
                }
            }
        } catch (e: Exception) {
            FileLogger.w(TAG, "移动文件失败: ${e.message}")
        }
    }

    /**
     * 带重试的上传（最多重试 MAX_UPLOAD_RETRIES 次，指数退避）
     */
    private suspend fun uploadWithRetry(
        file: File,
        bucketName: String,
        objectName: String,
        maxRetries: Int = MAX_UPLOAD_RETRIES
    ) {
        uploadingCount.incrementAndGet()
        var lastError: String? = null

        for (attempt in 0..maxRetries) {
            if (attempt > 0) {
                val delayMs = RETRY_BASE_DELAY_MS * (1L shl (attempt - 1))
                FileLogger.w(TAG, "上传重试 ${attempt}/$maxRetries delay=${delayMs}ms file=${file.name}")
                delay(delayMs)
            }

            var uploadSuccess = false
            minioUploader.uploadFile(
                filePath = file.absolutePath,
                bucketName = bucketName,
                objectName = objectName,
                onSuccess = { fileUrl: String ->
                    FileLogger.i(TAG, "MinIO上传成功 file=${file.name} url=$fileUrl")
                    uploadSuccess = true
                },
                onFailure = { error: String ->
                    lastError = error
                    FileLogger.w(TAG, "❌ 文件上传失败 (第${attempt + 1}次): ${file.name}, 错误: $error")
                    if (attempt == maxRetries) {
                        FileLogger.e(TAG, "文件上传最终失败: ${file.name}, 错误: $error")
                        FileLogger.e(TAG, "   MinIO配置 -> endpoint: ${ConfigManager.minioEndpoint}, " +
                                "bucket: ${ConfigManager.bucketName}, " +
                                "accessKey: ${ConfigManager.minioAccessKey.take(4)}****, " +
                                "stationCode: ${ConfigManager.stationCode}")
                    }
                }
            )

            if (uploadSuccess) {
                uploadingCount.decrementAndGet()
                return
            }
        }

        uploadingCount.decrementAndGet()
    }

    /**
     * 按平台参数切换当前媒体数据源相机（MQTT type=7）
     * 1=广角(LEFT_OR_MAIN), 2=长焦(RIGHT), 3=红外/第三路（可用列表中除 FPV/广角/长焦外的首个）
     */
    fun switchActiveCameraByParameter(parameter: Int?, response: UavControlResponse) {
        if (!isInitialized) {
            response.message = "相机服务未初始化"
            response.result = "FALSE"
            sendResponse(response)
            return
        }
        val target: ComponentIndexType = when (parameter) {
            1 -> ComponentIndexType.LEFT_OR_MAIN
            2 -> ComponentIndexType.RIGHT
            3 -> {
                val thermalOrExtra = availableCameras.firstOrNull { cam ->
                    cam != ComponentIndexType.FPV &&
                        cam != ComponentIndexType.LEFT_OR_MAIN &&
                        cam != ComponentIndexType.RIGHT
                }
                if (thermalOrExtra == null) {
                    response.message = "未检测到红外/第三路相机，请确认机型与可用相机列表"
                    response.result = "FALSE"
                    sendResponse(response)
                    return
                }
                thermalOrExtra
            }
            else -> {
                response.message = "无效的相机参数: $parameter（有效: 1=广角 2=长焦 3=红外/第三路）"
                response.result = "FALSE"
                sendResponse(response)
                return
            }
        }
        if (availableCameras.isNotEmpty() && target !in availableCameras) {
            FileLogger.w(TAG, "目标相机 $target 不在当前可用列表 $availableCameras，仍尝试切换数据源")
        }
        activeCameraIndex = target
        applyMediaDataSource(activeCameraIndex, activeStorageLocation)
        pullMediaFileList()
        FileLogger.i(TAG, "切换相机数据源 parameter=$parameter -> $target")
        response.message = "已切换相机数据源: $target"
        response.result = "TRUE"
        sendResponse(response)
    }

    /**
     * 拍照
     * 使用 KeyStartShootPhoto 开始拍照
     * 先切换到拍照模式，再执行拍照命令
     */
    fun takePhoto(response: UavControlResponse) {

        //testFileUpload(minioUploader)

        if (currentMissionFolderPath == null) {
            Log.w(TAG, "未设置任务文件夹路径，无法保存照片")
            response.message = "未设置任务文件夹路径"
            response.result = "FALSE"
            sendResponse(response)
            return
        }

        val keyCameraMode = KeyTools.createKey(CameraKey.KeyCameraMode, activeCameraIndex)
        val keyStartShootPhoto = KeyTools.createKey(CameraKey.KeyStartShootPhoto, activeCameraIndex)
        // 先切换到拍照模式
        KeyManager.getInstance().setValue(keyCameraMode, CameraMode.PHOTO_NORMAL, object : CommonCallbacks.CompletionCallback {
            override fun onSuccess() {
                FileLogger.i(TAG, "拍照: 已切到 PHOTO_NORMAL index=$activeCameraIndex")

                KeyManager.getInstance().performAction(keyStartShootPhoto, object : CommonCallbacks.CompletionCallbackWithParam<EmptyMsg> {
                    override fun onSuccess(t: EmptyMsg) {

                        response.message = "拍照命令执行成功"
                        response.result = "TRUE"
                        sendResponse(response)

                        // 拍照成功后，延迟2秒刷新文件列表（给相机时间保存文件）
                        pullListAfterPhotoRunnable?.let { mainHandler.removeCallbacks(it) }
                        pullListAfterPhotoRunnable = Runnable {
                            FileLogger.i(TAG, "拍照完成，刷新媒体列表")
                            pullMediaFileList()
                        }
                        mainHandler.postDelayed(pullListAfterPhotoRunnable!!, 2000)
                    }

                    override fun onFailure(error: IDJIError) {

                        response.message = "拍照失败: ${error.description() ?: "未知错误"}"
                        response.result = "FALSE"
                        sendResponse(response)
                    }
                })
            }

            override fun onFailure(error: IDJIError) {

                response.message = "切换相机模式失败: ${error.description() ?: "未知错误"}"
                response.result = "FALSE"
                sendResponse(response)
            }
        })
    }

    /**
     * 开始录像
     * 使用 KeyStartRecord 开始录像
     */
    fun startRecordVideo(response: UavControlResponse) {
        if (currentMissionFolderPath == null) {
            Log.d(TAG, "未设置任务文件夹路径，无法保存视频")
            response.message = "未设置任务文件夹路径"
            response.result = "FALSE"
            sendResponse(response)
            return
        }

        if (isRecording) {
            Log.d(TAG, "已经在录像中")
            response.message = "已经在录像中"
            response.result = "FALSE"
            sendResponse(response)
            return
        }

        val keyCameraMode = KeyTools.createKey(CameraKey.KeyCameraMode, activeCameraIndex)
        val keyStartRecord = KeyTools.createKey(CameraKey.KeyStartRecord, activeCameraIndex)
        KeyManager.getInstance().setValue(keyCameraMode, CameraMode.VIDEO_NORMAL, object : CommonCallbacks.CompletionCallback {
            override fun onSuccess() {
                FileLogger.i(TAG, "录像: 已切到 VIDEO_NORMAL index=$activeCameraIndex")
                KeyManager.getInstance().performAction(keyStartRecord, object : CommonCallbacks.CompletionCallbackWithParam<EmptyMsg> {
                    override fun onSuccess(t: EmptyMsg) {
                        FileLogger.i(TAG, "开始录像成功 index=$activeCameraIndex")
                        isRecording = true
                        response.message = "开始录像命令执行成功"
                        response.result = "TRUE"
                        sendResponse(response)
                    }

                    override fun onFailure(error: IDJIError) {
                        FileLogger.w(TAG, "开始录像失败: ${error.description()}")
                        response.message = "开始录像失败: ${error.description()}"
                        response.result = "FALSE"
                        sendResponse(response)
                    }
                })
            }

            override fun onFailure(error: IDJIError) {
                FileLogger.w(TAG, "切换录像模式失败: ${error.description()}")
                response.message = "切换录像模式失败: ${error.description()}"
                response.result = "FALSE"
                sendResponse(response)
            }
        })
    }

    /**
     * 停止录像
     * 使用 KeyStopRecord 停止录像
     */
    fun stopRecordVideo(response: UavControlResponse) {
        if (!isRecording) {
            Log.w(TAG, "当前没有在录像")
            response.message = "当前没有在录像"
            response.result = "FALSE"
            sendResponse(response)
            return
        }

        val keyStopRecord = KeyTools.createKey(CameraKey.KeyStopRecord, activeCameraIndex)
        KeyManager.getInstance().performAction(keyStopRecord, object : CommonCallbacks.CompletionCallbackWithParam<EmptyMsg> {
            override fun onSuccess(t: EmptyMsg) {
                FileLogger.i(TAG, "停止录像成功")
                isRecording = false
                response.message = "停止录像命令执行成功"
                response.result = "TRUE"
                sendResponse(response)

                // 停止录像后，延迟3秒刷新文件列表（给相机时间保存视频文件）
                pullListAfterVideoRunnable?.let { mainHandler.removeCallbacks(it) }
                pullListAfterVideoRunnable = Runnable {
                    FileLogger.i(TAG, "录像停止，刷新媒体列表")
                    pullMediaFileList()
                }
                mainHandler.postDelayed(pullListAfterVideoRunnable!!, 3000)
            }

            override fun onFailure(error: IDJIError) {
                FileLogger.w(TAG, "停止录像失败: ${error.description()}")
                response.message = "停止录像失败: ${error.description()}"
                response.result = "FALSE"
                sendResponse(response)
            }
        })
    }

    /**
     * 清除已处理文件记录
     * 在任务开始时调用，避免重复处理旧文件
     */
    fun clearProcessedFiles() {
        processedFiles.clear()
        FileLogger.i(TAG, "已清除已处理文件记录")
    }

    /**
     * 销毁资源
     */
    fun destroy() {
        pullListAfterPhotoRunnable?.let { mainHandler.removeCallbacks(it) }
        pullListAfterVideoRunnable?.let { mainHandler.removeCallbacks(it) }
        pullListAfterPhotoRunnable = null
        pullListAfterVideoRunnable = null

        fileUploader.cancelPendingUploads()

        // 取消注册媒体文件列表状态监听器
        mediaFileListStateListener?.let {
            mediaManager.removeMediaFileListStateListener(it)
        }
        mediaFileListStateListener = null

        // 取消注册摄像头监听器
        availableCameraListener?.let {
            cameraStreamManager.removeAvailableCameraUpdatedListener(it)
        }
        availableCameraListener = null

        // 不禁用 MediaManager，让 SDK 自动管理

        // 清除数据
        processedFiles.clear()
        availableCameras = emptyList()
        currentMissionFolderPath = null
        isInitialized = false
        uploadScope.cancel()

        FileLogger.i(TAG, "CameraService 已销毁")
    }
}
