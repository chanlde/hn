package dji.sampleV5.aircraft.video.ai

import dji.v5.manager.datacenter.camera.StreamInfo

/**
 * 视频帧分析器接口
 * 用于 AI 识别、目标检测等功能
 * 
 * 使用示例：
 * ```kotlin
 * class ObjectDetectionAnalyzer : VideoFrameAnalyzer {
 *     override fun analyzeFrame(data: ByteArray, info: StreamInfo): AnalysisResult {
 *         // 执行 AI 识别
 *         return AnalysisResult(detectedObjects = listOf(...))
 *     }
 * }
 * ```
 */
interface VideoFrameAnalyzer {
    /**
     * 分析视频帧
     * @param data 视频帧数据（包含起始码的完整 NAL 单元）
     * @param info 流信息
     * @return 分析结果
     */
    fun analyzeFrame(data: ByteArray, info: StreamInfo): AnalysisResult?
    
    /**
     * 是否启用分析
     */
    fun isEnabled(): Boolean = true
    
    /**
     * 启用/禁用分析
     */
    fun setEnabled(enabled: Boolean) {}
}

/**
 * 分析结果
 */
data class AnalysisResult(
    /**
     * 检测到的对象列表
     */
    val detectedObjects: List<DetectedObject> = emptyList(),
    
    /**
     * 分析耗时（毫秒）
     */
    val processingTimeMs: Long = 0,
    
    /**
     * 其他元数据
     */
    val metadata: Map<String, Any> = emptyMap()
)

/**
 * 检测到的对象
 */
data class DetectedObject(
    /**
     * 对象类型（如：person, car, drone 等）
     */
    val type: String,
    
    /**
     * 置信度 (0.0 - 1.0)
     */
    val confidence: Float,
    
    /**
     * 边界框（归一化坐标：x, y, width, height，范围 0.0-1.0）
     */
    val boundingBox: BoundingBox,
    
    /**
     * 其他属性
     */
    val attributes: Map<String, Any> = emptyMap()
)

/**
 * 边界框
 */
data class BoundingBox(
    val x: Float,      // 左上角 X（归一化）
    val y: Float,      // 左上角 Y（归一化）
    val width: Float,  // 宽度（归一化）
    val height: Float  // 高度（归一化）
)


