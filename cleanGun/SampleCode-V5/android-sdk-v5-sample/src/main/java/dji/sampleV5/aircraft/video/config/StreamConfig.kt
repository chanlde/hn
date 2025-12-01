package dji.sampleV5.aircraft.video.config

/**
 * 推流配置类
 */
data class StreamConfig(
    /**
     * RTMP 服务器地址
     */
    val rtmpUrl: String,
    
    /**
     * 是否只推视频（不推音频）
     */
    val videoOnly: Boolean = true,
    
    /**
     * 是否启用详细日志
     */
    val enableVerboseLog: Boolean = false,
    
    /**
     * 连接超时时间（毫秒）
     */
    val connectionTimeoutMs: Long = 5000,
    
    /**
     * 自动重连次数（0表示不重连）
     */
    val autoReconnectCount: Int = 0,
    
    /**
     * 重连间隔（毫秒）
     */
    val reconnectIntervalMs: Long = 3000
) {
    init {
        require(rtmpUrl.isNotEmpty()) { "RTMP URL 不能为空" }
        require(rtmpUrl.startsWith("rtmp://")) { "RTMP URL 格式错误，应以 rtmp:// 开头" }
    }
}


