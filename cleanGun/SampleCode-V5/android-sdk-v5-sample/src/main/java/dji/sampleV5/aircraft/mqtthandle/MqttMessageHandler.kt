package dji.sampleV5.aircraft.mqtthandle

import android.R
import android.R.id.message
import android.content.Context
import android.location.Location
import android.util.Log
import dji.sampleV5.aircraft.DJIApplication
import dji.sampleV5.aircraft.manager.LocationService
import androidx.lifecycle.MutableLiveData
import com.dji.util.FileLogger
import com.dji.wpmzsdk.common.utils.kml.model.Location2D
import com.dji.wpmzsdk.common.utils.kml.model.LocationCoordinate3D
import com.google.gson.Gson
import dji.sampleV5.aircraft.data.MissionUploadStateInfo
import org.json.JSONObject
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
import java.io.File
import kotlin.jvm.java

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

        // 将下发的任务ID设置到 CameraService
        if (request.taskId.isNotEmpty()) {
            cameraService.setTaskId(request.taskId)
            Log.d(TAG, "设置航线任务ID到 CameraService: ${request.taskId}")
        }
        
        // 保存任务文件名，用于断点续飞
        // 使用 request.key 构造文件名，与实际上传的文件名一致（FileDownloader 使用 request.key + ".kmz"）
        // 在 startMission 中使用 FileUtils.getFileName 会去掉扩展名，所以这里也保存不带扩展名的文件名
        if (request.key.isNotEmpty()) {
            currentMissionFileName = request.key  // 例如: "WH_001_UAV_001"，与 startMission 中的 missionId 一致
            Log.d(TAG, "保存任务文件名: $currentMissionFileName (来源: request.key，实际上传的文件名: ${currentMissionFileName}.kmz)")
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
            FileLogger.d(TAG, "⚠️ 收到响应消息或无效消息，忽略: $request")
            return  // 这是响应消息，不处理
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
            Log.d(TAG, "保存遥控器位置: lat=$finalLatitude, lon=$finalLongitude")
        } else {
            // 如果经纬度为空或无效，从 GPS 获取
            Log.d(TAG, "MQTT 消息中的经纬度无效，尝试从 GPS 获取位置")
            val locationService = (context.applicationContext as? DJIApplication)?.getLocationService()
            val gpsLocation = locationService?.getLastLocation()
            
            if (gpsLocation != null) {
                finalLatitude = gpsLocation.latitude
                finalLongitude = gpsLocation.longitude
                Log.d(TAG, "使用 GPS 位置: lat=$finalLatitude, lon=$finalLongitude")
            } else {
                // GPS 位置也不可用，返回错误
                Log.e(TAG, "无法获取位置：MQTT 消息中的经纬度无效，且 GPS 位置不可用")
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

        Log.d(TAG, "设置返航点: lat=${location2D.latitude}, lon=${location2D.longitude}")

        // 调用飞行控制服务设置返航点
        flightControlService.setHomeLocation(location2D, response)
    }

    /**
     * 处理断点续飞指令
     */
    private fun handlePauseResumeMission(message: String, mqttManager: MqttManager, key: String) {
        // 如果消息包含 result 字段，说明是响应消息，直接忽略（避免循环处理）
        if (message.contains("\"result\"") || message.contains("result")) {
            Log.d(TAG, "收到响应消息，忽略处理")
            return
        }

        // 解析请求消息
        val request: PauseResumeMissionRequest? = try {
            message.fromJson<PauseResumeMissionRequest>()
        } catch (e: Exception) {
            Log.e(TAG, "解析断点续飞请求失败: ${e.message}")
            return
        }

        if (request == null) {
            Log.e(TAG, "断点续飞请求为空")
            return
        }

        // 检查 tid
        if (request.tid.isBlank()) {
            Log.e(TAG, "断点续飞请求缺少 tid 字段")
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

        Log.d(TAG, "处理断点续飞指令 - type: ${request.type}, tid: ${request.tid}")

        when (request.type) {
            0 -> {
                // 暂停任务
                Log.d(TAG, "执行暂停任务")
                missionControlService.pauseMission(
                    onSuccess = {
                        Log.d(TAG, "暂停任务成功")
                        response.message = "暂停任务成功"
                        response.result = "TRUE"
                        sendResponse(response)
                    },
                    onFailure = { error ->
                        Log.e(TAG, "暂停任务失败: $error")
                        response.message = error
                        response.result = "FALSE"
                        sendResponse(response)
                    }
                )
            }
             1 -> {
                // 从断点恢复任务
                Log.d(TAG, "执行从断点恢复任务")
                
                // 使用保存的任务文件名（从 TaskFileRequest 中获取）
                val missionFileName = currentMissionFileName
                
                if (missionFileName.isNullOrBlank()) {
                    Log.e(TAG, "无法获取任务文件名，无法从断点恢复任务（请先通过 setTaskFile 下发任务文件）")
                    response.message = "无法获取任务文件名，请先通过 setTaskFile 下发任务文件"
                    response.result = "FALSE"
                    sendResponse(response)
                }
                
                Log.d(TAG, "使用保存的任务文件名: $missionFileName")
                
                missionControlService.resumeMissionFromBreakpoint(
                    missionFileName = missionFileName,
                    onSuccess = {
                        Log.d(TAG, "从断点恢复任务成功")
                        response.message = "从断点恢复任务成功"
                        response.result = "TRUE"
                        sendResponse(response)
                    },
                    onFailure = { error ->
                        Log.e(TAG, "从断点恢复任务失败: $error")
                        response.message = error
                        response.result = "FALSE"
                        sendResponse(response)
                    }
                )
            }
            else -> {
                Log.w(TAG, "未知的断点续飞类型: ${request.type}")
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
            FileLogger.d(TAG, "⚠️ 收到响应消息或无效消息，忽略: $request")
            return  // 这是响应消息，不处理
        }

        // ✅ 检查 tid
        if (request.tid.isNullOrBlank()) {
            FileLogger.e(TAG, "❌ 控制命令缺少 tid 字段")
            return
        }

        val response = UavControlResponse(
            key = key,
            tid = request.tid,
            api = "$api_UavControl$key",
            message = "ok",
            result = "TRUE"
        )

        FileLogger.d(TAG, "📩 处理控制命令 - type: $requestType, tid: ${request.tid}")

        when (requestType) {
            1 -> handleTakeoff(response)
            2 -> handleLand(response)
            3 -> handleReturnHome(response)
            4 -> handleTakePhoto(response)
            5 -> handleRecordVideo(request, response)
            else -> {
                FileLogger.w(TAG, "⚠️ 未知的控制类型: $requestType")
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