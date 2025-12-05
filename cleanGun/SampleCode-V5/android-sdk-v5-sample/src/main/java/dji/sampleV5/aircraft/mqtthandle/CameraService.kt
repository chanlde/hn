package dji.sampleV5.aircraft.mqtthandle

import MinioUploader
import android.content.Context
import android.util.Log
import dji.sampleV5.aircraft.data.UavControlResponse
import com.dji.network.GeneralUtils.BUCKET_NAME

import dji.sampleV5.aircraft.util.sendResponse
import dji.sdk.keyvalue.key.CameraKey
import dji.sdk.keyvalue.key.KeyTools
import dji.sdk.keyvalue.value.camera.CameraMode
import dji.sdk.keyvalue.value.camera.MediaFileType
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
import dji.sdk.keyvalue.value.common.ComponentIndexType
import dji.sdk.keyvalue.value.common.EmptyMsg
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.cancel
import kotlinx.coroutines.launch
import java.io.File
import java.io.FileOutputStream
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale
import java.util.concurrent.CopyOnWriteArraySet

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
    }
    private val uploadScope = CoroutineScope(SupervisorJob() + Dispatchers.IO)

    private val minioUploader = MinioUploader()

    @Volatile
    var currentMissionFolderPath: String? = null
    private var isRecording = false

    // 文件上传器
    private val fileUploader: MissionFileUploader by lazy {
        MissionFileUploader(deviceId)
    }

    // DJI Key - 使用正确的Key名称
    // 拍照：KeyStartShootPhoto 和 KeyStopShootPhoto
    // 录像：KeyStartRecord 和 KeyStopRecord
    // 相机模式：KeyCameraMode
    // 注意：所有 Key 必须指定 ComponentIndexType.FPV 来指定目标相机
    private val keyStartShootPhoto = KeyTools.createKey(CameraKey.KeyStartShootPhoto,ComponentIndexType.LEFT_OR_MAIN)
    private val keyStopShootPhoto = KeyTools.createKey(CameraKey.KeyStopShootPhoto,ComponentIndexType.LEFT_OR_MAIN)
    private val keyStartRecord = KeyTools.createKey(CameraKey.KeyStartRecord,ComponentIndexType.LEFT_OR_MAIN)
    private val keyStopRecord = KeyTools.createKey(CameraKey.KeyStopRecord,ComponentIndexType.LEFT_OR_MAIN)
    private val keyCameraMode = KeyTools.createKey(CameraKey.KeyCameraMode,ComponentIndexType.LEFT_OR_MAIN)

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

         Log.d(TAG, "========================================")
         Log.d(TAG, "🚀 开始初始化 CameraService")
         Log.d(TAG, "========================================")

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
             Log.d(TAG, "✅ CameraService 初始化完成")
        } catch (e: Exception) {
            Log.d(TAG, "❌ CameraService 初始化失败: ${e.message}")
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
                Log.d(TAG, "✅ 创建 Camera 目录成功: $baseDir")
            } else {
                Log.d(TAG, "❌ 创建 Camera 目录失败: $baseDir")
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
                Log.d(TAG, "✅ 创建任务文件夹成功: $missionFolderPath")
            } else {
                Log.d(TAG, "❌ 创建任务文件夹失败: $missionFolderPath")
                return
            }
        }
        // 设置当前任务文件夹路径
        currentMissionFolderPath = missionFolderPath
        Log.d(TAG, "📁 当前任务文件夹: $currentMissionFolderPath")
        // 例如: /storage/emulated/0/Camera/Mission_20231127_143052
    }

    /**
     * 设置当前任务文件夹路径
     * 在任务开始时调用
     */
    fun setMissionFolderPath(folderPath: String) {
        currentMissionFolderPath = folderPath
        // 清除已处理文件记录，避免处理旧文件
        clearProcessedFiles()
         Log.d(TAG, "设置任务文件夹路径: $folderPath")
    }

    /**
     * 清除任务文件夹路径
     * 在任务结束时调用
     */
    fun clearMissionFolderPath() {
        currentMissionFolderPath = null
         Log.d(TAG, "清除任务文件夹路径")
    }

    /**mei
     * 设置可用摄像头监听器
     */
    private fun setupCameraListener() {
        availableCameraListener = object : ICameraStreamManager.AvailableCameraUpdatedListener {
            override fun onAvailableCameraUpdated(availableCameraList: List<ComponentIndexType>) {
                 Log.d(TAG, "========================================")
                 Log.d(TAG, "📹 可用摄像头列表更新: ${availableCameraList.size} 个")
                availableCameraList.forEachIndexed { index, camera ->
                     Log.d(TAG, "   [$index] $camera")
                }
                 Log.d(TAG, "========================================")

                availableCameras = availableCameraList

                // 摄像头列表更新后，重新设置数据源
                if (availableCameraList.isNotEmpty()) {
                    setupMediaDataSource()
                }
            }

            override fun onCameraStreamEnableUpdate(cameraStreamEnableMap: Map<ComponentIndexType, Boolean>) {
                 Log.d(TAG, "📹 摄像头流状态更新:")
                cameraStreamEnableMap.forEach { (camera, enabled) ->
                     Log.d(TAG, "   $camera: ${if (enabled) "启用" else "禁用"}")
                }
            }
        }

        // 注册监听器
        cameraStreamManager.addAvailableCameraUpdatedListener(availableCameraListener!!)
         Log.d(TAG, "✅ 摄像头监听器已注册")

    }

    /**
     * 设置媒体文件数据源
     * 指定要访问的相机和存储位置
     */
    private fun setupMediaDataSource() {
         Log.d(TAG, "========================================")
         Log.d(TAG, "🔧 开始设置媒体文件数据源")

        // 优先使用检测到的可用摄像头
        val camerasToTry = if (availableCameras.isNotEmpty()) {
             Log.d(TAG, "📹 使用检测到的可用摄像头: $availableCameras")
            availableCameras
        } else {
             Log.d(TAG, "⚠️ 未检测到可用摄像头，使用默认列表")
            listOf(ComponentIndexType.FPV, ComponentIndexType.FPV)
        }

        val dataSource = MediaFileListDataSource.Builder()
            .setLocation(CameraStorageLocation.SDCARD)
            .setIndexType(ComponentIndexType.LEFT_OR_MAIN)
            .build()

        mediaManager.setMediaFileDataSource(dataSource)

        // 验证设置是否成功 - 使用刚创建的 dataSource 对象
         Log.d(TAG, "✅ 数据源设置成功！")
         Log.d(TAG, "   存储位置: ${dataSource.storageLocation}")
         Log.d(TAG, "   相机索引: ${dataSource.componentIndexType}")

         Log.d(TAG, "========================================")
    }

    /**
     * 手动重新设置数据源（调试用）
     */
    fun manualSetupDataSource() {
         Log.d(TAG, "🔧🔧🔧 手动触发数据源设置 🔧🔧🔧")
        setupMediaDataSource()
    }

    /**
     * 设置媒体文件监听器
     * 使用 MediaFileListStateListener 监听文件列表状态变化
     * 当状态变为 UP_TO_DATE 时，说明有新文件生成，需要获取文件列表并处理新文件
     */
    private fun setupMediaFileListener() {
         Log.d(TAG, "======== 开始设置媒体文件监听器 ========")

        mediaFileListStateListener = object : MediaFileListStateListener {
            override fun onUpdate(state: MediaFileListState) {
                 Log.d(TAG, "========================================")
                 Log.d(TAG, "📸 媒体文件列表状态变化: $state")
                 Log.d(TAG, "========================================")

                when (state) {
                    MediaFileListState.UP_TO_DATE -> {
                         Log.d(TAG, "✅ 文件列表已更新，开始处理新文件")
                        // 文件列表已更新，获取新文件并移动到任务文件夹
                        handleNewMediaFiles()
                    }
                    MediaFileListState.UPDATING -> {
                        // 文件列表正在更新中
                         Log.d(TAG, "⏳ 媒体文件列表更新中...")
                    }
                    MediaFileListState.IDLE -> {
                        // 空闲状态，但不自动拉取
                         Log.d(TAG, "💤 媒体文件列表为空闲状态")
                    }
                    else -> {
                         Log.d(TAG, "❓ 未知状态: $state")
                    }
                }
            }
        }

        // 注册监听器
        mediaManager.addMediaFileListStateListener(mediaFileListStateListener)
         Log.d(TAG, "✅ 媒体文件列表状态监听器已注册")

        // 获取当前状态
        val currentState = mediaManager.getMediaFileListState()
         Log.d(TAG, "📊 当前媒体文件列表状态: $currentState")

        // 初始化时标记所有旧文件为已处理（避免下载旧照片）
         Log.d(TAG, "🔄 拉取文件列表并标记旧文件...")
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

                     Log.d(TAG, "✅ 已标记 ${processedFiles.size} 个旧文件为已处理，不会下载")
                } catch (e: Exception) {
                    Log.d(TAG, "⚠️ 标记旧文件失败: ${e.message}")
                }
            }

            override fun onFailure(error: IDJIError) {
                Log.d(TAG, "⚠️ 拉取旧文件列表失败: ${error.description()}")
            }
        })
    }

    /**
     * 拉取媒体文件列表
     */
    private fun pullMediaFileList() {
         Log.d(TAG, "========================================")
         Log.d(TAG, "🔄 开始拉取媒体文件列表...")

        // 诊断信息：检查当前状态
        try {
            val currentState = mediaManager.getMediaFileListState()
             Log.d(TAG, "📊 当前文件列表状态: $currentState")
        } catch (e: Exception) {
            Log.d(TAG, "⚠️ 获取状态信息失败: ${e.message}")
        }

        // 创建拉取参数：获取所有类型的文件（照片和视频）
        val param = PullMediaFileListParam.Builder()
            .filter(MediaFileFilter.ALL)
            .build()

         Log.d(TAG, "🚀 执行 pullMediaFileListFromCamera...")
        mediaManager.pullMediaFileListFromCamera(param, object : CommonCallbacks.CompletionCallback {
            override fun onSuccess() {
                 Log.d(TAG, "✅ 拉取媒体文件列表成功")

                // 立即检查文件列表
                val mediaFileListData = mediaManager.getMediaFileListData()
                val fileCount = mediaFileListData?.getData()?.size ?: 0
                 Log.d(TAG, "📊 当前文件列表中有 $fileCount 个文件")
            }

            override fun onFailure(error: IDJIError) {
                Log.d(TAG, "========================================")
                Log.d(TAG, "❌ 拉取媒体文件列表失败")
                Log.d(TAG, "   错误描述: ${error.description()}")
                Log.d(TAG, "   错误码: ${error.errorCode()}")
                Log.d(TAG, "   提示: 可能是存储位置或相机索引类型不正确")
                Log.d(TAG, "========================================")
            }
        })
    }

    /**
     * 处理新生成的媒体文件
     * 获取文件列表，找出新文件并移动到任务文件夹
     */
    private fun handleNewMediaFiles() {
         Log.d(TAG, "========================================")
         Log.d(TAG, "🔍 开始处理新媒体文件")

        val missionPath = currentMissionFolderPath ?: run {
            Log.w(TAG, "⚠️ 没有设置任务文件夹路径，跳过文件处理")
            Log.w(TAG, "任务文件夹路径为空: $currentMissionFolderPath")
            return
        }

         Log.d(TAG, "📁 任务文件夹路径: $missionPath")

        try {
            // 获取媒体文件列表数据
            val mediaFileListData = mediaManager.getMediaFileListData()
             Log.d(TAG, "📊 MediaFileListData 是否为空: ${mediaFileListData == null}")

            val mediaFiles = mediaFileListData?.getData()
             Log.d(TAG, "📊 MediaFiles 是否为空: ${mediaFiles == null}")

            if (mediaFiles == null) {
                Log.w(TAG, "⚠️ 媒体文件列表为空，返回")
                return
            }

             Log.d(TAG, "📊 当前媒体文件列表数量: ${mediaFiles.size}")
             Log.d(TAG, "📊 已处理文件数量: ${processedFiles.size}")

            // 遍历文件列表，找出新文件
             Log.d(TAG, "🔍 开始遍历文件列表...")
            mediaFiles.forEachIndexed { index, mediaFile ->
                val fileId = mediaFile.fileName ?: run {
                     Log.d(TAG, "⚠️ 文件 $index: 文件名为空，跳过")
                    return@forEachIndexed
                }

                 Log.d(TAG, "📄 文件 $index: $fileId (类型: ${mediaFile.fileType})")

                // 检查是否已处理过
                if (processedFiles.contains(fileId)) {
                     Log.d(TAG, "⏭️ 文件 $fileId 已处理过，跳过")
                    return@forEachIndexed
                }

                // 标记为已处理
                processedFiles.add(fileId)
                 Log.d(TAG, "✅ 标记文件为已处理: $fileId")

                // 获取文件类型
                // 根据 MediaFileType 枚举判断是照片还是视频
                val fileType = when (mediaFile.fileType) {
                    // 照片类型
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

                    // 视频类型
                    MediaFileType.MOV,
                    MediaFileType.MP4,
                    MediaFileType.SEQ -> "video"

                    // 其他类型（文件夹、音频等）跳过
                    MediaFileType.PHOTO_FOLDER,
                    MediaFileType.VIDEO_FOLDER,
                    MediaFileType.FOLDER_ATTR,
                    MediaFileType.AUDIO,
                    MediaFileType.UNKNOWN -> {
                         Log.d(TAG, "跳过非照片/视频文件类型: ${mediaFile.fileType}")

                    }

                    // 其他未知类型也跳过
                    else -> {
                        Log.w(TAG, "未知的文件类型: ${mediaFile.fileType}, 文件名: $fileId")
                    }
                }

                 Log.d(TAG, "检测到新文件: $fileId, 类型: $fileType")

                // 下载文件到本地，然后移动到任务文件夹
                downloadAndMoveMediaFile(mediaFile, fileType.toString(), missionPath)
            }
        } catch (e: Exception) {
            Log.d(TAG, "处理新媒体文件失败: ${e.message}")
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
        missionFolderPath: String
    ) {
        val fileName = mediaFile.fileName ?: "unknown_file"
         Log.d(TAG, "准备下载文件: $fileName, 类型: $fileType")

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
                 Log.d(TAG, "文件下载开始: $fileName")
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
                if (progress % 20 == 0 || current == total) { // 每20%或完成时打印
                     Log.d(TAG, "文件下载进度: $fileName - $progress% ($current/$total bytes)")
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
                 Log.d(TAG, "文件下载完成: $fileName")

                // 关闭文件输出流
                try {
                    fileOutputStream?.close()
                    fileOutputStream = null
                } catch (e: Exception) {
                    Log.d(TAG, "关闭文件输出流失败: ${e.message}")
                }

                // 检查文件是否存在
                if (tempFile.exists() && tempFile.length() > 0) {
                     Log.d(TAG, "文件下载成功，大小: ${tempFile.length()} bytes")
                    // 移动到任务文件夹
                    moveMediaFileToMissionFolder(tempFile.absolutePath, fileType)
                } else {
                    Log.d(TAG, "下载的文件不存在或为空: ${tempFile.absolutePath}")
                }
            }

            override fun onFailure(error: IDJIError) {
                Log.d(TAG, "文件下载失败: $fileName, 错误: ${error.description()}")

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
    fun moveMediaFileToMissionFolder(sourceFilePath: String, fileType: String) {
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

            // 创建目标文件
            val targetFile = File(targetFolder, sourceFile.name)

            // 移动文件
            val moveSuccess = if (sourceFile.renameTo(targetFile)) {
                 Log.d(TAG, "文件移动成功: ${targetFile.absolutePath}")
                true
            } else {
                // 如果重命名失败，尝试复制后删除
                sourceFile.copyTo(targetFile, overwrite = true)
                sourceFile.delete()
                 Log.d(TAG, "文件复制成功: ${targetFile.absolutePath}")
                true
            }

            // 文件移动成功后，立即上传到服务器
            if (moveSuccess) {
                Log.d(TAG, "========================================")
                Log.d(TAG, "🚀 开始上传文件到服务器...")
                Log.d(TAG, "   文件: ${targetFile.name}")
                Log.d(TAG, "   类型: $fileType")
                Log.d(TAG, "========================================")

                // 使用协程上传
                uploadScope.launch {  // 或 viewModelScope.launch
                    minioUploader.uploadFile(
                        filePath = targetFile.absolutePath,
                        bucketName = BUCKET_NAME,
                        objectName = "dji/${targetFile.name}",
                        onSuccess = { fileUrl: String ->  // 明确指定回调的参数类型为 String
                            Log.d(TAG, "✅ 文件上传成功: ${targetFile.name}")
                            Log.d(TAG, "   访问地址: $fileUrl")
                            // TODO: 可以将 fileUrl 保存到数据库或发送给后端
                        },
                        onFailure = { error: String ->  // 明确指定回调的参数类型为 String
                            Log.d(TAG, "❌ 文件上传失败: ${targetFile.name}, 错误: $error")
                            // TODO: 可以实现失败重试机制
                        }
                    )
                }
            }
        } catch (e: Exception) {
            Log.d(TAG, "移动文件失败: ${e.message}")
        }
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

         Log.d(TAG, "========================================")
         Log.d(TAG, "📸 准备拍照")
         Log.d(TAG, "🔄 步骤1: 切换相机模式到拍照模式...")
         Log.d(TAG, "========================================")


        // 先切换到拍照模式
        KeyManager.getInstance().setValue(keyCameraMode, CameraMode.PHOTO_NORMAL, object : CommonCallbacks.CompletionCallback {
            override fun onSuccess() {
                 Log.d(TAG, "✅ 相机模式已切换到拍照模式")
                 Log.d(TAG, "🔄 步骤2: 开始拍照...")

                KeyManager.getInstance().performAction(keyStartShootPhoto, object : CommonCallbacks.CompletionCallbackWithParam<EmptyMsg> {
                    override fun onSuccess(t: EmptyMsg) {
                         Log.d(TAG, "========================================")
                         Log.d(TAG, "📸 拍照命令执行成功")
                         Log.d(TAG, "========================================")
                        response.message = "拍照命令执行成功"
                        response.result = "TRUE"
                        sendResponse(response)

                        // 拍照成功后，延迟2秒刷新文件列表（给相机时间保存文件）
                        android.os.Handler(android.os.Looper.getMainLooper()).postDelayed({
                             Log.d(TAG, "🔄 拍照完成，开始刷新文件列表...")
                            pullMediaFileList()
                        }, 2000)
                    }

                    override fun onFailure(error: IDJIError) {
                        Log.d(TAG, "========================================")
                        Log.d(TAG, "❌ 拍照失败")
                        Log.d(TAG, "   错误描述: ${error.description()}")
                        Log.d(TAG, "   错误码: ${error.errorCode()}")
                        Log.d(TAG, "========================================")
                        response.message = "拍照失败: ${error.description() ?: "未知错误"}"
                        response.result = "FALSE"
                        sendResponse(response)
                    }
                })
            }

            override fun onFailure(error: IDJIError) {
                Log.d(TAG, "========================================")
                Log.d(TAG, "❌ 切换相机模式到拍照模式失败")
                Log.d(TAG, "   错误描述: ${error.description()}")
                Log.d(TAG, "   错误码: ${error.errorCode()}")
                Log.d(TAG, "========================================")
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

        KeyManager.getInstance().performAction(keyStartRecord, object : CommonCallbacks.CompletionCallbackWithParam<EmptyMsg> {
            override fun onSuccess(t: EmptyMsg) {
                 Log.d(TAG, "开始录像命令执行成功")
                isRecording = true
                response.message = "开始录像命令执行成功"
                response.result = "TRUE"
                sendResponse(response)
                // 注意：录像文件会在停止录像后生成，实际文件生成后会在MediaFileListStateListener中处理
            }

            override fun onFailure(error: IDJIError) {
                Log.d(TAG, "开始录像失败: ${error.description()}")
                response.message = "开始录像失败: ${error.description()}"
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

        KeyManager.getInstance().performAction(keyStopRecord, object : CommonCallbacks.CompletionCallbackWithParam<EmptyMsg> {
            override fun onSuccess(t: EmptyMsg) {
                 Log.d(TAG, "========================================")
                 Log.d(TAG, "🎥 停止录像命令执行成功")
                 Log.d(TAG, "========================================")
                isRecording = false
                response.message = "停止录像命令执行成功"
                response.result = "TRUE"
                sendResponse(response)

                // 停止录像后，延迟3秒刷新文件列表（给相机时间保存视频文件）
                android.os.Handler(android.os.Looper.getMainLooper()).postDelayed({
                     Log.d(TAG, "🔄 录像停止，开始刷新文件列表...")
                    pullMediaFileList()
                }, 3000)
            }

            override fun onFailure(error: IDJIError) {
                Log.d(TAG, "❌ 停止录像失败: ${error.description()}")
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
         Log.d(TAG, "已清除已处理文件记录")
    }

    /**
     * 销毁资源
     */
    fun destroy() {
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

        Log.d(TAG, "✅ CameraService 已销毁")
    }
}
