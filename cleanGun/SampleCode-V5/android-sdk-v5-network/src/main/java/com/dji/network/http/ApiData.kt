package com.tji.network.data

import okhttp3.MultipartBody
import okhttp3.RequestBody

data class ApiResponse<T>(
    val code: Int,
    val message: String?,
    val data: T?
)

data class LoginResponse(
    val loginInfoId: String?,
    val token: String?,
    val devicePermission: Int?,
    val proBindId: String?
)

data class AppVersion(
    val version: String?,
    val innerVersion: Int?
)

data class FileUploadParts(
    val filePart: MultipartBody.Part,
    val fileName: RequestBody,
    val fileSize: RequestBody,
    val md5RequestBody: RequestBody
)

/**
 * 文件信息（经纬高）
 */
data class FileLocationInfo(
    val tid: String,           // 手动控制拍照指令的tid 或者录像结束指令的tid
    val longitude: Double,     // 经度
    val latitude: Double,      // 纬度
    val altitude: Double       // 高度
)

/**
 * 文件上传请求参数
 */
data class CaptureFileUploadRequest(
    val key: String,           // 设备ID
    val tid: String,           // 消息ID
    val filePath: String,      // 本地文件路径
    val fileInfo: FileLocationInfo,  // 文件位置信息
    val fileType: String       // 文件类型：1-可见光图片 2-红外图片 3-视频
)