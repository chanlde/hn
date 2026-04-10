package dji.v5.ux.cameracore.widget.cameracontrols.streamsettings

import android.app.Dialog
import android.content.Context
import android.graphics.Color
import android.graphics.drawable.ColorDrawable
import android.os.Handler
import android.os.Looper
import android.util.TypedValue
import android.view.Gravity
import android.view.LayoutInflater
import android.view.View
import android.view.Window
import android.view.WindowManager
import android.widget.CheckBox
import android.widget.LinearLayout
import android.widget.TextView
import dji.sdk.keyvalue.key.CameraKey
import dji.sdk.keyvalue.key.KeyTools
import dji.sdk.keyvalue.value.camera.CameraVideoStreamSourceType
import dji.sdk.keyvalue.value.camera.CameraStreamSettingsInfo
import dji.sdk.keyvalue.value.common.ComponentIndexType
import dji.v5.common.callback.CommonCallbacks
import dji.v5.common.error.IDJIError
import dji.v5.manager.KeyManager
import dji.v5.ux.R

/**
 * 多镜头存储设置弹窗
 * 类似 DJI Pilot 中的「存储设置」面板，可勾选拍照/录像时需要保存的镜头。
 */
class CameraStreamSettingsDialog(
    context: Context,
    private val cameraIndex: ComponentIndexType = ComponentIndexType.LEFT_OR_MAIN
) {

    private val dialog: Dialog = Dialog(context)
    private val mainHandler = Handler(Looper.getMainLooper())
    private val checkBoxMap = LinkedHashMap<CameraVideoStreamSourceType, CheckBox>()
    private var cbCurrentScreen: CheckBox? = null
    /** 回填 CheckBox 时禁止触发 applySettings，避免打开弹窗即多次写入飞机 */
    private var isLoading = false
    private val tvStatus: TextView
    private val llCheckboxes: LinearLayout

    init {
        dialog.requestWindowFeature(Window.FEATURE_NO_TITLE)
        val view = LayoutInflater.from(context)
            .inflate(R.layout.uxsdk_dialog_camera_stream_settings, null)
        dialog.setContentView(view)
        dialog.window?.apply {
            setBackgroundDrawable(ColorDrawable(Color.TRANSPARENT))
            setDimAmount(0.3f)
        }
        dialog.setCanceledOnTouchOutside(true)

        llCheckboxes = view.findViewById(R.id.ll_checkboxes)
        tvStatus = view.findViewById(R.id.tv_status)
    }

    fun show(anchorView: View? = null) {
        loadAvailableSources()
        dialog.show()

        dialog.window?.apply {
            if (anchorView != null) {
                val loc = IntArray(2)
                anchorView.getLocationOnScreen(loc)
                val lp = attributes
                lp.gravity = Gravity.TOP or Gravity.START
                lp.x = loc[0]
                lp.y = loc[1] + anchorView.height + 8
                lp.width = WindowManager.LayoutParams.WRAP_CONTENT
                lp.height = WindowManager.LayoutParams.WRAP_CONTENT
                attributes = lp
            } else {
                val lp = attributes
                lp.width = WindowManager.LayoutParams.WRAP_CONTENT
                lp.height = WindowManager.LayoutParams.WRAP_CONTENT
                attributes = lp
            }
        }
    }

    fun dismiss() {
        if (dialog.isShowing) dialog.dismiss()
    }

    private fun loadAvailableSources() {
        llCheckboxes.removeAllViews()
        checkBoxMap.clear()
        cbCurrentScreen = null

        val rangeKey = KeyTools.createKey(CameraKey.KeyCameraVideoStreamSourceRange, cameraIndex)
        KeyManager.getInstance().getValue(rangeKey,
            object : CommonCallbacks.CompletionCallbackWithParam<List<CameraVideoStreamSourceType>> {
                override fun onSuccess(range: List<CameraVideoStreamSourceType>?) {
                    mainHandler.post {
                        buildCheckboxes(range ?: emptyList())
                        loadCurrentSettings()
                    }
                }

                override fun onFailure(error: IDJIError) {
                    mainHandler.post {
                        buildCheckboxes(DEFAULT_SOURCES)
                        loadCurrentSettings()
                        showStatus("获取可用镜头失败: ${error.description()}")
                    }
                }
            })
    }

    private fun buildCheckboxes(sources: List<CameraVideoStreamSourceType>) {
        val ctx = llCheckboxes.context

        cbCurrentScreen = createCheckBox(ctx, "当前画面 (C)").also {
            llCheckboxes.addView(it)
        }

        for (source in sources) {
            val label = sourceLabel(source) ?: continue
            val cb = createCheckBox(ctx, label)
            llCheckboxes.addView(cb)
            checkBoxMap[source] = cb
        }

        cbCurrentScreen?.setOnCheckedChangeListener { _, _ -> applySettings() }
        for (cb in checkBoxMap.values) {
            cb.setOnCheckedChangeListener { _, _ -> applySettings() }
        }
    }

    private fun createCheckBox(ctx: Context, text: String): CheckBox {
        return CheckBox(ctx).apply {
            this.text = text
            setTextColor(Color.WHITE)
            setTextSize(TypedValue.COMPLEX_UNIT_SP, 15f)
            buttonTintList = android.content.res.ColorStateList.valueOf(Color.parseColor("#4FC3F7"))
            val dp8 = (8 * ctx.resources.displayMetrics.density).toInt()
            setPadding(dp8, dp8, dp8, dp8)
            layoutParams = LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT,
                LinearLayout.LayoutParams.WRAP_CONTENT
            )
        }
    }

    private fun loadCurrentSettings() {
        val captureKey = KeyTools.createKey(CameraKey.KeyCaptureCameraStreamSettings, cameraIndex)
        KeyManager.getInstance().getValue(captureKey,
            object : CommonCallbacks.CompletionCallbackWithParam<CameraStreamSettingsInfo> {
                override fun onSuccess(info: CameraStreamSettingsInfo?) {
                    mainHandler.post {
                        isLoading = true
                        try {
                            if (info == null) {
                                for (cb in checkBoxMap.values) cb.isChecked = true
                                cbCurrentScreen?.isChecked = true
                            } else {
                                cbCurrentScreen?.isChecked = info.getRequestCurrentScreen()
                                val selectedSources = info.getCameraVideoStreamSources() ?: emptyList()
                                for ((source, cb) in checkBoxMap.entries) {
                                    cb.isChecked = selectedSources.contains(source)
                                }
                            }
                        } finally {
                            isLoading = false
                        }
                    }
                }

                override fun onFailure(error: IDJIError) {
                    mainHandler.post {
                        isLoading = true
                        try {
                            for (cb in checkBoxMap.values) cb.isChecked = true
                            cbCurrentScreen?.isChecked = true
                            showStatus("读取当前设置失败: ${error.description()}")
                        } finally {
                            isLoading = false
                        }
                    }
                }
            })
    }

    private fun applySettings() {
        if (isLoading) return
        val selectedSources = checkBoxMap.entries
            .filter { entry -> entry.value.isChecked }
            .map { entry -> entry.key }
        val requestCurrentScreen = cbCurrentScreen?.isChecked ?: true

        val settings = CameraStreamSettingsInfo()
            .setRequestCurrentScreen(requestCurrentScreen)
            .setCameraVideoStreamSources(selectedSources)

        val captureKey = KeyTools.createKey(CameraKey.KeyCaptureCameraStreamSettings, cameraIndex)
        KeyManager.getInstance().setValue(captureKey, settings,
            object : CommonCallbacks.CompletionCallback {
                override fun onSuccess() {
                    applyRecordSettings(requestCurrentScreen, selectedSources)
                }
                override fun onFailure(error: IDJIError) {
                    mainHandler.post { showStatus("拍照设置失败: ${error.description()}") }
                }
            })
    }

    private fun applyRecordSettings(
        requestCurrentScreen: Boolean,
        sources: List<CameraVideoStreamSourceType>
    ) {
        val settings = CameraStreamSettingsInfo()
            .setRequestCurrentScreen(requestCurrentScreen)
            .setCameraVideoStreamSources(sources)

        val recordKey = KeyTools.createKey(CameraKey.KeyRecordCameraStreamSettings, cameraIndex)
        KeyManager.getInstance().setValue(recordKey, settings,
            object : CommonCallbacks.CompletionCallback {
                override fun onSuccess() {
                    mainHandler.post { showStatus("已保存") }
                    mainHandler.postDelayed({ hideStatus() }, 1500)
                }
                override fun onFailure(error: IDJIError) {
                    mainHandler.post { showStatus("录像设置失败: ${error.description()}") }
                }
            })
    }

    private fun showStatus(msg: String) {
        tvStatus.text = msg
        tvStatus.visibility = View.VISIBLE
    }

    private fun hideStatus() {
        tvStatus.visibility = View.GONE
    }

    companion object {
        private val DEFAULT_SOURCES = listOf(
            CameraVideoStreamSourceType.WIDE_CAMERA,
            CameraVideoStreamSourceType.ZOOM_CAMERA,
            CameraVideoStreamSourceType.INFRARED_CAMERA
        )

        fun sourceLabel(type: CameraVideoStreamSourceType): String? = when (type) {
            CameraVideoStreamSourceType.WIDE_CAMERA -> "广角视频 (W)"
            CameraVideoStreamSourceType.ZOOM_CAMERA -> "变焦视频 (Z)"
            CameraVideoStreamSourceType.INFRARED_CAMERA -> "红外视频 (IR)"
            CameraVideoStreamSourceType.NDVI_CAMERA -> "NDVI"
            CameraVideoStreamSourceType.RGB_CAMERA -> "RGB"
            CameraVideoStreamSourceType.POINT_CLOUD_CAMERA -> "点云"
            else -> null
        }
    }
}
