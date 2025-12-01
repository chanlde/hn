package dji.sampleV5.aircraft.log

import android.util.Log
import androidx.lifecycle.MutableLiveData
import androidx.lifecycle.ViewModel

object LogManager {

    // 使用 LiveData 来管理日志数据
    private val _logData = MutableLiveData<MutableList<String>>(mutableListOf())
    val logData: MutableLiveData<MutableList<String>> = _logData

    // 用于捕捉日志并将其添加到 LiveData
    fun log(tag: String, message: String) {
        val currentLogs = _logData.value ?: mutableListOf()
        currentLogs.add("[$tag]: $message")  // 记录日志
        _logData.postValue(currentLogs)  // ← 改成 postValue，可以在任何线程调用

        // 打印到 Logcat，方便开发时查看
        Log.d(tag, message)
    }
}