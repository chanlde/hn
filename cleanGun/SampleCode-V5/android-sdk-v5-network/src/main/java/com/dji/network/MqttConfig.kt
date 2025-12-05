package com.dji.network


data class MQTTConfig(
    val serverHost: String = GeneralUtils.serverHost, // 从 GeneralUtils 获取默认值
    val serverPort: Int = GeneralUtils.serverPort.toInt(), // 从 GeneralUtils 获取默认值
    val clientId: String = GeneralUtils.clientId, // 从 GeneralUtils 获取默认值
    val username: String = GeneralUtils.username, // 从 GeneralUtils 获取默认值
    val password: String = GeneralUtils.password, // 从 GeneralUtils 获取默认值
    val subscribeTopic: String = "device/status", // 固定值
    val publishTopic: String = "111" // 固定值
) {
    companion object {
        fun default() = MQTTConfig()
    }
}

