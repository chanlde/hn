package com.dji.network

data class MQTTConfig(
    val serverHost: String = "192.168.0.103",
    val serverPort: Int = 1883,
    val clientId: String = "my-mqtt-client-id",
    val username: String = "my-user",
    val password: String = "my-password",
    val subscribeTopic: String = "device/status",
    val publishTopic: String = "111"
) {
    companion object {
        // 默认配置
        fun default() = MQTTConfig()

        // 创建自定义配置
        fun fromCustom(
            serverHost: String? = null,
            serverPort: Int? = null,
            clientId: String? = null,
            username: String? = null,
            password: String? = null,
            subscribeTopic: String? = null,
            publishTopic: String? = null
        ): MQTTConfig {
            return MQTTConfig(
//                serverHost = serverHost ?: "ukrd.synology.me",
//                serverPort = serverPort ?: 41883,
                serverHost = serverHost ?: "192.168.0.113",
                serverPort = serverPort ?: 1883,
                clientId = clientId ?: "987652",
                username = username ?: "admin",
                password = password ?: "public",
                subscribeTopic = subscribeTopic ?: "device/status",
                publishTopic = publishTopic ?: "111"
            )
        }
    }
}
