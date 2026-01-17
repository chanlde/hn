package com.dji.network

//object GeneralUtils {
//
////    const val fcDeviceId = "1581F6GKB24C400408TU"
////    const val rtmpUrl = "rtmp://ukrd.synology.me:21935/live/test"
////    const val MINIO_ENDPOINT = "http://192.168.1.201:18006"
////    const val MINIO_ACCESS_KEY = "minio"
////    const val MINIO_SECRET_KEY = "UK13@ukdq"
////    const val BUCKET_NAME = "cloud-bucket-dji"
//
////
//    const val fcDeviceId = "1ZNBJ6F00C01DB"
//    const val rtmpUrl = "rtmp://192.168.0.113:1935/live/stream"
//    const val MINIO_ENDPOINT = "http://192.168.0.113:9005"
//    const val MINIO_ACCESS_KEY = "name"
//    const val MINIO_SECRET_KEY = "password"
//    const val BUCKET_NAME = "test"
//}

object GeneralUtils {

    val fcDeviceId: String
        get() = ConfigManager.fcDeviceId

    val rtmpUrl: String
        get() = ConfigManager.rtmpUrl

    val MINIO_ENDPOINT: String
        get() = ConfigManager.minioEndpoint

    val MINIO_ACCESS_KEY: String
        get() = ConfigManager.minioAccessKey

    val MINIO_SECRET_KEY: String
        get() = ConfigManager.minioSecretKey

    val BUCKET_NAME: String
        get() = ConfigManager.bucketName


    val serverHost: String
        get() = ConfigManager.serverHost

    val serverPort: String
        get() = ConfigManager.serverPort

    val clientId: String
        get() = ConfigManager.clientId

    val username: String
        get() = ConfigManager.username

    val password: String
        get() = ConfigManager.password

    // 如果你的代码中用的是常量（const val），需要改成属性（val）
    // 因为 const val 必须在编译时确定，无法从文件读取
}
