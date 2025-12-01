package dji.v5.ux.sample.showcase.defaultlayout

import android.util.Log
import android.view.View
import androidx.lifecycle.LifecycleOwner
import androidx.lifecycle.LiveData
import androidx.lifecycle.MutableLiveData
import androidx.lifecycle.Observer
import dji.v5.ux.core.widget.fpv.FPVWidget
import dji.v5.ux.map.MapWidget

// 地图显示模式枚举
enum class MapDisplayMode {
    FULLSCREEN,   // 地图全屏
    SMALL_SCREEN  // 地图小屏
}

// Lifecycle-aware VideoMapSwapManager
class VideoMapSwapManager(
    private val fpvWidget: FPVWidget,
    private val mapWidget: MapWidget,
    lifecycleOwner: LifecycleOwner
) {
    // LiveData 保存当前地图模式
    private val _mapMode = MutableLiveData<MapDisplayMode>()
    val mapMode: LiveData<MapDisplayMode> = _mapMode

    // 当前模式，默认小屏
    private var currentMode = MapDisplayMode.SMALL_SCREEN

    init {
        // 订阅 LiveData 并绑定生命周期
        _mapMode.observe(lifecycleOwner, Observer { mode ->
            currentMode = mode
            Log.d("VideoMapSwapManager", "Map mode changed: $mode")
        })

        // 确保 mapWidget 初始在前
        mapWidget.bringToFront()

        setupClickListeners()
    }

    private fun setupClickListeners() {
        fpvWidget.setOnClickListener { onViewClick(it) }
        mapWidget.setOnClickListener { onViewClick(it) }
    }

    fun onViewClick(view: View) {
        val fpvWidth = fpvWidget.width
        val mapWidth = mapWidget.width

        if (view == fpvWidget && fpvWidth < mapWidth) {
            swapToVideoFullscreen()
        } else if (view == mapWidget && mapWidth < fpvWidth) {
            swapToMapFullscreen()
        }
    }

    private fun swapToVideoFullscreen() {
        // 交换布局参数
        val tempParams = fpvWidget.layoutParams
        fpvWidget.layoutParams = mapWidget.layoutParams
        mapWidget.layoutParams = tempParams
        mapWidget.bringToFront()

        // 更新模式
        _mapMode.value = MapDisplayMode.SMALL_SCREEN
        Log.d("VideoMapSwapManager", "Swapped to Video fullscreen")
    }

    fun swapToMapFullscreen() {
        val tempParams = fpvWidget.layoutParams
        fpvWidget.layoutParams = mapWidget.layoutParams
        mapWidget.layoutParams = tempParams
        fpvWidget.bringToFront()

        _mapMode.value = MapDisplayMode.FULLSCREEN
        Log.d("VideoMapSwapManager", "Swapped to Map fullscreen")
    }

    // 提供外部订阅接口，可绑定任意 LifecycleOwner
    fun addObserver(owner: LifecycleOwner, observer: (MapDisplayMode) -> Unit) {
        _mapMode.observe(owner, Observer { observer(it) })
        observer(currentMode) // 立即回调当前状态
    }

    // 状态接口，可根据需要更新 UI
    interface MapWidgetState {
        fun updateDrawingToolbarVisibility(drawingToolbar: View)
    }
}
