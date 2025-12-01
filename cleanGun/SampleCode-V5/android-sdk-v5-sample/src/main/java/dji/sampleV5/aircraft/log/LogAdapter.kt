package dji.sampleV5.aircraft.log

import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.widget.TextView
import androidx.recyclerview.widget.RecyclerView
import dji.sampleV5.aircraft.R
class LogAdapter(private var logList: List<String>) : RecyclerView.Adapter<LogAdapter.LogViewHolder>() {

    override fun onCreateViewHolder(parent: ViewGroup, viewType: Int): LogViewHolder {
        val view = LayoutInflater.from(parent.context).inflate(R.layout.item_log, parent, false)
        return LogViewHolder(view)
    }

    override fun onBindViewHolder(holder: LogViewHolder, position: Int) {
        val log = logList[position]
        holder.logText.text = log
    }

    override fun getItemCount(): Int = logList.size

    // ViewHolder 用于绑定每个日志项
    class LogViewHolder(itemView: View) : RecyclerView.ViewHolder(itemView) {
        val logText: TextView = itemView.findViewById(R.id.logText)
    }

    // 更新日志
    fun updateLogs(newLogs: List<String>) {
        logList = newLogs
        notifyDataSetChanged()
    }
}
