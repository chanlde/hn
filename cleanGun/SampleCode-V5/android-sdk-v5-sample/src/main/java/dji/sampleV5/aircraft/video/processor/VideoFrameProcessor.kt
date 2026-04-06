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
        val nalUnits = NalUnitUtils.splitNalUnits(data)

        if (nalUnits.isEmpty()) {
            Log.w(TAG, "无 NAL 单元")
            return ProcessResult.Invalid
        }

        var result = ProcessResult.Invalid

        for (nalData in nalUnits) {
            if (nalData.isEmpty()) continue

            result = when (info.mimeType) {
                ICameraStreamManager.MimeType.H264 -> processH264Nal(nalData, info)
                ICameraStreamManager.MimeType.H265 -> processH265Nal(nalData, info)
                else -> {
                    Log.w(TAG, "未知编码格式: ${info.mimeType}")
                    ProcessResult.Unsupported
                }
            }
        }

        val currentInfo = codecInfo.get()
        val isComplete = currentInfo?.isComplete() == true
        val hasVideoFrame = containsVideoFrame(nalUnits, info)

        if (isComplete && hasVideoFrame) {
            prepareFrame(data, currentInfo!!, nalUnits, info)
            return ProcessResult.FrameReady
        }

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
                ProcessResult.CodecInfoUpdated
            }
            NalUnitUtils.H264.PPS -> {
                updateCodecInfo(info, pps = nalData)
                ProcessResult.CodecInfoUpdated
            }
            else -> {
                val currentInfo = codecInfo.get()
                val isComplete = currentInfo?.isComplete() == true
                if (isComplete) {
                    ProcessResult.FrameReady
                } else {
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
                ProcessResult.CodecInfoUpdated
            }
            NalUnitUtils.H265.SPS -> {
                updateCodecInfo(info, sps = nalData)
                ProcessResult.CodecInfoUpdated
            }
            NalUnitUtils.H265.PPS -> {
                updateCodecInfo(info, pps = nalData)
                ProcessResult.CodecInfoUpdated
            }
            else -> {
                val currentInfo = codecInfo.get()
                val isComplete = currentInfo?.isComplete() == true
                if (isComplete) {
                    ProcessResult.FrameReady
                } else {
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
