package dji.sampleV5.aircraft.data

data class UavControlResponse(
    var key: String,
    var tid: String,
    var api: String,
    var message: String,
    var result: String
)

data class UavControlRequest(
    val key: String?,
    val tid: String?,
    val type: Int?,
    val parameter: Int
)
data class SetHomeLocationRequest(
    val key: String,
    val tid: String,
    val homeLocation: HomeLocation // 嵌套你之前的 HomeLocation 类
)
data class HomeLocation(
    val longitude: Double,
    val latitude: Double
)

data class PauseResumeMissionRequest(
    val key: String,
    val tid: String,
    val type: Int  // 0=暂停, 1=断点续飞
)