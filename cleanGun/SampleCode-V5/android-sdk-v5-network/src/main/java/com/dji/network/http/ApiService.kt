package com.tji.network.data



import okhttp3.MultipartBody
import okhttp3.RequestBody
import retrofit2.http.*


interface ApiService {

    @GET("/userManager/user/login")
    suspend fun login(
        @Query("productId") productID: Int = 1,
        @Query("account") account: String,
        @Query("password") password: String,
        @Query("rcSn") sn: String ,

    ): ApiResponse<LoginResponse>

    /**
     * 上传手动抓拍的照片/视频文件
     * @param tid 消息ID
     * @param key 设备ID
     * @param tidParam tid参数（文件信息中的tid）
     * @param fileInfo 文件信息（JSON字符串，包含 tid, longitude, latitude, altitude）
     * @param fileType 文件类型（1：可见光图片 2：红外图片 3：视频）
     * @param file 文件数据（二进制）
     */
    @Multipart
    @POST("/api/file/captureUploading/{tid}")
    suspend fun uploadCaptureFile(
        @Path("tid") tid: String,
        @Part("key") key: RequestBody,
        @Part("tid") tidParam: RequestBody,
        @Part("fileInfo") fileInfo: RequestBody,
        @Part("fileType") fileType: RequestBody,
        @Part file: MultipartBody.Part
    ): ApiResponse<Any>

}