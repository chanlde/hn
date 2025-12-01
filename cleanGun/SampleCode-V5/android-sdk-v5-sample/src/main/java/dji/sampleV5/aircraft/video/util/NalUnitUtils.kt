package dji.sampleV5.aircraft.video.util

/**
 * NAL 单元工具类
 */
object NalUnitUtils {
    
    /**
     * H.264 NAL 单元类型常量
     */
    object H264 {
        const val NON_IDR = 1        // 非 IDR 图像的编码片
        const val DATA_PARTITION_A = 2  // 编码片数据分割块 A
        const val DATA_PARTITION_B = 3  // 编码片数据分割块 B
        const val DATA_PARTITION_C = 4  // 编码片数据分割块 C
        const val IDR = 5            // IDR 图像的编码片（关键帧）
        const val SEI = 6            // 补充增强信息
        const val SPS = 7            // 序列参数集
        const val PPS = 8            // 图像参数集
        const val ACCESS_UNIT_DELIMITER = 9  // 访问单元分隔符
        const val END_OF_SEQUENCE = 10  // 序列结束
        const val END_OF_STREAM = 11    // 流结束
        const val FILLER = 12           // 填充数据
        
        /**
         * 视频帧 NAL 类型范围（1-5）
         */
        val VIDEO_FRAME_TYPES = 1..5
    }
    
    /**
     * H.265 NAL 单元类型常量
     */
    object H265 {
        const val TRAIL_N = 0        // 非参考图像的尾随图像
        const val TRAIL_R = 1        // 参考图像的尾随图像
        const val TSA_N = 2          // 非参考图像的时间子层访问
        const val TSA_R = 3          // 参考图像的时间子层访问
        const val STSA_N = 4         // 非参考图像的时间子层访问（步进）
        const val STSA_R = 5         // 参考图像的时间子层访问（步进）
        const val RADL_N = 6         // 非参考图像的随机访问可解码前导
        const val RADL_R = 7         // 参考图像的随机访问可解码前导
        const val RASL_N = 8         // 非参考图像的随机访问跳过前导
        const val RASL_R = 9         // 参考图像的随机访问跳过前导
        const val RSV_VCL_N10 = 10   // 保留
        const val RSV_VCL_R11 = 11   // 保留
        const val RSV_VCL_N12 = 12   // 保留
        const val RSV_VCL_R13 = 13   // 保留
        const val RSV_VCL_N14 = 14   // 保留
        const val RSV_VCL_R15 = 15   // 保留
        const val BLA_W_LP = 16      // 带前导的断链访问图像
        const val BLA_W_RADL = 17    // 带随机访问可解码前导的断链访问图像
        const val BLA_N_LP = 18      // 带前导的断链访问图像（非参考）
        const val IDR_W_RADL = 19    // 带随机访问可解码前导的即时解码刷新图像（关键帧）
        const val IDR_N_LP = 20      // 即时解码刷新图像（关键帧）
        const val CRA_NUT = 21       // 清理随机访问图像（关键帧）
        const val RSV_IRAP_VCL22 = 22  // 保留
        const val RSV_IRAP_VCL23 = 23  // 保留
        const val RSV_VCL24 = 24     // 保留
        const val RSV_VCL25 = 25     // 保留
        const val RSV_VCL26 = 26     // 保留
        const val RSV_VCL27 = 27     // 保留
        const val RSV_VCL28 = 28     // 保留
        const val RSV_VCL29 = 29     // 保留
        const val RSV_VCL30 = 30     // 保留
        const val RSV_VCL31 = 31     // 保留
        const val VPS = 32           // 视频参数集
        const val SPS = 33           // 序列参数集
        const val PPS = 34           // 图像参数集
        const val ACCESS_UNIT_DELIMITER = 35  // 访问单元分隔符
        const val EOS = 36           // 序列结束
        const val EOB = 37           // 比特流结束
        const val FILLER = 38        // 填充数据
        const val PREFIX_SEI = 39    // 前缀补充增强信息
        const val SUFFIX_SEI = 40    // 后缀补充增强信息
        
        /**
         * 关键帧 NAL 类型范围（16-21）
         */
        val KEY_FRAME_TYPES = 16..21
        
        /**
         * 视频帧 NAL 类型范围（0-31，排除参数集）
         */
        val VIDEO_FRAME_TYPES = 0..31
    }

    fun splitNalUnits(data: ByteArray): List<ByteArray> {
        val nalUnits = mutableListOf<ByteArray>()
        var i = 0

        while (i < data.size - 3) {
            // 找 start code
            val startCodeLen = findStartCodeLengthAt(data, i)
            if (startCodeLen == 0) {
                i++
                continue
            }

            val nalStart = i + startCodeLen

            // 找下一个 start code
            var nalEnd = data.size
            for (j in nalStart until data.size - 2) {
                if (findStartCodeLengthAt(data, j) > 0) {
                    nalEnd = j
                    break
                }
            }

            if (nalStart < nalEnd) {
                nalUnits.add(data.copyOfRange(nalStart, nalEnd))
            }

            i = nalEnd
        }

        return nalUnits
    }

    private fun findStartCodeLengthAt(data: ByteArray, offset: Int): Int {
        if (offset + 4 <= data.size &&
            data[offset] == 0.toByte() &&
            data[offset + 1] == 0.toByte() &&
            data[offset + 2] == 0.toByte() &&
            data[offset + 3] == 1.toByte()) {
            return 4
        }
        if (offset + 3 <= data.size &&
            data[offset] == 0.toByte() &&
            data[offset + 1] == 0.toByte() &&
            data[offset + 2] == 1.toByte()) {
            return 3
        }
        return 0
    }

    fun getH264NalType(nalData: ByteArray): Int {
        return if (nalData.isNotEmpty()) nalData[0].toInt() and 0x1F else -1
    }

    fun getH265NalType(nalData: ByteArray): Int {
        return if (nalData.isNotEmpty()) (nalData[0].toInt() shr 1) and 0x3F else -1
    }

    fun isKeyFrame(nalType: Int, isH265: Boolean): Boolean {
        return if (isH265) {
            nalType in H265.KEY_FRAME_TYPES
        } else {
            nalType == H264.IDR
        }
    }
}