package dji.sampleV5.aircraft.manager

import android.util.Log
import android.location.Location
import dji.sampleV5.aircraft.util.LocationHelper
import dji.sampleV5.aircraft.util.ToastUtils.showToast

/**
 * 位置服务管理器
 * 负责管理位置获取和更新
 * 
 * 特性：
 * - 实时位置更新：启动后会持续监听位置变化，每次更新都会触发回调
 * - 自动管理：通过 stopLocation() 手动停止位置监听
 * 
 * @author Hoker
 * @date 2025/01/XX
 */
class LocationService private constructor(private val locationHelper: LocationHelper) {

    companion object {
        private const val TAG = "LocationService"

        @Volatile
        private var INSTANCE: LocationService? = null

        fun getInstance(locationHelper: LocationHelper): LocationService {
            return INSTANCE ?: synchronized(this) {
                INSTANCE ?: LocationService(locationHelper).also { INSTANCE = it }
            }
        }
    }

    private var isLocationStarted = false
    private var isLocationReceived = false
    private var lastLocation: Location? = null  // 保存最后一次获取到的位置

    /**
     * 启动位置监听（实时更新）
     * 启动后会持续监听位置变化，每次位置更新都会触发回调
     * 需要手动调用 stopLocation() 停止监听
     * 
     * @param onLocationReceived 位置更新回调，每次位置变化都会触发
     */
    fun startLocation(
        onLocationReceived: ((Location) -> Unit)? = null
    ) {
        if (isLocationStarted) {
            Log.d(TAG, "位置监听已启动，跳过")
            return
        }

        // 先检查权限
        if (!locationHelper.hasLocationPermission()) {
            Log.w(TAG, "位置权限未授予，无法启动定位")
            showToast("需要位置权限才能使用定位功能")
            return
        }

        // 检查定位服务是否启用
        if (!locationHelper.isLocationEnabled()) {
            Log.w(TAG, "位置服务未启用")
            showToast("请开启位置服务")
            return
        }

        try {
            val bestProvider = locationHelper.getBestProvider()
            if (bestProvider == null) {
                Log.w(TAG, "没有可用的定位方式")
                showToast("没有可用的定位方式")
                return
            }
            
            Log.d(TAG, "最佳定位方式: $bestProvider")

            locationHelper.startLocationUpdates(
                onLocationReceived = { location, provider ->
                    handleLocationReceived(location, onLocationReceived)
                },
                onStatusChanged = { status ->
                    Log.d(TAG, "定位状态: $status")
                }
            )

            isLocationStarted = true
            Log.d(TAG, "定位监听启动成功")
        } catch (e: SecurityException) {
            Log.e(TAG, "启动定位失败: 权限不足 - ${e.message}", e)
            showToast("位置权限不足，请检查权限设置")
            isLocationStarted = false
        } catch (e: IllegalStateException) {
            Log.e(TAG, "启动定位失败: 定位服务未开启 - ${e.message}", e)
            showToast("定位服务未开启，请检查设置")
            isLocationStarted = false
        } catch (e: Exception) {
            Log.e(TAG, "启动定位失败: ${e.message}", e)
            showToast("启动定位失败: ${e.message}")
            isLocationStarted = false
        }
    }

    /**
     * 处理位置接收（实时更新）
     * 每次位置更新都会触发回调，不会停止位置监听
     */
    private fun handleLocationReceived(
        location: Location,
        onLocationReceived: ((Location) -> Unit)?
    ) {
        val lat = location.latitude
        val lon = location.longitude
        
        // 保存最后一次获取到的位置
        lastLocation = location

        // 标记已获取到位置（第一次获取时设置）
        if (!isLocationReceived) {
            isLocationReceived = true
            Log.d(TAG, "首次获取位置: lat=$lat, lon=$lon")
            showToast("位置已获取: lat=$lat, lon=$lon")
        } else {
            // 后续位置更新只记录日志，不显示Toast（避免频繁弹窗）
            Log.d(TAG, "位置更新: lat=$lat, lon=$lon")
        }

        // 持续更新位置，不停止监听
        onLocationReceived?.invoke(location)
    }
    
    /**
     * 获取最后一次获取到的位置
     * @return 位置对象，如果未获取到位置则返回 null
     */
    fun getLastLocation(): Location? = lastLocation

    /**
     * 停止位置监听
     */
    fun stopLocation() {
        if (isLocationStarted) {
            locationHelper.stopLocationUpdates()
            isLocationStarted = false
            Log.d(TAG, "位置监听已停止")
        }
    }

    /**
     * 检查是否已获取位置
     */
    fun isLocationReceived(): Boolean = isLocationReceived

    /**
     * 重置位置状态
     */
    fun reset() {
        isLocationStarted = false
        isLocationReceived = false
    }
}

