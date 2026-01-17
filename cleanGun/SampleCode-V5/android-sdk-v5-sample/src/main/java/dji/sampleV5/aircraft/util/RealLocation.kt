package dji.sampleV5.aircraft.util

import android.Manifest
import android.annotation.SuppressLint
import android.content.Context
import android.content.pm.PackageManager
import android.location.Location
import android.location.LocationListener
import android.location.LocationManager
import android.os.Bundle
import android.util.Log
import androidx.core.content.ContextCompat
import androidx.lifecycle.DefaultLifecycleObserver
import androidx.lifecycle.LifecycleOwner

/**
 * 位置辅助类
 * ✅ 已优化：防止内存泄漏，自动管理生命周期
 */
class LocationHelper private constructor(
    context: Context  // ✅ 不保存为成员变量
) : DefaultLifecycleObserver {

    companion object {
        private const val TAG = "LocationHelper"

        /**
         * 创建实例（需要手动清理）
         * 使用后必须调用 cleanup()
         */
        fun createManual(context: Context): LocationHelper {
            return LocationHelper(context.applicationContext) // ✅ 使用 ApplicationContext
        }
    }

    // ✅ 使用 ApplicationContext，避免持有 Activity
    private val appContext = context.applicationContext

    private var locationManager: LocationManager? = null
    private var gpsListener: LocationListener? = null
    private var networkListener: LocationListener? = null

    // ✅ 标记是否已清理
    private var isCleanedUp = false

    init {
        initLocationManager()
    }

    fun initLocationManager() {
        if (isCleanedUp) {
            Log.w(TAG, "LocationHelper 已被清理，无法重新初始化")
            return
        }

        locationManager = appContext.getSystemService(Context.LOCATION_SERVICE) as LocationManager
        Log.d(TAG, "LocationManager 初始化完成")

        // 打印所有可用的provider
        val allProviders = locationManager?.allProviders
        val enabledProviders = locationManager?.getProviders(true)

        Log.d(TAG, "所有位置提供者: $allProviders")
        Log.d(TAG, "已启用的提供者: $enabledProviders")

        // 检查各个provider的状态
        Log.d(TAG, "GPS是否可用: ${locationManager?.isProviderEnabled(LocationManager.GPS_PROVIDER)}")
        Log.d(TAG, "Network是否可用: ${locationManager?.isProviderEnabled(LocationManager.NETWORK_PROVIDER)}")
        Log.d(TAG, "Passive是否可用: ${locationManager?.isProviderEnabled(LocationManager.PASSIVE_PROVIDER)}")
    }

    // 检查是否有任何定位方式开启
    fun isLocationEnabled(): Boolean {
        if (isCleanedUp) return false

        val gpsEnabled = locationManager?.isProviderEnabled(LocationManager.GPS_PROVIDER) ?: false
        val networkEnabled = locationManager?.isProviderEnabled(LocationManager.NETWORK_PROVIDER) ?: false

        Log.d(TAG, "定位状态 - GPS: $gpsEnabled, Network: $networkEnabled")
        return gpsEnabled || networkEnabled
    }

    // 检查权限
    fun hasLocationPermission(): Boolean {
        if (isCleanedUp) return false

        // 检查精确位置权限（包含粗略位置权限）
        val hasFineLocation = ContextCompat.checkSelfPermission(
            appContext,
            Manifest.permission.ACCESS_FINE_LOCATION
        ) == PackageManager.PERMISSION_GRANTED
        
        // 如果没有精确位置权限，检查粗略位置权限（Android 12+ 可能需要）
        val hasCoarseLocation = ContextCompat.checkSelfPermission(
            appContext,
            Manifest.permission.ACCESS_COARSE_LOCATION
        ) == PackageManager.PERMISSION_GRANTED
        
        val hasPermission = hasFineLocation || hasCoarseLocation
        Log.d(TAG, "位置权限检查 - 精确位置: $hasFineLocation, 粗略位置: $hasCoarseLocation, 结果: $hasPermission")
        return hasPermission
    }

    // 获取最佳provider
    fun getBestProvider(): String? {
        if (isCleanedUp) return null

        val gpsEnabled = locationManager?.isProviderEnabled(LocationManager.GPS_PROVIDER) ?: false

        return when {
            gpsEnabled -> {
                Log.d(TAG, "使用GPS定位（最精确）")
                LocationManager.GPS_PROVIDER
            }
            else -> {
                Log.w(TAG, "没有可用的定位方式")
                null
            }
        }
    }

    // 开始监听位置更新（自动选择最佳provider）
    @SuppressLint("MissingPermission")
    fun startLocationUpdates(
        onLocationReceived: (Location, String) -> Unit,
        onStatusChanged: (String) -> Unit
    ) {
        if (isCleanedUp) {
            Log.e(TAG, "LocationHelper 已被清理")
            return
        }

        if (!hasLocationPermission()) {
            Log.e(TAG, "没有位置权限")
            throw SecurityException("需要位置权限")
        }

        if (!isLocationEnabled()) {
            Log.e(TAG, "定位服务未开启")
            throw IllegalStateException("定位服务未开启")
        }

        // ✅ 先停止之前的监听
        stopLocationUpdates()

        val gpsEnabled = locationManager?.isProviderEnabled(LocationManager.GPS_PROVIDER) ?: false

        // 如果有GPS，优先使用GPS
        if (gpsEnabled) {
            Log.d(TAG, "启动GPS定位...")
            onStatusChanged("正在使用GPS定位...")
            startGPSUpdates(onLocationReceived, onStatusChanged)
        }
    }

    // GPS定位
    @SuppressLint("MissingPermission")
    private fun startGPSUpdates(
        onLocationReceived: (Location, String) -> Unit,
        onStatusChanged: (String) -> Unit
    ) {
        if (isCleanedUp) return

        gpsListener = object : LocationListener {
            override fun onLocationChanged(location: Location) {
                Log.d(TAG, "📍 GPS定位成功")
                onLocationReceived(location, "GPS")
            }

            override fun onStatusChanged(provider: String?, status: Int, extras: Bundle?) {
                Log.d(TAG, "GPS状态变化 - Status: $status")
            }

            override fun onProviderEnabled(provider: String) {
                Log.d(TAG, "GPS已启用")
                onStatusChanged("GPS已启用")
            }

            override fun onProviderDisabled(provider: String) {
                Log.d(TAG, "GPS已禁用")
                onStatusChanged("GPS已禁用")
            }
        }

        try {
            locationManager?.requestLocationUpdates(
                LocationManager.GPS_PROVIDER,
                1000L,
                0f,
                gpsListener!!
            )
            Log.d(TAG, "✅ GPS监听器已注册")
        } catch (e: Exception) {
            Log.e(TAG, "❌ GPS监听器注册失败: ${e.message}", e)
            gpsListener = null
        }
    }

    // ✅ 停止监听
    fun stopLocationUpdates() {
        try {
            gpsListener?.let {
                locationManager?.removeUpdates(it)
                Log.d(TAG, "✅ 已停止GPS更新")
            }

            networkListener?.let {
                locationManager?.removeUpdates(it)
                Log.d(TAG, "✅ 已停止Network更新")
            }
        } catch (e: Exception) {
            Log.e(TAG, "停止位置更新时出错: ${e.message}", e)
        } finally {
            gpsListener = null
            networkListener = null
        }
    }

    /**
     * ✅ 新增：完全清理资源
     */
    fun cleanup() {
        if (isCleanedUp) {
            Log.w(TAG, "LocationHelper 已经清理过了")
            return
        }

        Log.d(TAG, "开始清理 LocationHelper 资源...")

        stopLocationUpdates()
        locationManager = null
        isCleanedUp = true

        Log.d(TAG, "✅ LocationHelper 资源清理完成")
    }

    /**
     * ✅ 生命周期回调：Activity/Fragment 暂停时停止定位
     */
    override fun onPause(owner: LifecycleOwner) {
        Log.d(TAG, "生命周期 onPause - 停止位置更新")
        stopLocationUpdates()
    }

    /**
     * ✅ 生命周期回调：Activity/Fragment 销毁时清理资源
     */
    override fun onDestroy(owner: LifecycleOwner) {
        Log.d(TAG, "生命周期 onDestroy - 清理资源")
        cleanup()
        owner.lifecycle.removeObserver(this)
    }
}
