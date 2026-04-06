package dji.sampleV5.aircraft.mqtthandle

import android.content.Context
import dji.sampleV5.aircraft.DJIApplication
import dji.sampleV5.aircraft.manager.LocationService
import androidx.lifecycle.MutableLiveData
import com.dji.util.FileLogger
import com.google.gson.Gson
import dji.sampleV5.aircraft.data.MissionUploadStateInfo
import com.tji.network.MqttManager
import dji.sampleV5.aircraft.data.HomeLocation
import dji.sampleV5.aircraft.data.SetHomeLocationRequest
import dji.sampleV5.aircraft.data.UavControlRequest
import dji.sampleV5.aircraft.data.UavControlResponse
import dji.sampleV5.aircraft.data.PauseResumeMissionRequest

import dji.sampleV5.aircraft.util.fromJson
import dji.sampleV5.aircraft.util.sendResponse
import dji.sdk.keyvalue.value.common.LocationCoordinate2D
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.launch

/**
 * MQTT 消息处理器（重构后）
 * 职责：统一管理消息订阅、消息路由、消息解析
 * 不再直接调用 MSDK，而是委托给专门的服务类
 */
class MqttMessageHandler(
    private val taskService: TaskService,
    private val flightControlService: FlightControlService,
    private val missionControlService: MissionControlService,
    private val cameraService: CameraService,
    private val context: Context
//    private val gimbalService: GimbalService
) {
    companion object {
        private const val TAG = "MqttMessageHandler"
        private const val MQTT_PAYLOAD_PREVIEW_MAX = 256
        private const val api_TaskFile= "/api/work/setTaskFile_"
        private const val api_UavControl= "/api/machine/uav/control_"

        private const val api_SetHomeLocation= "/api/work/setHomeLocation_"
        private const val api_PauseResumeMission= "/api/work/pauseResumeMission_"
        private const val tid= "6a7bfe89-c386-4043-b600-b518e10096cc"

    }
    private val gson = Gson()
    
    // 保存遥控器位置（从 MQTT 消息接收到的经纬度）
    private var remoteControllerLocation: HomeLocation? = null
    
    // 保存当前任务文件名（从 TaskFileRequest 的 fileUrl 中提取）
    private var currentMissionFileName: String? = null

    private val uploadScope = CoroutineScope(SupervisorJob() + Dispatchers.IO)

    val missionUploadState = MutableLiveData<MissionUploadStateInfo>()

    /**
     * 初始化并订阅所有主题
     */
    fun setupSubscriptions(mqttManager: MqttManager, key: String) {
        // 订阅航线任务
        subscribeToTopic(mqttManager, "$api_TaskFile$key") { message ->
            handleTaskFile(message, mqttManager, key)
        }
        subscribeToTopic(mqttManager, "$api_SetHomeLocation$key") { message ->
            handleSetHomeLocation(message, mqttManager, key)
        }
        // 订阅断点续飞命令
        subscribeToTopic(mqttManager, "$api_PauseResumeMission$key") { message ->
            handlePauseResumeMission(message, mqttManager, key)
        }
        // 订阅UAV控制命令
        subscribeToTopic(mqttManager, "$api_UavControl$key") { message ->
            // 安全地尝试从JSON解析
            val request: UavControlRequest? = message.fromJson<UavControlRequest>()

            if (request != null) {
                // 如果解析成功，调用处理命令的方法
                handleControlCommand(request, key)
            } else {
                FileLogger.w(TAG, "UAV 控制消息无法解析 len=${message.length} ${mqttPayloadPreview(message)}")
            }
        }


    }

    private fun mqttPayloadPreview(raw: String): String {
        val t = raw.trim()
        return if (t.length <= MQTT_PAYLOAD_PREVIEW_MAX) t
        else t.take(MQTT_PAYLOAD_PREVIEW_MAX) + "…(总长度=${t.length})"
    }

    /*
     * 统一的主题订阅方法
     */
    private fun subscribeToTopic(
        mqttManager: MqttManager,
        topic: String,
        handler: (String) -> Unit
    ) {
        mqttManager.subscribe(
            topic = topic,
            onMessage = { message ->
                FileLogger.i(TAG, "MQTT 收到 topic=$topic len=${message.length} ${mqttPayloadPreview(message)}")
                try {
                    handler(message)
                } catch (e: Exception) {
                    FileLogger.e(TAG, "MQTT 消息处理异常 topic=$topic", e)
                }
            }
        )
    }

    // ==================== 消息处理方法 ====================

    private fun handleTaskFile(message: String, mqttManager: MqttManager,key: String) {

        val request = MessageParser.parseTaskFileRequest(message)

        if (request == null) {
            return
        }

        // 将下发的任务ID设置到 CameraService
        if (request.taskId.isNotEmpty()) {
            cameraService.setTaskId(request.taskId)
        }
        if (request.key.isNotEmpty()) {
            currentMissionFileName = request.key
        }
        if (request.taskId.isNotEmpty() || request.key.isNotEmpty()) {
            FileLogger.i(TAG, "setTaskFile taskId=${request.taskId} missionKey=${request.key}")
        }

        val response = UavControlResponse(
            key = key,
            tid = request.tid,
            api = "$api_TaskFile$key",
            message = "ok",
            result = "TRUE"
        )

        taskService.uploadWaypointMission(

            request = request,
            onProgress = { progress ->
                missionUploadState.postValue(MissionUploadStateInfo(updateProgress = progress))
            },
            onSuccess = { successMessage ->
                missionUploadState.postValue(MissionUploadStateInfo(tips = successMessage))
                sendResponse(response)
            },
            onFailure = { errorMessage ->
                missionUploadState.postValue(MissionUploadStateInfo(tips = errorMessage))
                response.result = "FALSE"
                response.message = errorMessage

                sendResponse(response)
            }
        )
    }

    private fun handleSetHomeLocation(message: String, mqttManager: MqttManager, key: String) {
        // 解析返回的 JSON 字符串
        val request: SetHomeLocationRequest? = message.fromJson<SetHomeLocationRequest>()
        val requestType = request?.homeLocation

        if (requestType == null ) {
            FileLogger.throttledD(TAG, "ignoreSetHomeInvalid", "setHomeLocation 非请求或无效消息，已忽略", 30_000L)
            return
        }


        val homeLocation = request?.homeLocation
        var finalLatitude: Double
        var finalLongitude: Double
        
        // 判断经纬度是否有效（不为空且不为0）
        val hasValidLatitude = homeLocation?.latitude != null && homeLocation.latitude != 0.0
        val hasValidLongitude = homeLocation?.longitude != null && homeLocation.longitude != 0.0
        
        if (hasValidLatitude && hasValidLongitude) {
            // 保存遥控器位置
            finalLatitude = homeLocation!!.latitude
            finalLongitude = homeLocation!!.longitude
            remoteControllerLocation = HomeLocation(finalLongitude, finalLatitude)
            FileLogger.i(TAG, "setHomeLocation 使用MQTT坐标 lat=$finalLatitude lon=$finalLongitude")
        } else {
            val locationService = (context.applicationContext as? DJIApplication)?.getLocationService()
            val gpsLocation = locationService?.getLastLocation()
            
            if (gpsLocation != null) {
                finalLatitude = gpsLocation.latitude
                finalLongitude = gpsLocation.longitude
                FileLogger.i(TAG, "setHomeLocation 使用本机GPS 替代无效MQTT坐标 lat=$finalLatitude lon=$finalLongitude")
            } else {
                FileLogger.e(TAG, "setHomeLocation 失败：MQTT坐标无效且GPS不可用", null)
                val errorResponse = UavControlResponse(
                    key = key,
                    tid = request?.tid ?: "",
                    api = "$api_SetHomeLocation$key",
                    message = "无法获取位置：MQTT 消息中的经纬度无效，且 GPS 位置不可用",
                    result = "FALSE"
                )
                sendResponse(errorResponse)
                return
            }
        }
        
        // 创建 UAV 控制响应
        val response = UavControlResponse(
            key = key,
            tid = request?.tid ?: "",
            api = "$api_SetHomeLocation$key",
            message = "ok",
            result = "TRUE"
        )

        // 创建 2D 位置对象
        val location2D = LocationCoordinate2D(0.0, 0.0)

        // 设置经纬度
        location2D.latitude = finalLatitude
        location2D.longitude = finalLongitude

        FileLogger.i(TAG, "setHomeLocation 执行 lat=${location2D.latitude} lon=${location2D.longitude}")

        // 调用飞行控制服务设置返航点
        flightControlService.setHomeLocation(location2D, response)
    }

    /**
     * 处理断点续飞指令
     */
    private fun handlePauseResumeMission(message: String, mqttManager: MqttManager, key: String) {
        // 如果消息包含 result 字段，说明是响应消息，直接忽略（避免循环处理）
        if (message.contains("\"result\"") || message.contains("result")) {
            return
        }

        // 解析请求消息
        val request: PauseResumeMissionRequest? = try {
            message.fromJson<PauseResumeMissionRequest>()
        } catch (e: Exception) {
            FileLogger.e(TAG, "解析 pauseResumeMission 失败: ${e.message}", e)
            return
        }

        if (request == null) {
            FileLogger.e(TAG, "pauseResumeMission 请求为空", null)
            return
        }

        // 检查 tid
        if (request.tid.isBlank()) {
            FileLogger.e(TAG, "pauseResumeMission 缺少 tid", null)
            return
        }

        // 创建响应对象
        val response = UavControlResponse(
            key = key,
            tid = request.tid,
            api = "$api_PauseResumeMission$key",
            message = "ok",
            result = "TRUE"
        )

        FileLogger.i(TAG, "pauseResumeMission type=${request.type} tid=${request.tid}")
        val missionName = currentMissionFileName

        if (missionName.isNullOrBlank()) {
            FileLogger.e(TAG, "pauseResumeMission 无任务文件名，请先 setTaskFile", null)
            response.message = "无法获取任务文件名，请先通过 setTaskFile 下发任务文件"
            response.result = "FALSE"
            sendResponse(response)
            return
        }

        when (request.type) {
            0 -> {
                missionControlService.pauseMission(
                    missionFileName = missionName,

                    onSuccess = {
                        FileLogger.i(TAG, "pauseResumeMission 暂停成功 mission=$missionName")
                        response.message = "暂停任务成功"
                        response.result = "TRUE"
                        sendResponse(response)
                    },
                    onFailure = { error ->
                        FileLogger.e(TAG, "pauseResumeMission 暂停失败: $error", null)
                        response.message = error
                        response.result = "FALSE"
                        sendResponse(response)
                    }
                )
            }
             1 -> {
                missionControlService.resumeMissionFromBreakpoint(
                    missionFileName = missionName,
                    onSuccess = {
                        FileLogger.i(TAG, "pauseResumeMission 断点恢复成功 mission=$missionName")
                        response.message = "从断点恢复任务成功"
                        response.result = "TRUE"
                        sendResponse(response)
                    },
                    onFailure = { error ->
                        FileLogger.e(TAG, "pauseResumeMission 断点恢复失败: $error", null)
                        response.message = error
                        response.result = "FALSE"
                        sendResponse(response)
                    }
                )
            }
            else -> {
                FileLogger.w(TAG, "pauseResumeMission 未知 type=${request.type}")
                response.message = "未知的断点续飞类型: ${request.type}"
                response.result = "FALSE"
                sendResponse(response)
            }
        }
    }

    /**
     * 处理控制指令
     */
    private fun handleControlCommand(
        request: UavControlRequest,
        key: String
    ) {
        // ✅ 判断是否是控制请求（有 type 字段）
        val requestType = request.type
        if (requestType == null || requestType == 0) {
            FileLogger.throttledD(TAG, "ignoreUavControlInvalid", "uav/control 响应或无效 type，已忽略", 30_000L)
            return
        }

        // ✅ 检查 tid
        if (request.tid.isNullOrBlank()) {
            FileLogger.e(TAG, "uav/control 缺少 tid", null)
            return
        }

        val response = UavControlResponse(
            key = key,
            tid = request.tid,
            api = "$api_UavControl$key",
            message = "ok",
            result = "TRUE"
        )

        FileLogger.i(TAG, "uav/control type=$requestType tid=${request.tid} param=${request.parameter}")

        when (requestType) {
            1 -> handleTakeoff(response)
            2 -> handleLand(response)
            3 -> handleReturnHome(response)
            4 -> handleTakePhoto(response)
            5 -> handleRecordVideo(request, response)
            6 -> handleMissionControl(request, response)
            7 -> handleCameraMode(request, response)
            9 -> handleControlAuthority(request, response)
            else -> {
                FileLogger.w(TAG, "uav/control 未知 type=$requestType")
                response.message = "未知的控制类型: $requestType"
                response.result = "FALSE"
                sendResponse(response)
            }
        }
    }

    private fun handleTakeoff(response:UavControlResponse) {
        flightControlService.takeoff(response)
    }

    private fun handleLand(response:UavControlResponse) {
        flightControlService.land(response)
    }

    private fun handleReturnHome(response:UavControlResponse) {
        flightControlService.returnHome(response)
    }
    
    /**
     * 处理拍照命令 (type = 4)
     */
    private fun handleTakePhoto(response: UavControlResponse) {
        cameraService.takePhoto(response)
    }

    /**
     * 处理录像命令 (type = 5)
     * parameter: 1=开始录像, 2=停止录像
     */
    private fun handleRecordVideo(request: UavControlRequest, response: UavControlResponse) {
        when (request.parameter) {
            2 -> cameraService.stopRecordVideo(response)
            1 -> cameraService.startRecordVideo(response)
            else -> {
                response.message = "无效的录像参数: ${request.parameter}"
                response.result = "FALSE"
                sendResponse(response)
            }
        }
    }

    /**
     * 处理航线控制命令 (type = 6)
     * parameter: 1=航线暂停, 2=航线继续, 3=终止并悬停, 4=终止并按航线策略返航
     */
    private fun handleMissionControl(request: UavControlRequest, response: UavControlResponse) {
        val missionFileName = currentMissionFileName
        if (missionFileName.isNullOrBlank()) {
            FileLogger.e(TAG, "航线控制无任务文件名，请先 setTaskFile", null)
            response.message = "无法获取任务文件名，请先通过 setTaskFile 下发任务文件"
            response.result = "FALSE"
            sendResponse(response)
            return
        }

        when (request.parameter) {
            1 -> {
                missionControlService.pauseMission(
                    missionFileName = missionFileName,
                    onSuccess = {
                        FileLogger.i(TAG, "航线控制 暂停成功 mission=$missionFileName")
                        response.message = "航线暂停成功"
                        response.result = "TRUE"
                        sendResponse(response)
                    },
                    onFailure = { error ->
                        FileLogger.e(TAG, "航线控制 暂停失败: $error", null)
                        response.message = error
                        response.result = "FALSE"
                        sendResponse(response)
                    }
                )
            }
            2 -> {
                missionControlService.resumeMissionFromBreakpoint(
                    missionFileName = missionFileName,
                    onSuccess = {
                        FileLogger.i(TAG, "航线控制 继续成功 mission=$missionFileName")
                        response.message = "航线继续成功"
                        response.result = "TRUE"
                        sendResponse(response)
                    },
                    onFailure = { error ->
                        FileLogger.e(TAG, "航线控制 继续失败: $error", null)
                        response.message = error
                        response.result = "FALSE"
                        sendResponse(response)
                    }
                )
            }
            3 -> {
                missionControlService.stopMission(
                    missionFileName = missionFileName,
                    onSuccess = {
                        FileLogger.i(TAG, "航线控制 终止悬停成功 mission=$missionFileName")
                        response.message = "终止并悬停成功"
                        response.result = "TRUE"
                        sendResponse(response)
                    },
                    onFailure = { error ->
                        FileLogger.e(TAG, "航线控制 终止悬停失败: $error", null)
                        response.message = error
                        response.result = "FALSE"
                        sendResponse(response)
                    }
                )
            }
            4 -> {
                missionControlService.stopMission(
                    missionFileName = missionFileName,
                    onSuccess = {
                        FileLogger.i(TAG, "航线控制 终止后返航 mission=$missionFileName")
                        flightControlService.returnHome(response)
                    },
                    onFailure = { error ->
                        FileLogger.e(TAG, "航线控制 终止失败: $error", null)
                        response.message = error
                        response.result = "FALSE"
                        sendResponse(response)
                    }
                )
            }
            else -> {
                FileLogger.w(TAG, "航线控制 未知 parameter=${request.parameter}")
                response.message = "未知的航线控制参数: ${request.parameter}"
                response.result = "FALSE"
                sendResponse(response)
            }
        }
    }

    /**
     * 处理相机模式命令 (type = 7)
     * parameter: 1=广角, 2=长焦, 3=红外
     */
    private fun handleCameraMode(request: UavControlRequest, response: UavControlResponse) {
        // 1=广角 (LEFT_OR_MAIN), 2=长焦 (RIGHT), 3=红外/第三路
        cameraService.switchActiveCameraByParameter(request.parameter, response)
    }

    /**
     * 处理控制权模式命令 (type = 9)
     * parameter: 1=控制权获取, 2=控制权释放
     */
    private fun handleControlAuthority(request: UavControlRequest, response: UavControlResponse) {
        // TODO: 实现控制权获取/释放
        // 需要调用 FlightControlService 的相关方法
        FileLogger.w(TAG, "控制权模式待实现 parameter=${request.parameter}")
        response.message = "控制权模式功能待实现"
        response.result = "FALSE"
        sendResponse(response)
    }

//    private fun handlePauseMission(message: String, mqttManager: MqttManager) {
//        val request = MessageParser.parseBaseRequest(message, "/api/mission/pause") ?: return
//
//        missionControlService.pauseMission(
//            onSuccess = {
//                sendResponse(mqttManager, request.key, request.tid, request.api, true)
//            },
//            onFailure = { error ->
//                sendResponse(mqttManager, request.key, request.tid, request.api, false, error)
//            }
//        )
//    }
//
//    private fun handleResumeMission(message: String, mqttManager: MqttManager) {
//        val request = MessageParser.parseBaseRequest(message, "/api/mission/resume") ?: return
//
//        missionControlService.resumeMission(
//            onSuccess = {
//                sendResponse(mqttManager, request.key, request.tid, request.api, true)
//            },
//            onFailure = { error ->
//                sendResponse(mqttManager, request.key, request.tid, request.api, false, error)
//            }
//        )
//    }
//
//    private fun handleStopMission(message: String, mqttManager: MqttManager) {
//        val request = MessageParser.parseBaseRequest(message, "/api/mission/stop") ?: return
//
//        missionControlService.stopMission(
//            onSuccess = {
//                sendResponse(mqttManager, request.key, request.tid, request.api, true)
//            },
//            onFailure = { error ->
//                sendResponse(mqttManager, request.key, request.tid, request.api, false, error)
//            }
//        )
//    }

//    private fun handleStartVideo(message: String, mqttManager: MqttManager) {
//        val request = MessageParser.parseBaseRequest(message, "/api/camera/startVideo") ?: return
//
//        cameraService.startVideo(
//            onSuccess = {
//                sendResponse(mqttManager, request.key, request.tid, request.api, true)
//            },
//            onFailure = { error ->
//                sendResponse(mqttManager, request.key, request.tid, request.api, false, error)
//            }
//        )
//    }
//
//    private fun handleStopVideo(message: String, mqttManager: MqttManager) {
//        val request = MessageParser.parseBaseRequest(message, "/api/camera/stopVideo") ?: return
//
//        cameraService.stopVideo(
//            onSuccess = {
//                sendResponse(mqttManager, request.key, request.tid, request.api, true)
//            },
//            onFailure = { error ->
//                sendResponse(mqttManager, request.key, request.tid, request.api, false, error)
//            }
//        )
//    }
//
//    private fun handleTakePhoto(message: String, mqttManager: MqttManager) {
//        val request = MessageParser.parseBaseRequest(message, "/api/camera/takePhoto") ?: return
//
//        cameraService.takePhoto(
//            onSuccess = {
//                sendResponse(mqttManager, request.key, request.tid, request.api, true)
//            },
//            onFailure = { error ->
//                sendResponse(mqttManager, request.key, request.tid, request.api, false, error)
//            }
//        )
//    }
//
//    private fun handleGimbalControl(message: String, mqttManager: MqttManager) {
//        val request = MessageParser.parseGimbalRequest(message) ?: return
//
//        gimbalService.control(
//            pitch = request.pitch,
//            roll = request.roll,
//            yaw = request.yaw,
//            onSuccess = {
//                sendResponse(mqttManager, request.key, request.tid, request.api, true)
//            },
//            onFailure = { error ->
//                sendResponse(mqttManager, request.key, request.tid, request.api, false, error)
//            }
//        )
//    }

}