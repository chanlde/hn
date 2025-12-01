package com.dji.network.http

import android.util.Log
import com.tji.network.data.ApiResponse
import com.tji.network.data.ApiService
import com.tji.network.data.LoginResponse
import kotlinx.coroutines.*
import kotlinx.coroutines.cancel
import okhttp3.*
import okhttp3.MediaType.Companion.toMediaTypeOrNull
import okhttp3.RequestBody.Companion.asRequestBody
import okhttp3.RequestBody.Companion.toRequestBody
import okhttp3.logging.HttpLoggingInterceptor
import retrofit2.Retrofit
import retrofit2.converter.gson.GsonConverterFactory
import java.net.SocketTimeoutException
import java.net.UnknownHostException
import java.util.concurrent.TimeUnit
import javax.net.ssl.SSLException
import kotlin.also
import kotlin.apply
import kotlin.collections.forEach
import kotlin.jvm.java
import kotlin.let

// ==================== DataReportManager ====================
class HttpClient private constructor() {

    companion object {
        @Volatile
        private var INSTANCE: HttpClient? = null

        fun getInstance(): HttpClient {
            return INSTANCE ?: synchronized(this) {
                INSTANCE ?: HttpClient().also { INSTANCE = it }
            }
        }
        private const val TAG = "HttpClient"
    }

    private val scope = CoroutineScope(Dispatchers.IO + SupervisorJob())

    var authToken: String? = null

    // ==================== OkHttp 客户端配置 ====================
    val okHttpClient: OkHttpClient by lazy {
        OkHttpClient.Builder()
            .readTimeout(30, TimeUnit.SECONDS)
            .writeTimeout(30, TimeUnit.SECONDS)
            .connectTimeout(15, TimeUnit.SECONDS)
            .addInterceptor(createAuthInterceptor())
            .addInterceptor(createLoggingInterceptor())
            .build()
    }

    // ==================== Retrofit 实例 ====================
    private val retrofit: Retrofit by lazy {
        Retrofit.Builder()
            .baseUrl("BASE_URL")
            .client(okHttpClient)
            .addConverterFactory(GsonConverterFactory.create())
            .build()
    }

    private val apiService: ApiService by lazy {
        retrofit.create(ApiService::class.java)
    }

    // ==================== 拦截器 ====================
    private fun createLoggingInterceptor(): Interceptor {
        return HttpLoggingInterceptor { message ->
            Log.d(TAG, message)
        }.apply {
            level = HttpLoggingInterceptor.Level.BODY
        }
    }

    private fun createAuthInterceptor(): Interceptor {
        return Interceptor { chain ->
            var request = chain.request()

            authToken?.let { token ->
                Log.d(TAG, "🔑 Token 存在: ${token}...")  // ← 添加这行

                if (request.header("Authorization") == null) {
                    request = request.newBuilder()
                        .addHeader("Authorization", "Bearer $token")
                        .addHeader("token", token)
                        .build()

                    Log.d(TAG, "✅ 已添加 Authorization 头")  // ← 添加这行
                } else {
                    Log.d(TAG, "⚠️ Authorization 头已存在，跳过")
                }
            } ?: Log.w(TAG, "❌ Token 为空，未添加认证头")  // ← 添加这行

            // 打印最终的请求头
            Log.d(TAG, "========== 请求头信息 ==========")
            request.headers.forEach { (name, value) ->
                Log.d(TAG, "$name: $value")
            }
            Log.d(TAG, "================================")

            chain.proceed(request)
        }
    }

    private fun handleException(e: Exception): String {
        Log.d(TAG, "网络异常1111111111111111111111: ")
        return when (e) {
            is UnknownHostException -> "网络连接失败，请检查网络"
            is SocketTimeoutException -> "请求超时，请重试"
            is SSLException -> "安全连接失败"
            else -> "网络异常"
        }
    }

    // ==================== 用户认证相关 ====================
    suspend fun login(account: String, password: String,sn:String): ApiResponse<LoginResponse> {
        authToken = null
        return safeApiCall {
            apiService.login(1,account, password,sn)
        }
    }

    // ==================== 文件上传相关 ====================
    /**
     * 上传手动抓拍的照片/视频文件
     * @param request 上传请求参数
     * @return 上传结果
     */
    suspend fun uploadCaptureFile(request: com.tji.network.data.CaptureFileUploadRequest): ApiResponse<Any> {
        return try {
            val file = java.io.File(request.filePath)
            if (!file.exists()) {
                Log.e(TAG, "文件不存在: ${request.filePath}")
                return ApiResponse(code = -1, message = "文件不存在", data = null)
            }

            // 创建文件的 RequestBody
            val requestFile = file.asRequestBody("multipart/form-data".toMediaTypeOrNull())
            
            // 创建 MultipartBody.Part
            val filePart = MultipartBody.Part.createFormData(
                "pictureFile", 
                file.name, 
                requestFile
            )

            // 创建其他参数的 RequestBody
            val keyBody = request.key.toRequestBody("text/plain".toMediaTypeOrNull())
            val tidBody = request.tid.toRequestBody("text/plain".toMediaTypeOrNull())
            val fileTypeBody = request.fileType.toRequestBody("text/plain".toMediaTypeOrNull())
            
            // 将 fileInfo 转换为 JSON 字符串
            val gson = com.google.gson.Gson()
            val fileInfoJson = gson.toJson(request.fileInfo)
            val fileInfoBody = fileInfoJson.toRequestBody("application/json".toMediaTypeOrNull())

            Log.d(TAG, "========================================")
            Log.d(TAG, "📤 准备上传文件")
            Log.d(TAG, "   文件名: ${file.name}")
            Log.d(TAG, "   文件大小: ${file.length()} bytes")
            Log.d(TAG, "   设备ID: ${request.key}")
            Log.d(TAG, "   消息ID: ${request.tid}")
            Log.d(TAG, "   文件类型: ${request.fileType}")
            Log.d(TAG, "   位置信息: $fileInfoJson")
            Log.d(TAG, "========================================")

            safeApiCall {
                apiService.uploadCaptureFile(
                    tid = request.tid,
                    key = keyBody,
                    tidParam = tidBody,
                    fileInfo = fileInfoBody,
                    fileType = fileTypeBody,
                    file = filePart
                )
            }
        } catch (e: Exception) {
            Log.e(TAG, "上传文件异常", e)
            ApiResponse(code = -1, message = "上传文件失败: ${e.message}", data = null)
        }
    }


    // ==================== 通用的 API 调用方法 ====================
    private suspend fun <T> safeApiCall(call: suspend () -> ApiResponse<T>): ApiResponse<T> {
        return try {
            val response = call.invoke()

            if (response.code == 200) {  // 如果成功，处理返回的数据
                response
            } else {
                ApiResponse(code = response.code, message = "${response.message}", data = null)
            }
        } catch (e: Exception) {
            Log.e(TAG, "API调用异常", e)
            ApiResponse(code = -1, message = handleException(e), data = null)
        }
    }


    // ==================== 清理方法 ====================
    private fun clearAuthInfo() {
        authToken = null
        Log.d(TAG, "认证信息已清除")
    }

    fun destroy() {
        scope.cancel()
        okHttpClient.dispatcher.executorService.shutdown() // 关闭连接池
        clearAuthInfo()
        INSTANCE = null
    }
}
