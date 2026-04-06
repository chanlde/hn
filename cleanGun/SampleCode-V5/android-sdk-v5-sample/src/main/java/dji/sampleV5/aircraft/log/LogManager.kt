package dji.sampleV5.aircraft.log

import android.util.Log
import androidx.lifecycle.MutableLiveData

object LogManager {

    private const val MAX_LOG_LINES = 500
    private val repeatSuffix = Regex(" \\(×(\\d+)\\)$")

    private val _logData = MutableLiveData<MutableList<String>>(mutableListOf<String>())
    val logData: MutableLiveData<MutableList<String>> = _logData

    private fun baseLineOf(line: String): String {
        val m = repeatSuffix.find(line) ?: return line
        return line.removeRange(m.range)
    }

    private fun repeatCountOf(line: String): Int {
        val m = repeatSuffix.find(line) ?: return 1
        return m.groupValues[1].toIntOrNull() ?: 1
    }

    fun log(tag: String, message: String) {
        val entry = "[$tag]: $message"
        val current = (_logData.value ?: mutableListOf()).toMutableList()

        if (current.isNotEmpty()) {
            val last = current.last()
            if (baseLineOf(last) == entry) {
                current.removeAt(current.size - 1)
                val next = repeatCountOf(last) + 1
                current.add("$entry (×$next)")
            } else {
                current.add(entry)
            }
        } else {
            current.add(entry)
        }

        while (current.size > MAX_LOG_LINES) {
            current.removeAt(0)
        }
        _logData.postValue(current)
        Log.d(tag, message)
    }
}
