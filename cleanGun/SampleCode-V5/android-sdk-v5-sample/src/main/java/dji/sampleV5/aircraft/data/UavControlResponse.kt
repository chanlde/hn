package dji.sampleV5.aircraft.data

data class UavControlResponse(
    var key: String,
    var tid: String,
    var api: String,
    var message: String,
    var result: String
)

data class UavControlRequest(
    val key: String,
    val tid: String,
    val type: Int,
    val parameter: Int
)