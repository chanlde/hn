import java.io.File
import android.util.Log
import com.dji.network.GeneralUtils.BUCKET_NAME
import com.dji.network.GeneralUtils.MINIO_ACCESS_KEY
import com.dji.network.GeneralUtils.MINIO_ENDPOINT
import com.dji.network.GeneralUtils.MINIO_SECRET_KEY
import io.minio.BucketExistsArgs
import io.minio.MakeBucketArgs
import io.minio.MinioClient
import io.minio.PutObjectArgs
import io.minio.errors.MinioException
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import java.net.ConnectException
import java.net.SocketTimeoutException
import java.net.UnknownHostException
import javax.net.ssl.SSLException

class MinioUploader {

    private val minioClient: MinioClient by lazy {
        MinioClient.builder()
            .endpoint(MINIO_ENDPOINT)
            .credentials(MINIO_ACCESS_KEY, MINIO_SECRET_KEY)
            .build()
    }
    private val TAG = "MinioUploader"

    /**
     * 测试 MinIO 连接并列出所有桶
     */
    suspend fun testConnectionAndListBuckets(): Pair<Boolean, String> = withContext(Dispatchers.IO) {
        Log.d(TAG, "========================================")
        Log.d(TAG, "🔍 测试 MinIO 连接")
        Log.d(TAG, "========================================")
        Log.d(TAG, "📡 Endpoint: $MINIO_ENDPOINT")
        Log.d(TAG, "🔑 Access Key: ${MINIO_ACCESS_KEY.take(4)}****")

        try {
            // 尝试列出所有桶来测试连接
            val buckets = minioClient.listBuckets()
            Log.d(TAG, "✔️ 连接成功！")
            Log.d(TAG, "========================================")
            Log.d(TAG, "📦 MinIO 服务器上的所有桶:")
            Log.d(TAG, "========================================")

            if (buckets.isEmpty()) {
                Log.d(TAG, "⚠️ 服务器上没有任何桶")
            } else {
                buckets.forEachIndexed { index, bucket ->
                    Log.d(TAG, "${index + 1}. 桶名: ${bucket.name()}")
                    Log.d(TAG, "   创建时间: ${bucket.creationDate()}")
                    Log.d(TAG, "----------------------------------------")
                }
            }
            Log.d(TAG, "========================================")

            Pair(true, "连接成功，共 ${buckets.size} 个桶")

        } catch (e: Exception) {
            val errorMsg = when (e) {
                is ConnectException -> {
                    Log.d(TAG, "❌ 连接错误: 无法连接到 MinIO 服务器")
                    Log.d(TAG, "   请检查:")
                    Log.d(TAG, "   1. MinIO 服务器是否启动")
                    Log.d(TAG, "   2. 网络连接是否正常")
                    Log.d(TAG, "   3. Endpoint 地址是否正确: $MINIO_ENDPOINT")
                    Log.d(TAG, "   4. 防火墙是否阻止了连接")
                    "无法连接到 MinIO 服务器，请检查网络和地址"
                }
                is SocketTimeoutException -> {
                    Log.d(TAG, "❌ 连接超时: MinIO 服务器响应超时")
                    Log.d(TAG, "   可能原因:")
                    Log.d(TAG, "   1. 网络速度过慢")
                    Log.d(TAG, "   2. 服务器负载过高")
                    Log.d(TAG, "   3. Endpoint 地址错误")
                    "连接超时，服务器无响应"
                }
                is UnknownHostException -> {
                    Log.d(TAG, "❌ 主机名解析失败: 无法解析 MinIO 服务器地址")
                    Log.d(TAG, "   请检查:")
                    Log.d(TAG, "   1. Endpoint 地址是否正确: $MINIO_ENDPOINT")
                    Log.d(TAG, "   2. DNS 服务是否正常")
                    Log.d(TAG, "   3. 是否需要使用 IP 地址代替域名")
                    "无法解析服务器地址，请检查 Endpoint 配置"
                }
                is SSLException -> {
                    Log.d(TAG, "❌ SSL/TLS 错误: 安全连接失败")
                    Log.d(TAG, "   可能原因:")
                    Log.d(TAG, "   1. 使用了 https:// 但服务器不支持")
                    Log.d(TAG, "   2. SSL 证书无效或过期")
                    Log.d(TAG, "   3. 尝试改用 http:// (仅限开发环境)")
                    "SSL 连接失败，请检查协议配置"
                }
                is MinioException -> {
                    Log.d(TAG, "❌ MinIO 错误: ${e.message}")
                    when {
                        e.message?.contains("Access Denied", ignoreCase = true) == true -> {
                            Log.d(TAG, "   认证失败，请检查:")
                            Log.d(TAG, "   1. Access Key 是否正确")
                            Log.d(TAG, "   2. Secret Key 是否正确")
                            Log.d(TAG, "   3. 用户权限是否足够")
                            "认证失败，请检查访问密钥"
                        }
                        e.message?.contains("Invalid", ignoreCase = true) == true -> {
                            Log.d(TAG, "   配置无效，请检查 MinIO 配置参数")
                            "配置参数无效"
                        }
                        else -> {
                            Log.d(TAG, "   详细信息: ${e.message}")
                            e.message ?: "MinIO 操作失败"
                        }
                    }
                }
                else -> {
                    Log.d(TAG, "❌ 未知错误: ${e.javaClass.simpleName}")
                    Log.d(TAG, "   错误信息: ${e.message}")
                    e.message ?: "未知错误"
                }
            }
            Log.d(TAG, "========================================")
            Pair(false, errorMsg)
        }
    }

    suspend fun uploadFile(
        filePath: String,
        bucketName: String = BUCKET_NAME,
        objectName: String,
        onSuccess: (String) -> Unit,
        onFailure: (String) -> Unit
    ) = withContext(Dispatchers.IO) {
        Log.d(TAG, "========================================")
        Log.d(TAG, "🚀 开始上传文件: $filePath")
        Log.d(TAG, "========================================")

        val file = File(filePath)
        if (!file.exists()) {
            Log.d(TAG, "❌ 文件不存在：$filePath")
            onFailure("文件不存在")
            return@withContext
        }

        try {
//            // 先测试连接并列出所有桶
//            val (connected, message) = testConnectionAndListBuckets()
//            if (!connected) {
//                Log.d(TAG, "❌ 上传失败: $message")
//                onFailure(message)
//                return@withContext
//            }

            Log.d(TAG, "✔️ 检查目标桶是否存在: $bucketName")
            // 确保目标桶存在
            if (!minioClient.bucketExists(BucketExistsArgs.builder().bucket(bucketName).build())) {
                Log.d(TAG, "⚠️ 桶不存在，正在创建桶：$bucketName")
                minioClient.makeBucket(MakeBucketArgs.builder().bucket(bucketName).build())
                Log.d(TAG, "✔️ 桶创建成功：$bucketName")
            } else {
                Log.d(TAG, "✔️ 目标桶已存在：$bucketName")
            }

            // 上传文件
            Log.d(TAG, "🚀 正在上传文件到桶: $bucketName")
            Log.d(TAG, "   对象名称: $objectName")
            Log.d(TAG, "   文件大小: ${file.length() / 1024} KB")

            minioClient.putObject(
                PutObjectArgs.builder()
                    .bucket(bucketName)
                    .`object`(objectName)
                    .stream(file.inputStream(), file.length(), -1)
                    .contentType(getContentType(file))
                    .build()
            )

            Log.d(TAG, "✔️ 文件上传成功！")
            Log.d(TAG, "   桶: $bucketName")
            Log.d(TAG, "   对象: $objectName")
            Log.d(TAG, "========================================")
            onSuccess("文件上传成功")

        } catch (e: Exception) {
            val errorMsg = when (e) {
                is ConnectException -> "无法连接到 MinIO 服务器"
                is SocketTimeoutException -> "连接超时"
                is UnknownHostException -> "无法解析服务器地址"
                is SSLException -> "SSL 连接失败"
                is MinioException -> e.message ?: "MinIO 操作失败"
                else -> e.message ?: "上传失败"
            }
            Log.d(TAG, "========================================")
            onFailure(errorMsg)
        }
    }

    private fun getContentType(file: File): String {
        return when (file.extension.lowercase()) {
            "jpg", "jpeg" -> "image/jpeg"
            "png" -> "image/png"
            "mp4" -> "video/mp4"
            "txt" -> "text/plain"
            "pdf" -> "application/pdf"
            "zip" -> "application/zip"
            else -> "application/octet-stream"
        }
    }
}
