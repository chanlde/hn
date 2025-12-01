package dji.sampleV5.aircraft.log

import androidx.lifecycle.ViewModel

class LogViewModel : ViewModel() {

    // 直接使用 LogManager 来获取日志
    val logData = LogManager.logData
}
