package dji.sampleV5.aircraft.mqtthandle

import android.content.Context
import android.util.Log
import java.io.File
import java.io.FileOutputStream
import java.net.HttpURLConnection
import java.net.URL

/**
 * 文件下载工具
 * 职责：从网络下载文件到本地
 */
object FileDownloader {
    private const val TAG = "FileDownloader"

    /**
     * 下载 KMZ 文件
     */
    fun downloadKMZFile(fileUrl: String, fileName: String, context: Context): File? {
        return try {
            // 创建文件存储路径
            val file = File(context.getExternalFilesDir(null), "$fileName.kmz")

            // 使用 HttpURLConnection 下载文件
            val url = URL(fileUrl)
            val connection = url.openConnection() as HttpURLConnection
            connection.requestMethod = "GET"
            connection.connectTimeout = 30000
            connection.readTimeout = 30000
            connection.connect()

            // 检查响应码
            if (connection.responseCode == HttpURLConnection.HTTP_OK) {
                val inputStream = connection.inputStream
                val outputStream = FileOutputStream(file)

                // 读取文件并写入到本地
                val buffer = ByteArray(4096)
                var bytesRead: Int
                var totalBytes = 0L

                while (inputStream.read(buffer).also { bytesRead = it } != -1) {
                    outputStream.write(buffer, 0, bytesRead)
                    totalBytes += bytesRead
                }

                inputStream.close()
                outputStream.close()

                Log.d(TAG, "文件下载成功: ${file.absolutePath}, 大小: $totalBytes bytes")
                file
            } else {
                Log.e(TAG, "下载失败: HTTP ${connection.responseCode}")
                null
            }
        } catch (e: Exception) {
            Log.e(TAG, "下载文件失败: ${e.message}", e)
            null
        }
    }
}