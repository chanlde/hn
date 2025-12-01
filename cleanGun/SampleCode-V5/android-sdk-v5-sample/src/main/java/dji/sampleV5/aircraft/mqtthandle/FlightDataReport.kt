package dji.sampleV5.aircraft.mqtthandle

import android.util.Log
import com.amap.api.map3d.R
import com.dji.network.MQTTConfig
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
        startFlightDataReport()
        this.key = key
    }

    // 启动周期性上报任务
    private fun startFlightDataReport() {
        // 只在没有运行的情况下启动新的任务
        if (flightDataReportJob?.isActive != true) {
            flightDataReportJob = CoroutineScope(Dispatchers.IO).launch {
                try {
                    while (true) { // 每秒上报 5 次

                        // 更新所有设备数据
                        deviceManager.updateAllData()

                        // 获取设备数据的 JSON 字符串
                        val jsonData = deviceManager.getData().toJsonString()

                        // 打印日志信息
                        Log.d("FlightDataReport", "Data to send: $jsonData")

                        // 发布到 MQTT 服务器
                        MqttManager.getInstance().publish(
                            topic = "/api/machine/uav/info_$key",  // 主题
                            message = jsonData,  // 消息内容
                            onSuccess = {
                                //Log.d("FlightDataReport", "Message successfully sent to MQTT server.")
                            },
                            onError = { throwable ->
                               // Log.e("FlightDataReport", "Error sending message: ${throwable.message}")
                            }
                        )
                        delay(200) // 每 200 毫秒上报一次，即每秒 5 次
                    }
                } catch (e: Exception) {
                    Log.e("FlightDataReport", "Error in flightDataReport: ${e.message}")
                }
            }
        }
    }

    private fun startFlightAlarmDataReport() {
        // 只在没有运行的情况下启动新的任务
        if (flightAlarmDataReportJob?.isActive != true) {
            flightAlarmDataReportJob = CoroutineScope(Dispatchers.IO).launch {
                try {
                    while (true) { // 每秒上报 5 次

                       var airplaneState=AirSenseAirplaneState()
                        // 发布到 MQTT 服务器
                        Log.d("FlightDataReport", "vvvvvvvvvvvvvvvvvvvv${airplaneState.getWarningLevel()}")

//                        MqttManager.getInstance().publish(
//                            topic = "/api/machine/uav/info_1581F8DBW255D00A2LD4",  // 主题
//                            message = jsonData,  // 消息内容
//                            onSuccess = {
//                                Log.d("FlightDataReport", "Message successfully sent to MQTT server.")
//                            },
//                            onError = { throwable ->
//                                Log.e("FlightDataReport", "Error sending message: ${throwable.message}")
//                            }
//                        )


                        delay(200) // 每 200 毫秒上报一次，即每秒 5 次
                    }
                } catch (e: Exception) {
                    Log.e("FlightDataReport", "Error in flightDataReport: ${e.message}")
                }
            }
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
        // 取消所有任务
        cancelFlightDataReport()
        
        // 清理设备管理器的监听器
        deviceManager.destroy()
        
        Log.d("FlightDataReport", "资源已清理")
    }
}
