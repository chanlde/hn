package dji.sampleV5.aircraft.video.callback

/**
 * 推流回调接口
 */
interface StreamCallback {
    /**
     * 连接开始
     */
    fun onConnectionStarted(url: String) {}
    
    /**
     * 连接成功
     */
    fun onConnectionSuccess() {}
    
    /**
     * 连接失败
     */
    fun onConnectionFailed(reason: String) {}
    
    /**
     * 断开连接
     */
    fun onDisconnect() {}
    
    /**
     * 认证错误
     */
    fun onAuthError() {}
    
    /**
     * 认证成功
     */
    fun onAuthSuccess() {}
    
    /**
     * 码率变化
     */
    fun onBitrateChanged(bitrate: Long) {}
    
    /**
     * 推流开始
     */
    fun onStreamingStarted() {}
    
    /**
     * 推流停止
     */
    fun onStreamingStopped() {}
    
    /**
     * 视频参数已设置
     */
    fun onVideoConfigSet(width: Int, height: Int, mimeType: String) {}
    
    /**
     * 错误发生
     */
    fun onError(error: Throwable) {}
}


