package dji.v5.ux.sample.showcase.defaultlayout

import android.app.Activity
import android.os.Bundle
import android.util.Log
import androidx.lifecycle.DefaultLifecycleObserver
import androidx.lifecycle.LifecycleOwner
import dji.v5.ux.core.widget.fpv.FPVWidget
import dji.v5.ux.map.MapWidget
import dji.v5.ux.mapkit.core.maps.DJIMap
import dji.v5.ux.mapkit.core.models.DJILatLng
import java.lang.ref.WeakReference

class MapWidgetManager(
    activity: Activity, // 不直接持有
    private val mapWidget: MapWidget,
    private val fpvWidget: FPVWidget
) : DefaultLifecycleObserver {

    companion object {
        private const val TAG = "MapWidgetManager"
    }

    // 使用弱引用避免内存泄漏
    private val activityRef = WeakReference(activity)
    private var videoMapSwapManager: VideoMapSwapManager? = null
    private var isDestroyed = false

    init {
        // 自动注册生命周期观察者
        (activity as? LifecycleOwner)?.lifecycle?.addObserver(this)
    }

    fun initializeMapWidget(savedInstanceState: Bundle?) {
        if (isDestroyed) return


        mapWidget.initAMap { map ->
            // 检查是否已销毁，避免在销毁后执行回调
            if (isDestroyed || activityRef.get() == null) {
                return@initAMap
            }

            Log.d(TAG, "Map initialized successfully")
            setupMapUI(map)
            setupMapClickListener(map)
        }

        mapWidget.onCreate(savedInstanceState)

        fpvWidget.post {
            if (!isDestroyed) {
               initializeVideoMapSwapManager()
            }
        }
    }

    private fun setupMapUI(map: Any) {
        if (isDestroyed) return

        try {
            val uiSetting = map.javaClass.getMethod("getUiSettings").invoke(map)
            uiSetting?.javaClass?.getMethod("setZoomControlsEnabled", Boolean::class.java)
                ?.invoke(uiSetting, false)
            Log.d(TAG, "Map UI settings configured")
        } catch (e: Exception) {
            Log.e(TAG, "Error setting up map UI", e)
        }
    }

    private fun setupMapClickListener(map: DJIMap) {
        if (isDestroyed) return

        try {
            map.setOnMapClickListener { latLng ->
                if (!isDestroyed) {
                    handleMapClick(latLng)
                }
            }
            Log.d(TAG, "Map click listener set up")
        } catch (e: Exception) {
            Log.e(TAG, "Error setting up map click listener", e)
        }
    }

    private fun handleMapClick(latLng: DJILatLng) {
        if (isDestroyed) return

        Log.d(TAG, "Map clicked at position: $latLng")

        if (mapWidget.width < 1000) {
            videoMapSwapManager?.swapToMapFullscreen()
        }
    }

    private fun initializeVideoMapSwapManager() {
        if (isDestroyed) return

        try {
            val activity = activityRef.get()
            if (activity == null) {
                Log.w(TAG, "Activity reference is null, cannot initialize VideoMapSwapManager")
                return
            }

            videoMapSwapManager = VideoMapSwapManager(
                fpvWidget,
                mapWidget,
                lifecycleOwner = activity as LifecycleOwner
            )

            videoMapSwapManager?.addObserver(activity as LifecycleOwner) { mode ->
                if (!isDestroyed) {
                    Log.d(TAG, "Map mode changed: $mode")
                }
            }

            Log.d(TAG, "VideoMapSwapManager initialized safely")
        } catch (e: Exception) {
            Log.e(TAG, "Error initializing VideoMapSwapManager", e)
        }
    }

    // 实现生命周期回调
    override fun onResume(owner: LifecycleOwner) {
        if (!isDestroyed) {
            mapWidget.onResume()
        }
    }

    override fun onPause(owner: LifecycleOwner) {
        if (!isDestroyed) {
            mapWidget.onPause()
        }
    }

    override fun onDestroy(owner: LifecycleOwner) {
        onDestroy()
    }
    fun onDestroy() {
        if (isDestroyed) return

        isDestroyed = true

        try {
            // 清理地图资源
            mapWidget.map?.let { map ->
                map.clear()
                map.removeAllOnMapClickListener()
            }

            // 销毁地图控件
            mapWidget.onDestroy()

            // 清理引用
            videoMapSwapManager = null

            // 注销生命周期观察者
            activityRef.get()?.let { activity ->
                (activity as? LifecycleOwner)?.lifecycle?.removeObserver(this)
            }

        } catch (e: Exception) {
            Log.e(TAG, "Error during destroy", e)
        }
    }

    fun onSaveInstanceState(outState: Bundle) {
        if (!isDestroyed) {
            mapWidget.onSaveInstanceState(outState)
        }
    }
}