package dji.sampleV5.aircraft.video.config

import dji.v5.manager.interfaces.ICameraStreamManager
import java.util.Arrays

/**
 * 视频编码信息
 */
data class VideoCodecInfo(
    val mimeType: ICameraStreamManager.MimeType,
    val width: Int,
    val height: Int,
    val frameRate: Int = 30,  // 默认30fps，如果无法获取
    val sps: ByteArray? = null,
    val pps: ByteArray? = null,
    val vps: ByteArray? = null  // H.265 需要
) {
    /**
     * 检查编码参数是否完整
     */
    fun isComplete(): Boolean {
        return when (mimeType) {
            ICameraStreamManager.MimeType.H264 -> sps != null && pps != null
            ICameraStreamManager.MimeType.H265 -> sps != null && pps != null && vps != null
            else -> false
        }
    }
    
    /**
     * 检查编码参数是否发生变化
     */
    fun hasChanged(other: VideoCodecInfo?): Boolean {
        if (other == null) return true
        if (mimeType != other.mimeType) return true
        if (width != other.width || height != other.height) return true
        if (frameRate != other.frameRate) return true
        if (!Arrays.equals(sps, other.sps)) return true
        if (!Arrays.equals(pps, other.pps)) return true
        if (mimeType == ICameraStreamManager.MimeType.H265) {
            if (!Arrays.equals(vps, other.vps)) return true
        }
        return false
    }
    
    override fun equals(other: Any?): Boolean {
        if (this === other) return true
        if (javaClass != other?.javaClass) return false
        
        other as VideoCodecInfo
        
        if (mimeType != other.mimeType) return false
        if (width != other.width) return false
        if (height != other.height) return false
        if (frameRate != other.frameRate) return false
        if (!Arrays.equals(sps, other.sps)) return false
        if (!Arrays.equals(pps, other.pps)) return false
        if (!Arrays.equals(vps, other.vps)) return false
        
        return true
    }
    
    override fun hashCode(): Int {
        var result = mimeType.hashCode()
        result = 31 * result + width
        result = 31 * result + height
        result = 31 * result + frameRate
        result = 31 * result + (sps?.contentHashCode() ?: 0)
        result = 31 * result + (pps?.contentHashCode() ?: 0)
        result = 31 * result + (vps?.contentHashCode() ?: 0)
        return result
    }
}


