package dji.sampleV5.aircraft.mqtthandle

import android.R
import android.R.id.message
import android.content.Context
import android.util.Log
import androidx.lifecycle.MutableLiveData
import com.google.gson.Gson
import dji.sampleV5.aircraft.data.MissionUploadStateInfo
import org.json.JSONObject
import com.tji.network.MqttManager
import dji.sampleV5.aircraft.data.UavControlRequest
import dji.sampleV5.aircraft.data.UavControlResponse

import dji.sampleV5.aircraft.util.fromJson
import dji.sampleV5.aircraft.util.sendResponse
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.launch
import java.io.File

/**
 * MQTT 消息处理器（重构后）
 * 职责：统一管理消息订阅、消息路由、消息解析
 * 不再直接调用 MSDK，而是委托给专门的服务类
 */
class MqttMessageHandler(
    private val taskService: TaskService,
    private val flightControlService: FlightControlService,
    private val missionControlService: MissionControlService,
    private val cameraService: CameraService
//    private val gimbalService: GimbalService
) {
    companion object {
        private const val TAG = "MqttMessageHandler"
        private const val api_TaskFile= "/api/work/setTaskFile_"
        private const val api_UavControl= "/api/machine/uav/control_"
        private const val tid= "6a7bfe89-c386-4043-b600-b518e10096cc"

    }
    private val gson = Gson()

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

        // 订阅UAV控制命令
        subscribeToTopic(mqttManager, "$api_UavControl$key") { message ->
            // 安全地尝试从JSON解析
            val request: UavControlRequest? = message.fromJson<UavControlRequest>()

            if (request != null) {
                // 如果解析成功，调用处理命令的方法
                handleControlCommand(request, key)
            } else {
                // 如果解析失败，记录日志或者做其他处理
                Log.e("MqttSubscription", "无法解析消息: $message")

            }
        }
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
                Log.d(TAG, "📩 收到消息 - Topic: $topic")
                Log.d(TAG, "Message: $message")
                handler(message)
            }
        )
    }

    // ==================== 消息处理方法 ====================

    private fun handleTaskFile(message: String, mqttManager: MqttManager,key: String) {

        val request = MessageParser.parseTaskFileRequest(message)

        if (request == null) {
            return
        }

        var response = UavControlResponse(
            key = key,
            tid = tid,
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
    /**
     * 处理控制指令
     */
    private fun handleControlCommand(
        request: UavControlRequest,
        key: String
    ) {

        var response = UavControlResponse(
            key = key,
            tid = tid,
            api = "$api_UavControl$key",
            message = "ok",
            result = "TRUE"
        )
        Log.d(TAG, "📩 vvvvvvvvvvvvvvvvvvvvvv ${request.type}")

        when (request.type) {
            1 -> handleTakeoff(response)
            2 -> handleLand(response)
            3 -> handleReturnHome(response)
            4 -> handleTakePhoto(response)
            5 -> handleRecordVideo(request, response)
//            6 -> handleWaypointControl(request,key)
//            7 -> handleCameraMode(request,key)
//            9 -> handleControlAuth(request,key)

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
     * parameter: 0=停止录像, 1=开始录像
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