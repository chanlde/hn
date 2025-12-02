import okhttp3.*
import okhttp3.MediaType.Companion.toMediaType
import java.io.File
import android.util.Log
import dji.sampleV5.aircraft.log.LogManager
import dji.sampleV5.aircraft.util.GeneralUtils.BUCKET_NAME
import dji.sampleV5.aircraft.util.GeneralUtils.MINIO_ACCESS_KEY
import dji.sampleV5.aircraft.util.GeneralUtils.MINIO_ENDPOINT
import dji.sampleV5.aircraft.util.GeneralUtils.MINIO_SECRET_KEY
import io.minio.BucketExistsArgs
import io.minio.GetPresignedObjectUrlArgs
import io.minio.MakeBucketArgs
import io.minio.MinioClient
import io.minio.PutObjectArgs
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import io.minio.http.Method

class MinioUploader {

    private val minioClient: MinioClient by lazy {
        MinioClient.builder()
            .endpoint(MINIO_ENDPOINT)   // 例如 http://192.168.0.113:9005
            .credentials(MINIO_ACCESS_KEY, MINIO_SECRET_KEY)
            .build()
    }
    private val TAG = "MinioUploader"
    suspend fun uploadFile(
        filePath: String,
        bucketName: String = BUCKET_NAME,           // 默认用你配置的桶
        objectName: String,                         // 例如 "dji/photo_123.jpg"
        onSuccess: (String) -> Unit,                // 返回可直接点开的7天预签名链接
        onFailure: (String) -> Unit
    ) = withContext(Dispatchers.IO) {
        LogManager.log(TAG, "========================================")
        LogManager.log(TAG, "🚀 开始上传文件: $filePath")
        LogManager.log(TAG, "========================================")

        val file = File(filePath)
        if (!file.exists()) {
            LogManager.log(TAG, "❌ 文件不存在：$filePath")
            onFailure("文件不存在")
            return@withContext
        }

        try {
            LogManager.log(TAG, "✔️ 检查桶是否存在: $bucketName")
            // 1. 确保 bucket 存在
            if (!minioClient.bucketExists(BucketExistsArgs.builder().bucket(bucketName).build())) {
                LogManager.log(TAG, "❌ 桶不存在，正在创建桶：$bucketName")
                minioClient.makeBucket(MakeBucketArgs.builder().bucket(bucketName).build())
                LogManager.log(TAG, "✔️ 桶创建成功：$bucketName")
            } else {
                LogManager.log(TAG, "✔️ 桶已存在：$bucketName")
            }

            // 2. 上传文件
            LogManager.log(TAG, "🚀 正在上传文件：$objectName")
            minioClient.putObject(
                PutObjectArgs.builder()
                    .bucket(bucketName)
                    .`object`(objectName)
                    .stream(file.inputStream(), file.length(), -1)
                    .contentType(getContentType(file))
                    .build()
            )
            LogManager.log(TAG, "✔️ 文件上传成功：$objectName")

        } catch (e: Exception) {
            LogManager.log(TAG, "❌ 上传失败: ${e.message}")
            onFailure(e.message ?: "上传失败")
        }
    }

    private fun getContentType(file: File): String {
        return when (file.extension.lowercase()) {
            "jpg", "jpeg" -> "image/jpeg"
            "png" -> "image/png"
            "mp4" -> "video/mp4"
            "txt" -> "text/plain"
            else -> "application/octet-stream"
        }
    }
}
