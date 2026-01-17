package dji.sampleV5.aircraft.mqtthandle

import android.util.Log
import com.amap.api.map3d.R
import com.dji.network.MQTTConfig
import com.dji.util.FileLogger
import com.tji.network.MqttManager
import dji.sdk.keyvalue.value.flightcontroller.AirSenseAirplaneState
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch

class FlightDataReport(key: String) {

    private var flightDataReportJob: Job? = null
    private val deviceManager = DeviceDataManager()
    private var flightAlarmDataReportJob: Job? = null
    private  var key: String

    init {
        this.key = key
        FileLogger.i("FlightDataReport", "初始化飞行数据上报服务, key=$key")
        startFlightDataReport()
    }

    // 启动周期性上报任务
    private fun startFlightDataReport() {
        // 只在没有运行的情况下启动新的任务
        if (flightDataReportJob?.isActive != true) {
            FileLogger.i("FlightDataReport", "启动协程任务 - 飞行数据上报")
            flightDataReportJob = CoroutineScope(Dispatchers.IO).launch {
                FileLogger.thread("FlightDataReport", "协程启动 - 飞行数据上报循环")
                try {
                    var loopCount = 0
                    while (true) { // 每秒上报 5 次
                        loopCount++
                        try {
                            // 更新所有设备数据
                            deviceManager.updateAllData()

                            // 获取上报格式的 JSON 字符串
                            val jsonData = deviceManager.getFlightReportData().toJsonString()

                            // 发布到 MQTT 服务器
                            MqttManager.getInstance().publish(
                                topic = "/api/machine/uav/info_$key",  // 主题
                                message = jsonData,  // 消息内容
                                onSuccess = {
                                    if (loopCount % 50 == 0) {  // 每50次记录一次成功
                                        FileLogger.d("FlightDataReport", "MQTT发布成功 #$loopCount")
                                    }
                                },
                                onError = { throwable ->
                                    FileLogger.e("FlightDataReport", "MQTT发布失败: ${throwable.message}", throwable)
                                }
                            )
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
        FileLogger.thread("FlightDataReport", "销毁飞行数据上报服务 - destroy")
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
