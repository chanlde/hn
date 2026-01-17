package dji.sampleV5.aircraft

import android.app.Application
import android.util.Log
import com.dji.util.FileLogger
import dji.sampleV5.aircraft.manager.LocationService
import dji.sampleV5.aircraft.models.MSDKManagerVM
import dji.sampleV5.aircraft.models.globalViewModels
import dji.sampleV5.aircraft.util.LocationHelper

/**
 * Class Description
 *
 * @author Hoker
 * @date 2022/3/1
 *
 * Copyright (c) 2022, DJI All Rights Reserved.
 */
open class DJIApplication : Application() {

    private val msdkManagerVM: MSDKManagerVM by globalViewModels()
    
    companion object {
        private const val TAG = "DJIApplication"
        
        @Volatile
        private var INSTANCE: DJIApplication? = null
        
        /**
         * 获取 Application 实例（静态方法）
         * 用于在非 Android 组件类中获取 Application 实例
         */
        fun getInstance(): DJIApplication? = INSTANCE
    }
    
    // 定位服务相关（Application 级别管理，不受 Activity 生命周期影响）
    private var locationHelper: LocationHelper? = null
    private var locationService: LocationService? = null

    override fun onCreate() {
        super.onCreate()
        
        // 保存实例
        INSTANCE = this

        // 优先初始化日志系统，以便捕捉后续可能的异常
        FileLogger.init(this, enableCrashHandler = true)

        msdkManagerVM.initMobileSDK(this)
        
        // 初始化定位服务（Application 级别，不受 Activity 生命周期影响）
        initLocationService()
    }
    
    /**
     * 初始化定位服务
     * 使用 createManual 创建，不绑定生命周期，确保定位服务持续运行
     */
    private fun initLocationService() {
        try {
            locationHelper = LocationHelper.createManual(this)
            locationHelper?.initLocationManager()
            locationHelper?.let { helper ->
                locationService = LocationService.getInstance(helper)
                Log.d(TAG, "定位服务初始化成功（Application 级别）")
            } ?: run {
                Log.e(TAG, "LocationHelper 创建失败")
            }
        } catch (e: Exception) {
            Log.e(TAG, "定位服务初始化失败: ${e.message}", e)
        }
    }
    
    /**
     * 获取 LocationHelper 实例
     */
    fun getLocationHelper(): LocationHelper? = locationHelper
    
    /**
     * 获取 LocationService 实例
     */
    fun getLocationService(): LocationService? = locationService
    
    override fun onTerminate() {
        super.onTerminate()
        // 清理定位资源
        locationHelper?.cleanup()
        locationHelper = null
        locationService = null
        INSTANCE = null  // 清除实例引用
        Log.d(TAG, "Application 终止，已清理定位资源")
    }
}
