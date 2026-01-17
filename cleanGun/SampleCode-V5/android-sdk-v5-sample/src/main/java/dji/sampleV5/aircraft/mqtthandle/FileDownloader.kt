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
    fun downloadKMZFile(fileUrl: String, fileName: String, context: Context): DownloadResult {
        return try {
            val file = File(context.getExternalFilesDir(null), "$fileName.kmz")
            val url = URL(fileUrl)
            val connection = url.openConnection() as HttpURLConnection
            connection.connectTimeout = 30000
            connection.readTimeout = 30000
            connection.connect()

            if (connection.responseCode == HttpURLConnection.HTTP_OK) {
                val inputStream = connection.inputStream
                val outputStream = FileOutputStream(file)
                val buffer = ByteArray(4096)
                var bytesRead: Int
                while (inputStream.read(buffer).also { bytesRead = it } != -1) {
                    outputStream.write(buffer, 0, bytesRead)
                }
                inputStream.close()
                outputStream.close()
                DownloadResult.Success(file)
            } else {
                // 返回具体的 HTTP 状态码
                DownloadResult.Failure("服务器返回错误码: ${connection.responseCode}")
            }
        } catch (e: java.net.UnknownHostException) {
            DownloadResult.Failure("网络不可用，请检查连接", e)
        } catch (e: java.net.SocketTimeoutException) {
            DownloadResult.Failure("下载超时，请重试", e)
        } catch (e: Exception) {
            DownloadResult.Failure("下载异常: ${e.localizedMessage}", e)
        }
    }
}

sealed class DownloadResult {
    data class Success(val file: File) : DownloadResult()
    data class Failure(val reason: String, val exception: Throwable? = null) : DownloadResult()
}