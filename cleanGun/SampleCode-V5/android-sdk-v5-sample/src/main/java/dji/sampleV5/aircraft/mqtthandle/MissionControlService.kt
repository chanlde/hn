package dji.sampleV5.aircraft.mqtthandle

import android.util.Log
import com.dji.util.FileLogger
import dji.sdk.keyvalue.key.FlightControllerKey
import dji.sdk.keyvalue.key.KeyTools
import dji.v5.common.callback.CommonCallbacks
import dji.v5.common.error.IDJIError
import dji.v5.manager.KeyManager
import dji.v5.manager.aircraft.waypoint3.WaypointMissionManager
import dji.v5.manager.aircraft.waypoint3.model.BreakPointInfo

/**
 * 任务控制服务
 * 职责：处理航线任务的暂停、恢复、停止
 */
class MissionControlService {
    companion object {
        private const val TAG = "MissionControlService"
        private const val ALTITUDE_THRESHOLD_METERS = 2.0  // 高度阈值：2米，超过认为在空中
    }

    /**
     * 暂停任务
     */
    fun pauseMission(
        onSuccess: () -> Unit,
        onFailure: (String) -> Unit
    ) {
        try {
            Log.d(TAG, "暂停任务")

            WaypointMissionManager.getInstance().pauseMission(object : CommonCallbacks.CompletionCallback {
                override fun onSuccess() {
                    Log.d(TAG, "暂停任务成功")
                    onSuccess()
                }
                override fun onFailure(error: IDJIError) {
                    Log.e(TAG, "暂停任务失败: ${error.description()}")
                    onFailure(error.description())
                }
            })

        } catch (e: Exception) {
            Log.e(TAG, "暂停任务失败: ${e.message}", e)
            onFailure("暂停任务异常: ${e.message}")
        }
    }

    /**
     * 恢复任务
     */
    fun resumeMission(
        onSuccess: () -> Unit,
        onFailure: (String) -> Unit
    ) {
        try {
            Log.d(TAG, "恢复任务")

            // TODO: 调用 MSDK 恢复航线
            // WaypointMissionManager.getInstance().resumeMission(object : CommonCallbacks.CompletionCallback {
            //     override fun onSuccess() {
            //         Log.d(TAG, "恢复任务成功")
            //         onSuccess()
            //     }
            //     override fun onFailure(error: IDJIError) {
            //         Log.e(TAG, "恢复任务失败: ${error.description()}")
            //         onFailure(error.description())
            //     }
            // })

            // 临时模拟成功
            onSuccess()

        } catch (e: Exception) {
            Log.e(TAG, "恢复任务失败: ${e.message}", e)
            onFailure("恢复任务异常: ${e.message}")
        }
    }

    /**
     * 停止任务
     * @param missionFileName 任务文件名（必需，不能为空）
     */
    fun stopMission(
        missionFileName: String,
        onSuccess: () -> Unit,
        onFailure: (String) -> Unit
    ) {
        try {
            Log.d(TAG, "停止任务: $missionFileName")

            WaypointMissionManager.getInstance().stopMission(
                missionFileName,
                object : CommonCallbacks.CompletionCallback {
                    override fun onSuccess() {
                        Log.d(TAG, "停止任务成功")
                        onSuccess()
                    }
                    override fun onFailure(error: IDJIError) {
                        Log.e(TAG, "停止任务失败: ${error.description()}")
                        onFailure("停止任务失败: ${error.description()}")
                    }
                }
            )

        } catch (e: Exception) {
            Log.e(TAG, "停止任务失败: ${e.message}", e)
            onFailure("停止任务异常: ${e.message}")
        }
    }
    
    /**
     * 查询飞机是否在空中
     * @param callback 回调函数，返回 true 表示在空中，false 表示在地面，null 表示查询失败
     */
    private fun isAircraftInAir(callback: (Boolean?) -> Unit) {
        try {
            // 方法1: 查询飞行状态
            val keyIsFlying = KeyTools.createKey(FlightControllerKey.KeyIsFlying)
            KeyManager.getInstance().getValue(keyIsFlying, object : CommonCallbacks.CompletionCallbackWithParam<Boolean> {
                override fun onSuccess(isFlying: Boolean?) {
                    if (isFlying != null) {
                        Log.d(TAG, "查询飞行状态: isFlying=$isFlying")
                        callback(isFlying)
                        return
                    }
                    
                    // 如果飞行状态查询失败，尝试查询高度
                    queryAircraftAltitude(callback)
                }
                
                override fun onFailure(error: IDJIError) {
                    Log.w(TAG, "查询飞行状态失败: ${error.description()}，尝试查询高度")
                    queryAircraftAltitude(callback)
                }
            })
        } catch (e: Exception) {
            Log.e(TAG, "查询飞机状态异常: ${e.message}", e)
            callback(null)  // 查询失败，返回 null
        }
    }
    
    /**
     * 查询飞机高度（备用方法）
     */
    private fun queryAircraftAltitude(callback: (Boolean?) -> Unit) {
        try {
            val keyAltitude = KeyTools.createKey(FlightControllerKey.KeyAircraftLocation3D)
            KeyManager.getInstance().getValue(keyAltitude, object : CommonCallbacks.CompletionCallbackWithParam<dji.sdk.keyvalue.value.common.LocationCoordinate3D> {
                override fun onSuccess(location: dji.sdk.keyvalue.value.common.LocationCoordinate3D?) {
                    if (location != null && location.altitude != null) {
                        val altitude = location.altitude
                        val isInAir = altitude > ALTITUDE_THRESHOLD_METERS
                        Log.d(TAG, "查询飞机高度: altitude=$altitude 米，判断结果: ${if (isInAir) "空中" else "地面"}")
                        callback(isInAir)
                    } else {
                        Log.w(TAG, "飞机高度信息为空")
                        callback(null)  // 高度信息不可用，返回 null
                    }
                }
                
                override fun onFailure(error: IDJIError) {
                    Log.e(TAG, "查询飞机高度失败: ${error.description()}")
                    callback(null)  // 查询失败，返回 null
                }
            })
        } catch (e: Exception) {
            Log.e(TAG, "查询飞机高度异常: ${e.message}", e)
            callback(null)
        }
    }

    /**
     * 从断点恢复任务（空中恢复）
     * 使用 resumeMission(BreakPointInfo) 在不退出航线任务的前提下从断点恢复
     * @param missionFileName 任务文件名
     * @param breakPointInfo 断点信息
     */
    private fun resumeMissionFromBreakpointInAir(
        missionFileName: String,
        breakPointInfo: BreakPointInfo,
        onSuccess: () -> Unit,
        onFailure: (String) -> Unit
    ) {
        try {
            Log.d(TAG, "执行空中恢复任务（resumeMission）...")

            // 使用断点信息恢复任务（不退出航线任务）
            WaypointMissionManager.getInstance().resumeMission(
                breakPointInfo,
                object : CommonCallbacks.CompletionCallback {
                    override fun onSuccess() {
                        Log.d(TAG, "空中恢复任务成功")
                        onSuccess()
                    }

                    override fun onFailure(error: IDJIError) {
                        Log.e(TAG, "空中恢复任务失败: ${error.description()}")
                        onFailure("空中恢复任务失败: ${error.description()}")
                    }
                }
            )
        } catch (e: Exception) {
            Log.e(TAG, "空中恢复任务异常: ${e.message}", e)
            onFailure("空中恢复任务异常: ${e.message}")
        }
    }

    /**
     * 从断点恢复任务（地面重启）
     * 先调用 stopMission，然后使用 startMission(String missionFileName, BreakPointInfo breakPointInfo, ...)
     * @param missionFileName 任务文件名
     * @param breakPointInfo 断点信息
     */
    private fun resumeMissionFromBreakpointOnGround(
        missionFileName: String,
        breakPointInfo: BreakPointInfo,
        onSuccess: () -> Unit,
        onFailure: (String) -> Unit
    ) {
        try {
            Log.d(TAG, "执行地面重启任务（stopMission + startMission）...")

            // 第一步：先停止任务
            WaypointMissionManager.getInstance().stopMission(
                missionFileName,
                object : CommonCallbacks.CompletionCallback {
                    override fun onSuccess() {
                        Log.d(TAG, "停止任务成功，开始从断点启动任务...")

                        // 第二步：使用断点信息启动任务
                        WaypointMissionManager.getInstance().startMission(
                            missionFileName,
                            breakPointInfo,
                            object : CommonCallbacks.CompletionCallback {
                                override fun onSuccess() {
                                    Log.d(TAG, "地面重启任务成功")
                                    onSuccess()
                                }

                                override fun onFailure(error: IDJIError) {
                                    Log.e(TAG, "地面重启任务失败: ${error.description()}")
                                    onFailure("地面重启任务失败: ${error.description()}")
                                }
                            }
                        )
                    }

                    override fun onFailure(error: IDJIError) {
                        Log.e(TAG, "停止任务失败: ${error.description()}")
                        onFailure("停止任务失败: ${error.description()}")
                    }
                }
            )
        } catch (e: Exception) {
            Log.e(TAG, "地面重启任务异常: ${e.message}", e)
            onFailure("地面重启任务异常: ${e.message}")
        }
    }

    /**
     * 从断点恢复任务（自动选择方式）
     * 根据飞机状态自动选择使用空中恢复（resumeMission）或地面重启（stopMission + startMission）
     * @param missionFileName 任务文件名（必需，不能为空）
     */
    fun resumeMissionFromBreakpoint(
        missionFileName: String? = null,
        onSuccess: () -> Unit,
        onFailure: (String) -> Unit
    ) {
        try {
            FileLogger.i(TAG, "开始从断点恢复任务（自动选择方式）...")

            // 验证文件名不能为空
            if (missionFileName.isNullOrBlank()) {
                val errorMsg = "任务文件名为空，无法查询断点信息"
                Log.e(TAG, errorMsg)
                onFailure(errorMsg)
                return
            }

            Log.d(TAG, "使用任务文件名: $missionFileName，开始查询断点信息和飞机状态...")

            // 第一步：查询断点信息
            WaypointMissionManager.getInstance().queryBreakPointInfoFromAircraft(
                missionFileName,
                object : CommonCallbacks.CompletionCallbackWithParam<BreakPointInfo> {
                    override fun onSuccess(breakPointInfo: BreakPointInfo?) {
                        if (breakPointInfo == null) {
                            Log.e(TAG, "查询断点信息为空")
                            onFailure("未找到断点信息")
                            return
                        }

                        // 打印详细的断点信息
                        try {
                            val waylineID = breakPointInfo.waylineID
                            val waypointID = breakPointInfo.waypointID
                            val progress = breakPointInfo.segmentProgress
                            val location = breakPointInfo.location
                            val recoverActionType = breakPointInfo.recoverActionType

                            FileLogger.i(TAG, """
                            === 断点信息详情 ===
                            对象: $breakPointInfo
                            航线ID (waylineID): $waylineID
                            航点ID (waypointID): $waypointID
                            段进度 (segmentProgress): $progress
                            位置 (location): $location
                              - 纬度: ${location?.latitude}
                              - 经度: ${location?.longitude}
                              - 高度: ${location?.altitude}
                            恢复动作类型 (recoverActionType): $recoverActionType
                            ====================
                        """.trimIndent())
                        } catch (e: Exception) {
                            Log.w(TAG, "打印断点信息异常: ${e.message}")
                            Log.d(TAG, "断点信息对象: $breakPointInfo")
                        }

                        Log.d(TAG, "查询断点信息成功: $breakPointInfo")

                        // 第二步：查询飞机状态，根据状态自动选择恢复方式
                        isAircraftInAir { isInAir ->
                            when (isInAir) {
                                true -> {
                                    // 飞机在空中，使用空中恢复（resumeMission）
                                    FileLogger.i(TAG, "检测到飞机在空中，使用空中恢复方式（resumeMission）")
                                    resumeMissionFromBreakpointInAir(
                                        missionFileName,
                                        breakPointInfo,
                                        onSuccess,
                                        onFailure
                                    )
                                }
                                false -> {
                                    // 飞机在地面，使用地面重启（stopMission + startMission）
                                    FileLogger.i(TAG, "检测到飞机在地面，使用地面重启方式（stopMission + startMission）")
                                    resumeMissionFromBreakpointOnGround(
                                        missionFileName,
                                        breakPointInfo,
                                        onSuccess,
                                        onFailure
                                    )
                                }
                                null -> {
                                    // 无法查询飞机状态，默认使用地面重启方式（更安全）
                                    FileLogger.w(TAG, "无法查询飞机状态，默认使用地面重启方式（更安全）")
                                    resumeMissionFromBreakpointOnGround(
                                        missionFileName,
                                        breakPointInfo,
                                        onSuccess,
                                        onFailure
                                    )
                                }
                            }
                        }
                    }

                    override fun onFailure(error: IDJIError) {
                        FileLogger.e(TAG, "查询断点信息失败: ${error.description()}")
                        onFailure("查询断点信息失败: ${error.description()}")
                    }
                }
            )

        } catch (e: Exception) {
            FileLogger.e(TAG, "从断点恢复任务异常: ${e.message}", e)
            onFailure("从断点恢复任务异常: ${e.message}")
        }
    }
}