package dji.sampleV5.aircraft.mqtthandle

import android.util.Log
import com.dji.util.FileLogger
import com.google.gson.JsonParser
import com.tji.network.MqttManager
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch

class FlightDataReport(key: String, cameraService: CameraService? = null) {

    companion object {
        /** 空心帧告警最小间隔，避免 SDK 未就绪时日志刷屏 */
        private const val EMPTY_FRAME_WARN_INTERVAL_MS = 5_000L
    }

    private var flightDataReportJob: Job? = null
    private val deviceManager = DeviceDataManager()
    private var flightAlarmDataReportJob: Job? = null
    private  var key: String

    /** 上一次"空心帧"告警时间（毫秒），用于节流 WARN 日志 */
    @Volatile
    private var lastEmptyFrameWarnMs: Long = 0L

    init {
        this.key = key
        // 设置 CameraService 引用到 DeviceDataManager
        deviceManager.cameraService = cameraService
        FileLogger.i("FlightDataReport", "初始化飞行数据上报服务, key=$key, cameraService=${if (cameraService != null) "已设置" else "未设置"}")
        startFlightDataReport()
    }

    // 启动周期性上报任务
    private fun startFlightDataReport() {
        // 只在没有运行的情况下启动新的任务
        if (flightDataReportJob?.isActive != true) {
            FileLogger.i("FlightDataReport", "启动飞行数据上报循环")
            flightDataReportJob = CoroutineScope(Dispatchers.IO).launch {
                try {
                    var loopCount = 0
                    while (true) {
                        loopCount++
                        try {
                            deviceManager.updateAllData()
                            val jsonData = deviceManager.getFlightReportData().toJsonString()

                            if (isEmptyFrame(jsonData)) {
                                val now = System.currentTimeMillis()
                                if (now - lastEmptyFrameWarnMs > EMPTY_FRAME_WARN_INTERVAL_MS) {
                                    lastEmptyFrameWarnMs = now
                                    FileLogger.w(
                                        "FlightDataReport",
                                        "检测到空心帧（仅 tid），跳过本次上报 loop=$loopCount json=$jsonData"
                                    )
                                }
                            } else {
                                MqttManager.getInstance().publish(
                                    topic = "/api/machine/uav/info_$key",
                                    message = jsonData,
                                    onSuccess = {
                                        FileLogger.throttledD(
                                            "FlightDataReport",
                                            "mqttPublishOk",
                                            "MQTT 周期上报正常 loop=$loopCount",
                                            60_000L
                                        )
                                    },
                                    onError = { throwable ->
                                        FileLogger.e("FlightDataReport", "MQTT发布失败: ${throwable.message}", throwable)
                                    }
                                )
                                Log.d("FlightDataReport", "MQTT 周期上报正常 loop=$jsonData")
                            }
                            delay(200) // 每 200 毫秒上报一次，即每秒 5 次
                        } catch (e: Exception) {
                            FileLogger.e("FlightDataReport", "数据上报循环内异常: ${e.message}", e)
                            delay(1000)  // 出错后等待1秒再继续
                        }
                    }
                } catch (e: Exception) {
                    FileLogger.e("FlightDataReport", "飞行数据上报协程异常: ${e.message}", e)
                } finally {
                    FileLogger.i("FlightDataReport", "飞行数据上报协程结束")
                }
            }
        } else {
            FileLogger.w("FlightDataReport", "飞行数据上报任务已在运行，跳过启动")
        }
    }

    /**
     * 判断 JSON 是否为"空心帧"（仅包含 tid，没有任何遥测字段）。
     *
     * 当 SDK 尚未就绪、USB 闪断或 `KeyManager` 会话重建瞬间，`getValue` 会连续返回 null，
     * 经 [dji.sampleV5.aircraft.data.FlightReportData.toJsonString] 序列化后会得到 `{"tid":"..."}`，
     * 此时没有必要上报，跳过以减少后端"状态丢失"的误判。
     */
    private fun isEmptyFrame(json: String): Boolean {
        return try {
            val obj = JsonParser.parseString(json).asJsonObject
            obj.size() <= 1 // 只剩 tid
        } catch (e: Exception) {
            FileLogger.w("FlightDataReport", "空心帧检测解析失败，按非空心处理: ${e.message}")
            false
        }
    }

    // 可以根据需要添加取消任务的函数
    fun cancelFlightDataReport() {
        flightDataReportJob?.cancel()
        flightAlarmDataReportJob?.cancel()
    }
    
    /**
     * 销毁资源，停止所有任务并清理监听器
     * 
     * 注意：在不再使用时必须调用此方法，避免内存泄漏
     */
    fun destroy() {
        try {
            // 取消所有任务
            cancelFlightDataReport()
            FileLogger.i("FlightDataReport", "协程任务已取消")
            
            // 清理设备管理器的监听器
            deviceManager.destroy()
            FileLogger.i("FlightDataReport", "设备管理器已销毁")
            
            FileLogger.i("FlightDataReport", "资源已清理完成")
        } catch (e: Exception) {
            FileLogger.e("FlightDataReport", "销毁资源时发生异常", e)
        }
    }
}
