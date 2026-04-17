package dji.sampleV5.aircraft.mqtthandle

import android.os.Handler
import android.os.Looper
import com.dji.util.FileLogger
import dji.sdk.keyvalue.key.FlightControllerKey
import dji.sdk.keyvalue.key.KeyTools
import dji.sdk.keyvalue.value.flightcontroller.LowBatteryRTHInfo
import dji.sdk.keyvalue.value.flightcontroller.LowBatteryRTHState
import dji.v5.common.callback.CommonCallbacks
import dji.v5.common.error.IDJIError
import dji.v5.manager.KeyManager
import dji.v5.manager.aircraft.waypoint3.WaylineExecutingInfoListener
import dji.v5.manager.aircraft.waypoint3.WaypointMissionExecuteStateListener
import dji.v5.manager.aircraft.waypoint3.WaypointMissionManager
import dji.v5.manager.aircraft.waypoint3.model.WaylineExecutingInfo
import dji.v5.manager.aircraft.waypoint3.model.WaypointMissionExecuteState

/**
 * 航线任务状态：监听航点索引、任务执行状态、飞控重连与低电返航。
 * 任务真正结束（FINISHED -> READY）时做一次媒体列表补扫；不自动清除 [CameraService] 任务文件夹路径（仅显式 clear 或下次 set 覆盖）。
 */
class WaypointMissionStateManager(
    private val listenerTag: Any,
    private val lock: Any,
    private val getCameraService: () -> CameraService?,
    private val onWaypointIndexUpdate: (Int) -> Unit,
    /** 上层将 [DeviceDataManager] 的 currentTaskStatus 置为 10（须在 [lock] 外由调用方自行同步时，由本类在 synchronized(lock) 内调用） */
    private val onMissionInterruptTaskStatus10: () -> Unit,
    /** 任务结束或上层 [resetMissionState]：清空上报航点、taskStatus=0、降落辅助字段等（在 synchronized(lock) 内调用） */
    private val onResetReportingAfterMissionEnd: () -> Unit,
    private val getIsFlying: () -> Boolean?,
    private val getCurrentWaypointIndex: () -> Int?,
    private val getReportWaypointIndex: () -> Int?,
    private val getTaskStatusForLog: () -> Int,
) {

    companion object {
        private const val TAG = "WaypointMissionStateManager"
        private const val END_MISSION_PULL_DELAY_MS = 2000L

        /** 上层 [resetMissionState] 或日志用 */
        private const val RESET_REASON_MISSION_END = "missionEnd"
    }

    private var waylineExecutingInfoListener: WaylineExecutingInfoListener? = null
    private var waypointMissionExecuteStateListener: WaypointMissionExecuteStateListener? = null
    private var connectionListener: CommonCallbacks.KeyListener<Boolean>? = null
    private var lowBatteryRTHListener: CommonCallbacks.KeyListener<LowBatteryRTHInfo>? = null

    private val mainHandler = Handler(Looper.getMainLooper())
    private var endMissionPullRunnable: Runnable? = null

    @Volatile
    var currentWaypointMissionExecuteState: WaypointMissionExecuteState? = null
        private set

    @Volatile
    var isMissionInterrupted: Boolean = false
        private set

    @Volatile
    var isLowBatteryRTH: Boolean = false
        private set

    init {
        setupWaylineListener()
        setupWaypointMissionExecuteStateListener()
        setupFlightControllerConnectionListener()
        setupLowBatteryRTHListener()
    }

    private inner class WaylineExecutingInfoListenerImpl : WaylineExecutingInfoListener {
        override fun onWaylineExecutingInfoUpdate(info: WaylineExecutingInfo) {
            synchronized(lock) {
                val oldIdx = getCurrentWaypointIndex()
                onWaypointIndexUpdate(info.currentWaypointIndex)
                FileLogger.logStateChange(TAG, "currentWaypointIndex", info.currentWaypointIndex)
                FileLogger.w(
                    TAG,
                    "[DIAG-WPIDX] waypointIndex: $oldIdx -> ${info.currentWaypointIndex}" +
                        " | isFlying=${getIsFlying()}" +
                        " | missionState=${currentWaypointMissionExecuteState?.name}"
                )
            }
        }

        override fun onWaylineExecutingInterruptReasonUpdate(error: IDJIError) {
            synchronized(lock) {
                val wpAtInterrupt = getCurrentWaypointIndex()
                val msAtInterrupt = currentWaypointMissionExecuteState?.name
                isMissionInterrupted = true
                onMissionInterruptTaskStatus10()
                FileLogger.w(
                    TAG,
                    "[DIAG-WP-INTERRUPT] 航线中断瞬间 wpIdx=$wpAtInterrupt missionState=$msAtInterrupt isFlying=${getIsFlying()} -> taskStatus=10"
                )
            }
            try {
                val description = error.description() ?: "未知错误"
                val errorCode = error.errorCode()
                FileLogger.w(TAG, "航线执行中断: $description code=$errorCode -> taskStatus=10")
            } catch (e: Exception) {
                FileLogger.e(TAG, "处理航线中断错误时异常: ${e.message}", e)
            }
        }
    }

    private fun setupWaylineListener() {
        waylineExecutingInfoListener = WaylineExecutingInfoListenerImpl()
        waylineExecutingInfoListener?.let {
            WaypointMissionManager.getInstance().addWaylineExecutingInfoListener(it)
            FileLogger.i(TAG, "[DIAG-WP] WaylineExecutingInfoListener 已注册到 WaypointMissionManager")
        }
    }

    private fun removeWaylineListener() {
        waylineExecutingInfoListener?.let {
            val wp = synchronized(lock) { getCurrentWaypointIndex() }
            WaypointMissionManager.getInstance().removeWaylineExecutingInfoListener(it)
            waylineExecutingInfoListener = null
            FileLogger.w(TAG, "[DIAG-WP] WaylineExecutingInfoListener 已移除 | 移除前 wpIdx=$wp")
        }
    }

    private fun setupWaypointMissionExecuteStateListener() {
        waypointMissionExecuteStateListener = object : WaypointMissionExecuteStateListener {
            override fun onMissionStateUpdate(missionState: WaypointMissionExecuteState) {
                synchronized(lock) {
                    val previousState = currentWaypointMissionExecuteState
                    // 仅 FINISHED -> READY 视为航线任务完成；其它到 READY 不触发结束补扫
                    val wasFinished = previousState == WaypointMissionExecuteState.FINISHED
                    val willEndMissionScan = wasFinished && missionState == WaypointMissionExecuteState.READY
                    FileLogger.w(
                        TAG,
                        "[DIAG-STATE] missionState: ${previousState?.name} -> ${missionState.name}" +
                            " | t=${System.currentTimeMillis()}" +
                            " | isFlying=${getIsFlying()}" +
                            " | wpIdx=${getCurrentWaypointIndex()}" +
                            " | willEndMissionScan=$willEndMissionScan"
                    )
                    currentWaypointMissionExecuteState = missionState
                    FileLogger.logStateChange(TAG, "waypointMissionExecuteState", missionState.name)

                    if (wasFinished && missionState == WaypointMissionExecuteState.READY) {
                        FileLogger.i(
                            TAG,
                            "检测到任务结束 (FINISHED -> READY)：重置上报字段 + 延迟补扫媒体列表（不自动清任务路径）"
                        )
                        handleMissionFinishedLockedAlreadyHeld()
                    }
                }
            }
        }
        waypointMissionExecuteStateListener?.let {
            WaypointMissionManager.getInstance().addWaypointMissionExecuteStateListener(it)
            FileLogger.i(TAG, "[DIAG-WP] WaypointMissionExecuteStateListener 已注册到 WaypointMissionManager")
        }
    }

    private fun removeWaypointMissionExecuteStateListener() {
        waypointMissionExecuteStateListener?.let {
            val (wp, ms) = synchronized(lock) {
                getCurrentWaypointIndex() to currentWaypointMissionExecuteState?.name
            }
            WaypointMissionManager.getInstance().removeWaypointMissionExecuteStateListener(it)
            waypointMissionExecuteStateListener = null
            FileLogger.w(TAG, "[DIAG-WP] WaypointMissionExecuteStateListener 已移除 | 移除前 wpIdx=$wp missionState=$ms")
        }
    }

    /**
     * 飞控会话重建后（关机再开等），WaypointMissionManager 上已注册的监听器可能失效，需移除后重新注册。
     */
    fun reRegisterWaypointListeners() {
        FileLogger.i(TAG, "[DIAG-WP] 飞控重连：重新注册 WaylineExecutingInfo / WaypointMissionExecuteState 监听器")
        removeWaylineListener()
        removeWaypointMissionExecuteStateListener()
        setupWaylineListener()
        setupWaypointMissionExecuteStateListener()
    }

    private fun setupFlightControllerConnectionListener() {
        val fcConnectionKey = KeyTools.createKey(FlightControllerKey.KeyConnection)
        connectionListener = object : CommonCallbacks.KeyListener<Boolean> {
            override fun onValueChange(oldValue: Boolean?, newValue: Boolean?) {
                val wasConnected = oldValue == true
                val connected = newValue == true
                if (!wasConnected && connected) {
                    FileLogger.i(
                        TAG,
                        "[DIAG-WP] 飞控已连接 old=$oldValue new=$newValue，仅重新注册航线监听器（不重置任务状态、不清路径）"
                    )
                    reRegisterWaypointListeners()
                }
            }
        }
        connectionListener?.let {
            KeyManager.getInstance().listen(fcConnectionKey, listenerTag, it)
        }
    }

    private fun setupLowBatteryRTHListener() {
        val lowBatteryRTHKey = KeyTools.createKey(FlightControllerKey.KeyLowBatteryRTHInfo)
        lowBatteryRTHListener = object : CommonCallbacks.KeyListener<LowBatteryRTHInfo> {
            override fun onValueChange(oldValue: LowBatteryRTHInfo?, newValue: LowBatteryRTHInfo?) {
                synchronized(lock) {
                    val oldState = oldValue?.lowBatteryRTHStatus
                    val newState = newValue?.lowBatteryRTHStatus

                    when (newState) {
                        LowBatteryRTHState.COUNTING_DOWN,
                        LowBatteryRTHState.EXECUTED -> {
                            isMissionInterrupted = true
                            isLowBatteryRTH = true
                            onMissionInterruptTaskStatus10()
                        }
                        else -> {
                            isLowBatteryRTH = false
                        }
                    }

                    if (newState != oldState) {
                        FileLogger.logStateChange(TAG, "lowBatteryRTH", "$oldState->$newState lowBatRTH=$isLowBatteryRTH")
                    }
                }
            }
        }
        lowBatteryRTHListener?.let {
            KeyManager.getInstance().listen(lowBatteryRTHKey, listenerTag, it)
        }
    }

    private fun removeFlightControllerConnectionListener() {
        val fcConnectionKey = KeyTools.createKey(FlightControllerKey.KeyConnection)
        connectionListener?.let {
            KeyManager.getInstance().cancelListen(fcConnectionKey, it)
            connectionListener = null
        }
    }

    private fun removeLowBatteryRTHListener() {
        val lowBatteryRTHKey = KeyTools.createKey(FlightControllerKey.KeyLowBatteryRTHInfo)
        lowBatteryRTHListener?.let {
            KeyManager.getInstance().cancelListen(lowBatteryRTHKey, it)
            lowBatteryRTHListener = null
        }
    }

    fun cancelEndMissionDelayedTasks() {
        endMissionPullRunnable?.let { mainHandler.removeCallbacks(it) }
        endMissionPullRunnable = null
    }

    /**
     * 上层手动重置（如 [DeviceDataManager.resetMissionState]）：清上报字段 + 延迟补扫媒体；不自动清任务路径。
     */
    fun resetMissionState(resetReason: String = RESET_REASON_MISSION_END) {
        synchronized(lock) {
            resetMissionStateLockedAlreadyHeld(resetReason)
        }
    }

    /** 已在 [lock] 上同步时调用（例如 READY 回调内） */
    private fun resetMissionStateLockedAlreadyHeld(resetReason: String = RESET_REASON_MISSION_END) {
        cancelEndMissionDelayedTasks()

        val stack = Thread.currentThread().stackTrace
        val caller = stack.drop(2).take(3).joinToString(" <- ") { el ->
            "${el.className.substringAfterLast('.')}.${el.methodName}:${el.lineNumber}"
        }
        FileLogger.w(
            TAG,
            "[DIAG-RESET] >>> resetMissionState 被调用!" +
                " | wpIdx=${getCurrentWaypointIndex()}" +
                " | reportWpIdx=${getReportWaypointIndex()}" +
                " | isFlying=${getIsFlying()}" +
                " | missionState=${currentWaypointMissionExecuteState?.name}" +
                " | taskStatus=${getTaskStatusForLog()}" +
                " | isMissionInterrupted=$isMissionInterrupted" +
                " | caller=$caller" +
                " | resetReason=$resetReason"
        )

        clearMissionReportingLockedAlreadyHeld()
        FileLogger.i(TAG, "[DIAG-WP] resetMissionState 完成: 航点与任务字段已清空")
        FileLogger.i(TAG, "任务状态已重置: currentWaypointIndex=null, currentTaskStatus=0, isMissionInterrupted=false")

        scheduleEndMissionMediaPullLockedAlreadyHeld("resetMissionState")
    }

    /** 清空上报航点 / taskStatus / 内部航线状态位（不碰 CameraService 路径） */
    private fun clearMissionReportingLockedAlreadyHeld() {
        onResetReportingAfterMissionEnd()
        currentWaypointMissionExecuteState = null
        isMissionInterrupted = false
        isLowBatteryRTH = false
    }

    /** 延迟一次 end-mission 媒体列表补扫；不调用 [CameraService.clearMissionFolderPath] */
    private fun scheduleEndMissionMediaPullLockedAlreadyHeld(logPrefix: String) {
        val folderPathSnapshot = getCameraService()?.currentMissionFolderPath
        endMissionPullRunnable = Runnable {
            FileLogger.i(
                TAG,
                "$logPrefix pullMediaFileListForEndMission snapshotPath=$folderPathSnapshot（不自动清路径）"
            )
            getCameraService()?.pullMediaFileListForEndMission()
            endMissionPullRunnable = null
        }
        mainHandler.postDelayed(endMissionPullRunnable!!, END_MISSION_PULL_DELAY_MS)
    }

    /**
     * FINISHED -> READY：清空上报并延迟补扫媒体；不自动清任务路径。
     */
    private fun handleMissionFinishedLockedAlreadyHeld() {
        cancelEndMissionDelayedTasks()
        val pathSnap = getCameraService()?.currentMissionFolderPath
        FileLogger.i(
            TAG,
            "[DIAG-WP] 任务正常结束: missionFolderPathSnapshot=$pathSnap | ${END_MISSION_PULL_DELAY_MS}ms 后 pullMediaFileListForEndMission"
        )
        clearMissionReportingLockedAlreadyHeld()
        FileLogger.i(TAG, "[DIAG-WP] 任务结束: 上报字段已清空（保留任务文件夹路径）")
        scheduleEndMissionMediaPullLockedAlreadyHeld("任务结束")
        // 关闭 CameraService 任务会话：延迟关闭，给 pullMediaFileListForEndMission 的补扫留处理窗口
        getCameraService()?.endMissionSession("waypointFinished")
    }

    fun destroy() {
        cancelEndMissionDelayedTasks()
        synchronized(lock) {
            FileLogger.w(
                TAG,
                "[DIAG-WP] destroy 开始 | wpIdx=${getCurrentWaypointIndex()} mission=${currentWaypointMissionExecuteState?.name}"
            )
        }
        removeWaylineListener()
        removeWaypointMissionExecuteStateListener()
        removeFlightControllerConnectionListener()
        removeLowBatteryRTHListener()
        FileLogger.i(TAG, "WaypointMissionStateManager 已清理")
    }
}
