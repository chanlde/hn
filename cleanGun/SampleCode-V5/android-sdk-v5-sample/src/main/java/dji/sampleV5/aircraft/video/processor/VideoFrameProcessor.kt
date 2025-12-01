package dji.sampleV5.aircraft.video.processor

import android.media.MediaCodec
import android.util.Log
import dji.sampleV5.aircraft.video.config.VideoCodecInfo
import dji.sampleV5.aircraft.video.util.NalUnitUtils
import dji.v5.manager.datacenter.camera.StreamInfo
import dji.v5.manager.interfaces.ICameraStreamManager
import java.nio.ByteBuffer
import java.util.concurrent.atomic.AtomicReference

/**
 * 视频帧处理器
 * 负责处理 H.264/H.265 视频帧，提取 SPS/PPS/VPS，并准备发送数据
 */
class VideoFrameProcessor {
    companion object {
        private const val TAG = "VideoFrameProcessor"
    }

    private val codecInfo = AtomicReference<VideoCodecInfo?>(null)

    var onFrameReady: ((ByteArray, MediaCodec.BufferInfo) -> Unit)? = null

    /**
     * 处理一帧数据（可能包含多个 NAL 单元）
     */
    fun processFrame(data: ByteArray, info: StreamInfo): ProcessResult {
        Log.d(TAG, "========== processFrame 开始 ==========")
        Log.d(TAG, "数据大小: ${data.size} 字节")
        Log.d(TAG, "分辨率: ${info.width}x${info.height}, 编码: ${info.mimeType}")
        
        // 分割出所有 NAL 单元
        val nalUnits = NalUnitUtils.splitNalUnits(data)
        Log.d(TAG, "分割出 NAL 单元数量: ${nalUnits.size}")

        if (nalUnits.isEmpty()) {
            Log.w(TAG, "没有找到 NAL 单元，返回 Invalid")
            return ProcessResult.Invalid
        }

        var result = ProcessResult.Invalid

        for ((index, nalData) in nalUnits.withIndex()) {
            if (nalData.isEmpty()) {
                Log.w(TAG, "NAL 单元 $index 为空，跳过")
                continue
            }

            val nalType = when (info.mimeType) {
                ICameraStreamManager.MimeType.H264 -> NalUnitUtils.getH264NalType(nalData)
                ICameraStreamManager.MimeType.H265 -> NalUnitUtils.getH265NalType(nalData)
                else -> -1
            }
            Log.d(TAG, "处理 NAL 单元 $index: 类型=$nalType, 大小=${nalData.size} 字节")

            result = when (info.mimeType) {
                ICameraStreamManager.MimeType.H264 -> processH264Nal(nalData, info)
                ICameraStreamManager.MimeType.H265 -> processH265Nal(nalData, info)
                else -> {
                    Log.w(TAG, "未知编码格式: ${info.mimeType}")
                    ProcessResult.Unsupported
                }
            }
            Log.d(TAG, "NAL 单元 $index 处理结果: $result")
        }

        // 如果编码信息完整，发送整帧数据
        val currentInfo = codecInfo.get()
        val isComplete = currentInfo?.isComplete() == true
        val hasVideoFrame = containsVideoFrame(nalUnits, info)
        
        Log.d(TAG, "编码信息完整: $isComplete")
        Log.d(TAG, "包含视频帧: $hasVideoFrame")
        
        if (isComplete && hasVideoFrame) {
            Log.d(TAG, "准备发送视频帧，大小: ${data.size} 字节")
            prepareFrame(data, currentInfo!!, nalUnits, info)
            Log.d(TAG, "========== processFrame 结束: FrameReady ==========")
            return ProcessResult.FrameReady
        }

        Log.d(TAG, "========== processFrame 结束: $result ==========")
        return result
    }

    /**
     * 处理单个 H.264 NAL 单元
     */
    private fun processH264Nal(nalData: ByteArray, info: StreamInfo): ProcessResult {
        val nalType = NalUnitUtils.getH264NalType(nalData)

        return when (nalType) {
            NalUnitUtils.H264.SPS -> {
                updateCodecInfo(info, sps = nalData)
                Log.d(TAG, "H.264 SPS 已更新, 长度: ${nalData.size}")
                ProcessResult.CodecInfoUpdated
            }
            NalUnitUtils.H264.PPS -> {
                updateCodecInfo(info, pps = nalData)
                Log.d(TAG, "H.264 PPS 已更新, 长度: ${nalData.size}")
                ProcessResult.CodecInfoUpdated
            }
            else -> {
                val currentInfo = codecInfo.get()
                val isComplete = currentInfo?.isComplete() == true
                Log.d(TAG, "H.264 NAL 类型 $nalType (非 SPS/PPS), 编码信息完整: $isComplete")
                if (isComplete) {
                    ProcessResult.FrameReady
                } else {
                    Log.d(TAG, "等待编码信息: SPS=${currentInfo?.sps?.size ?: "null"}, PPS=${currentInfo?.pps?.size ?: "null"}")
                    ProcessResult.WaitingForCodecInfo
                }
            }
        }
    }

    /**
     * 处理单个 H.265 NAL 单元
     */
    private fun processH265Nal(nalData: ByteArray, info: StreamInfo): ProcessResult {
        val nalType = NalUnitUtils.getH265NalType(nalData)

        return when (nalType) {
            NalUnitUtils.H265.VPS -> {
                updateCodecInfo(info, vps = nalData)
                Log.d(TAG, "H.265 VPS 已更新, 长度: ${nalData.size}")
                ProcessResult.CodecInfoUpdated
            }
            NalUnitUtils.H265.SPS -> {
                updateCodecInfo(info, sps = nalData)
                Log.d(TAG, "H.265 SPS 已更新, 长度: ${nalData.size}")
                ProcessResult.CodecInfoUpdated
            }
            NalUnitUtils.H265.PPS -> {
                updateCodecInfo(info, pps = nalData)
                Log.d(TAG, "H.265 PPS 已更新, 长度: ${nalData.size}")
                ProcessResult.CodecInfoUpdated
            }
            else -> {
                val currentInfo = codecInfo.get()
                val isComplete = currentInfo?.isComplete() == true
                Log.d(TAG, "H.265 NAL 类型 $nalType (非 VPS/SPS/PPS), 编码信息完整: $isComplete")
                if (isComplete) {
                    ProcessResult.FrameReady
                } else {
                    Log.d(TAG, "等待编码信息: VPS=${currentInfo?.vps?.size ?: "null"}, SPS=${currentInfo?.sps?.size ?: "null"}, PPS=${currentInfo?.pps?.size ?: "null"}")
                    ProcessResult.WaitingForCodecInfo
                }
            }
        }
    }

    /**
     * 检查是否包含视频帧（非参数集）
     */
    private fun containsVideoFrame(nalUnits: List<ByteArray>, info: StreamInfo): Boolean {
        for (nalData in nalUnits) {
            if (nalData.isEmpty()) continue

            val nalType = when (info.mimeType) {
                ICameraStreamManager.MimeType.H264 -> NalUnitUtils.getH264NalType(nalData)
                ICameraStreamManager.MimeType.H265 -> NalUnitUtils.getH265NalType(nalData)
                else -> continue
            }

            // H.264: 1-5 是视频帧, H.265: 0-31 是视频帧（排除 32-34 参数集）
            val isVideoFrame = when (info.mimeType) {
                ICameraStreamManager.MimeType.H264 -> nalType in NalUnitUtils.H264.VIDEO_FRAME_TYPES
                ICameraStreamManager.MimeType.H265 -> nalType in NalUnitUtils.H265.VIDEO_FRAME_TYPES
                else -> false
            }

            if (isVideoFrame) return true
        }
        return false
    }

    /**
     * 准备发送帧
     */
    private fun prepareFrame(
        data: ByteArray,
        codecInfo: VideoCodecInfo,
        nalUnits: List<ByteArray>,
        info: StreamInfo
    ) {
        // 检查是否为关键帧
        val isKeyFrame = nalUnits.any { nalData ->
            if (nalData.isEmpty()) return@any false
            val nalType = when (info.mimeType) {
                ICameraStreamManager.MimeType.H264 -> NalUnitUtils.getH264NalType(nalData)
                ICameraStreamManager.MimeType.H265 -> NalUnitUtils.getH265NalType(nalData)
                else -> return@any false
            }
            NalUnitUtils.isKeyFrame(nalType, info.mimeType == ICameraStreamManager.MimeType.H265)
        }

        val bufferInfo = MediaCodec.BufferInfo().apply {
            offset = 0
            size = data.size
            flags = if (isKeyFrame) MediaCodec.BUFFER_FLAG_KEY_FRAME else 0
        }

        onFrameReady?.invoke(data, bufferInfo)
    }

    private fun updateCodecInfo(
        info: StreamInfo,
        sps: ByteArray? = null,
        pps: ByteArray? = null,
        vps: ByteArray? = null
    ) {
        val current = codecInfo.get()
        val updated = VideoCodecInfo(
            mimeType = info.mimeType,
            width = info.width,
            height = info.height,
            frameRate = 30,
            sps = sps ?: current?.sps,
            pps = pps ?: current?.pps,
            vps = vps ?: current?.vps
        )
        codecInfo.set(updated)
        
        // 详细日志
        Log.d(TAG, "========== updateCodecInfo ==========")
        Log.d(TAG, "当前状态: SPS=${current?.sps?.size ?: "null"}, PPS=${current?.pps?.size ?: "null"}, VPS=${current?.vps?.size ?: "null"}")
        Log.d(TAG, "更新参数: SPS=${sps?.size ?: "null"}, PPS=${pps?.size ?: "null"}, VPS=${vps?.size ?: "null"}")
        Log.d(TAG, "更新后状态: SPS=${updated.sps?.size ?: "null"}, PPS=${updated.pps?.size ?: "null"}, VPS=${updated.vps?.size ?: "null"}")
        Log.d(TAG, "编码信息是否完整: ${updated.isComplete()}")
        Log.d(TAG, "分辨率: ${info.width}x${info.height}, 编码: ${info.mimeType}")
        Log.d(TAG, "=====================================")
    }

    fun getCodecInfo(): VideoCodecInfo? = codecInfo.get()

    fun reset() {
        codecInfo.set(null)
    }

    enum class ProcessResult {
        FrameReady,
        CodecInfoUpdated,
        WaitingForCodecInfo,
        Invalid,
        Unsupported
    }
}


