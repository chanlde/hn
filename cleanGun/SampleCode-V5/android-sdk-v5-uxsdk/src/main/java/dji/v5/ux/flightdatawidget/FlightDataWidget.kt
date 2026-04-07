package dji.v5.ux.flightdatawidget

import android.content.Context
import android.graphics.Color
import android.util.AttributeSet
import android.util.Log
import androidx.appcompat.widget.AppCompatTextView
import androidx.lifecycle.LifecycleOwner
import androidx.lifecycle.Observer
import dji.v5.ux.core.extension.textColor
import java.lang.ref.WeakReference

/**
 * 飞行数据 Widget
 *
 * 功能：显示飞行数据（位置、姿态、速度等）
 * 目前支持：位置数据（经纬高）、速度、海拔（AMSL）、当前航点索引
 */
class FlightDataWidget @JvmOverloads constructor(
    context: Context,
    attrs: AttributeSet? = null,
    defStyleAttr: Int = 0
) : AppCompatTextView(context, attrs, defStyleAttr) {

    companion object {
        private const val TAG = "FlightDataWidget"
    }

    private var flightDataViewModel: FlightDataViewModel? = null
    private var lifecycleOwnerRef: WeakReference<LifecycleOwner>? = null
    private var isActive = false

    // 保存当前数据，避免互相覆盖
    private var currentLocationData: FlightDataViewModel.LocationData? = null
    private var currentSpeed: Double? = null
    private var currentAltitudeAmslMeters: Double? = null
    private var currentWaypointIndex: Int? = null

    // 位置数据观察者
    private val locationObserver = Observer<FlightDataViewModel.LocationData> { locationData ->
        if (isActive) {
            currentLocationData = locationData
            updateDisplay()  // 更新显示
        }
    }

    private val velocity3DObserver = Observer<Double> { speed ->
        if (isActive) {
            currentSpeed = speed
            updateDisplay()  // 更新显示
        }
    }

    private val altitudeAmslObserver = Observer<Double?> { amsl ->
        if (isActive) {
            currentAltitudeAmslMeters = amsl
            updateDisplay()
        }
    }

    private val waypointIndexObserver = Observer<Int?> { idx ->
        if (isActive) {
            currentWaypointIndex = idx
            updateDisplay()
        }
    }

    /**
     * 绑定飞行数据 ViewModel（位置数据）
     */
    fun bindViewModel(viewModel: FlightDataViewModel, lifecycleOwner: LifecycleOwner) {
        stop()
        this.flightDataViewModel = viewModel
        this.lifecycleOwnerRef = WeakReference(lifecycleOwner)
        this.isActive = true

        // 观察位置数据变化
        viewModel.location.observe(lifecycleOwner, locationObserver)
        viewModel.speed.observe(lifecycleOwner, velocity3DObserver)
        viewModel.altitudeAmslMeters.observe(lifecycleOwner, altitudeAmslObserver)
        viewModel.currentWaypointIndex.observe(lifecycleOwner, waypointIndexObserver)

        // 立即获取一次位置数据
        viewModel.getCurrentLocation()

        Log.d(TAG, "FlightDataWidget 已绑定 ViewModel")
    }

    /**
     * 停止监听 - 释放所有资源
     */
    fun stop() {
        if (!isActive) return

        isActive = false

        // 移除观察者
        flightDataViewModel?.location?.removeObserver(locationObserver)
        flightDataViewModel?.speed?.removeObserver(velocity3DObserver)
        flightDataViewModel?.altitudeAmslMeters?.removeObserver(altitudeAmslObserver)
        flightDataViewModel?.currentWaypointIndex?.removeObserver(waypointIndexObserver)

        // 清理引用
        flightDataViewModel = null
        lifecycleOwnerRef?.clear()
        lifecycleOwnerRef = null

        Log.d(TAG, "FlightDataWidget 已停止")
    }

    /**
     * 统一更新显示（合并位置和速度数据）
     */
    private fun updateDisplay() {
        val locationText = currentLocationData?.getFormattedString() ?: "位置数据未就绪"
        val speedText = currentSpeed?.let { "速度: %.2fm/s".format(it) } ?: "速度: --"
        val amslText = currentAltitudeAmslMeters?.let { "海拔: %.2fm".format(it) } ?: "海拔: --"
        val wpText = currentWaypointIndex?.let { "当前航点: $it" } ?: "当前航点: --"

        // 合并显示（最下方为当前航点索引）
        text = "$locationText\n$speedText\n$amslText\n$wpText"

        // 设置文本颜色
        textColor = if (currentLocationData?.isValid == true) {
            Color.WHITE
        } else {
            Color.GRAY
        }
    }

    /**
     * View分离时自动停止
     */
    override fun onDetachedFromWindow() {
        super.onDetachedFromWindow()
        stop()
    }
}