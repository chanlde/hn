from __future__ import annotations

from typing import Any

from PyQt5.QtCore import QTimer
from PyQt5.QtWidgets import (
    QCheckBox,
    QDialog,
    QDialogButtonBox,
    QGridLayout,
    QHBoxLayout,
    QLabel,
    QMainWindow,
    QPushButton,
    QVBoxLayout,
    QWidget,
)

import ota_protocol as proto
from ota_protocol import DeviceInfo, FirmwareInfo
from serial_worker import SerialWorker
from widgets.connect_panel import ConnectPanel
from widgets.firmware_panel import FirmwarePanel
from widgets.log_panel import LogPanel
from widgets.upgrade_panel import UpgradePanel


class MainWindow(QMainWindow):
    def __init__(self) -> None:
        super().__init__()
        self.setWindowTitle("SolarClean 固件升级工具")
        self.resize(980, 520)

        self.worker: SerialWorker | None = None
        self.connected = False
        self.current_port = ""
        self.device_info: DeviceInfo | None = None
        self.firmware_info: FirmwareInfo | None = None
        self.firmware_data: bytes | None = None

        self.upgrading = False
        self.start_pending = False
        self.ota_ready = False
        self.handshake_active = False
        self.waiting_ack = False
        self.offset = 0
        self.seq = 0
        self.chunk_size = proto.CHUNK_SIZE
        self.current_frame = b""
        self.current_label = ""
        self.current_retries = 0
        self.handshake_send_count = 0

        self.handshake_timer = QTimer(self)
        self.handshake_timer.setInterval(300)
        self.handshake_timer.timeout.connect(self._send_probe_commands)

        self.start_ack_timer = QTimer(self)
        self.start_ack_timer.setSingleShot(True)
        self.start_ack_timer.timeout.connect(self._handle_start_timeout)

        self.ack_timer = QTimer(self)
        self.ack_timer.timeout.connect(self._retry_current_frame)

        self._build_ui()
        self._connect_signals()
        self._refresh_ready_state()

    def _build_ui(self) -> None:
        central = QWidget()
        central.setObjectName("central")
        self.setCentralWidget(central)

        root = QGridLayout(central)
        root.setContentsMargins(20, 20, 20, 20)
        root.setHorizontalSpacing(14)
        root.setVerticalSpacing(14)

        header_row = QWidget()
        header_layout = QHBoxLayout(header_row)
        header_layout.setContentsMargins(0, 0, 0, 0)
        header_layout.setSpacing(12)

        header = QLabel("固件升级工具")
        header.setStyleSheet("font-size: 24px; font-weight: 700;")
        self.param_settings_btn = QPushButton("参数设置")
        self.param_settings_btn.clicked.connect(self.open_param_settings)
        header_layout.addWidget(header)
        header_layout.addStretch(1)
        header_layout.addWidget(self.param_settings_btn)
        root.addWidget(header_row, 0, 0, 1, 2)

        tip = QLabel("按顺序完成 3 步即可升级。升级过程中请不要拔掉设备。")
        tip.setProperty("muted", True)
        root.addWidget(tip, 1, 0, 1, 2)

        self.connect_panel = ConnectPanel()
        self.firmware_panel = FirmwarePanel()
        self.upgrade_panel = UpgradePanel()
        self.param_dialog = self._build_param_dialog()

        self.log_panel = LogPanel(self)
        self.log_panel.setVisible(False)

        root.addWidget(self.connect_panel, 2, 0, 1, 2)
        root.addWidget(self.firmware_panel, 3, 0)
        root.addWidget(self.upgrade_panel, 3, 1)
        root.setColumnStretch(0, 3)
        root.setColumnStretch(1, 2)
        root.setRowStretch(3, 1)

    def _build_param_dialog(self) -> QDialog:
        dialog = QDialog(self)
        dialog.setWindowTitle("参数设置")
        dialog.setMinimumWidth(420)
        dialog.setModal(False)

        root = QVBoxLayout(dialog)
        root.setContentsMargins(20, 18, 20, 18)
        root.setSpacing(10)

        title = QLabel("设备参数")
        title.setStyleSheet("font-size: 16px; font-weight: 600;")
        root.addWidget(title)

        self.param_status_label = QLabel("请先连接设备")
        self.param_status_label.setProperty("muted", True)
        self.param_status_label.setWordWrap(True)
        root.addWidget(self.param_status_label)

        self.failsafe_hold_check = QCheckBox("遥控器断连后保持喷水和摆动")
        self.failsafe_hold_check.setEnabled(False)
        root.addWidget(self.failsafe_hold_check)

        self.remote_control_check = QCheckBox("启用遥控器控制")
        self.remote_control_check.setEnabled(False)
        root.addWidget(self.remote_control_check)

        self.four_g_check = QCheckBox("启用 4G 通讯")
        self.four_g_check.setEnabled(False)
        root.addWidget(self.four_g_check)

        self.psdk_check = QCheckBox("启用 PSDK")
        self.psdk_check.setEnabled(False)
        root.addWidget(self.psdk_check)

        self.psdk_hint_label = QLabel("PSDK 开关保存后下次上电生效")
        self.psdk_hint_label.setProperty("muted", True)
        root.addWidget(self.psdk_hint_label)

        buttons = QDialogButtonBox(QDialogButtonBox.Close)
        close_button = buttons.button(QDialogButtonBox.Close)
        if close_button is not None:
            close_button.setText("关闭")
        buttons.rejected.connect(dialog.close)
        root.addWidget(buttons)

        return dialog

    def open_param_settings(self) -> None:
        self.param_dialog.show()
        self.param_dialog.raise_()
        self.param_dialog.activateWindow()

    def _connect_signals(self) -> None:
        self.connect_panel.connect_requested.connect(self.open_serial)
        self.connect_panel.disconnect_requested.connect(self.close_serial)
        self.connect_panel.probe_requested.connect(lambda: self._begin_auto_handshake(force=True))
        self.firmware_panel.firmware_loaded.connect(self.on_firmware_loaded)
        self.firmware_panel.load_failed.connect(self.on_file_error)
        self.upgrade_panel.start_requested.connect(self.start_upgrade)
        self.upgrade_panel.cancel_requested.connect(self.cancel_upgrade)
        self.failsafe_hold_check.toggled.connect(self._on_failsafe_hold_toggled)
        self.remote_control_check.toggled.connect(self._on_remote_control_toggled)
        self.four_g_check.toggled.connect(self._on_four_g_toggled)
        self.psdk_check.toggled.connect(self._on_psdk_toggled)

    def open_serial(self, port: str, baudrate: int) -> None:
        if self.worker is not None:
            return
        self.connect_panel.set_connecting(True)
        self.worker = SerialWorker(port, baudrate, self)
        self.worker.connected.connect(self.on_connected)
        self.worker.disconnected.connect(self.on_disconnected)
        self.worker.error.connect(self.on_error)
        self.worker.text_received.connect(self.on_text_received)
        self.worker.message_received.connect(self.on_message_received)
        self.worker.finished.connect(self.on_worker_finished)
        self.worker.start()
        self.log_panel.append(f"Opening {port} @ {baudrate}")

    def close_serial(self) -> None:
        self._stop_timers()
        self.upgrading = False
        self.start_pending = False
        self.handshake_active = False
        self.waiting_ack = False
        if self.worker is not None:
            self.worker.stop()
            self.worker.wait(1500)
            self.worker = None
        self.on_disconnected()

    def on_connected(self, port: str) -> None:
        self.connected = True
        self.current_port = port
        self.device_info = None
        self.connect_panel.set_connected(True, port)
        self.param_status_label.setText("正在读取参数")
        self.log_panel.append(f"Connected {port}")
        self.upgrade_panel.set_status("正在准备设备", "请保持设备连接，软件会自动完成准备。", 0)
        self._begin_auto_handshake()
        self._refresh_ready_state()

    def on_disconnected(self) -> None:
        self._stop_timers()
        self.connected = False
        self.current_port = ""
        self.device_info = None
        self.ota_ready = False
        self.start_pending = False
        self.upgrading = False
        self.handshake_active = False
        self.waiting_ack = False
        self.connect_panel.set_connected(False)
        self.param_status_label.setText("请先连接设备")
        self.failsafe_hold_check.blockSignals(True)
        self.failsafe_hold_check.setChecked(False)
        self.failsafe_hold_check.blockSignals(False)
        self.failsafe_hold_check.setEnabled(False)
        self._set_param_check(self.remote_control_check, False, False)
        self._set_param_check(self.four_g_check, False, False)
        self._set_param_check(self.psdk_check, False, False)
        self._refresh_ready_state()

    def on_worker_finished(self) -> None:
        if self.worker is not None and not self.worker.isRunning():
            self.worker = None
        if not self.connected:
            self.connect_panel.set_connected(False)
            self._refresh_ready_state()

    def on_firmware_loaded(self, info: FirmwareInfo, data: bytes) -> None:
        self.firmware_info = info
        self.firmware_data = data
        self.log_panel.append(f"Selected firmware: {info.path}")
        self.log_panel.append(f"size={info.size}, sha256={info.sha256}")
        if self.ota_ready:
            self.upgrade_panel.set_status("可以升级", "设备和升级文件已准备好，请点击“开始升级”。", 0)
        else:
            self.upgrade_panel.set_status("升级文件已选择", "请等待设备准备完成，必要时重新上电设备。", 0)
            self._begin_auto_handshake()
        self._refresh_ready_state()

    def on_file_error(self, message: str) -> None:
        self.log_panel.append(message)
        self.upgrade_panel.set_status("文件不可用", "请确认选择的是厂家提供的升级文件。", None)

    def start_upgrade(self) -> None:
        if not self.connected or self.worker is None:
            self.upgrade_panel.set_status("请先连接设备", "连接设备后，再点击“开始升级”。", None)
            return
        if self.firmware_info is None or self.firmware_data is None:
            self.upgrade_panel.set_status("请选择升级文件", "请先点击“选择固件”。", None)
            return
        if not self.ota_ready:
            self.upgrade_panel.set_status("设备未准备好", "请重新上电设备，软件会自动准备。", 0)
            self._begin_auto_handshake(force=True)
            return

        self.start_pending = True
        self.upgrade_panel.set_running(True)
        self.upgrade_panel.set_status("正在开始升级", "请不要拔掉设备。", 0)
        self._refresh_ready_state()
        self._send_start_command()
        self.start_ack_timer.start(30000)

    def _begin_transfer(self) -> None:
        self.upgrading = True
        self.start_pending = False
        self.waiting_ack = False
        self.offset = 0
        self.seq = 0
        self.current_frame = b""
        self.current_retries = 0
        self.upgrade_panel.set_running(True)
        self.upgrade_panel.set_status("正在升级", "请不要拔掉设备。", 0)
        self.log_panel.append("Start firmware transfer")
        self._send_next_frame()

    def cancel_upgrade(self) -> None:
        if self.worker and self.connected and (self.upgrading or self.waiting_ack or self.start_pending):
            frame = proto.build_abort_frame(self.seq, self.offset)
            self.worker.send(frame)
            self.log_panel.append("Abort requested")
        self._stop_upgrade("已停止", "升级已停止，请重新连接设备后再试。", 0)

    def on_text_received(self, text: str) -> None:
        self.log_panel.append(text)

    def on_message_received(self, msg: dict[str, Any]) -> None:
        msg_type = msg.get("type")
        if msg_type == "deviceInfo":
            self._handle_device_info(msg)
        elif msg_type == "deviceParams":
            self._handle_device_params(msg)
        elif msg_type == "otaSerialReady":
            self._handle_ready_ack(msg)
        elif msg_type == "otaSerialStatus":
            self._handle_status(msg)
        elif msg_type == "otaSerialAck":
            self._handle_start_ack(msg)
        elif msg_type == "otaSerialChunkAck":
            self._handle_chunk_ack(msg)
        elif msg_type == "otaSerialDone":
            self._handle_done(msg)

    def on_error(self, message: str) -> None:
        self.log_panel.append(message)
        if self.upgrading or self.start_pending:
            self._stop_upgrade("升级失败", self._friendly_error(message), None)

    def _handle_device_info(self, msg: dict[str, Any]) -> None:
        info = proto.parse_device_info(msg)
        self.device_info = info
        self.chunk_size = max(128, min(proto.CHUNK_SIZE, int(info.max_chunk or proto.CHUNK_SIZE)))
        self.connect_panel.set_device_info(info)
        self.param_status_label.setText("参数已同步")
        self.failsafe_hold_check.blockSignals(True)
        self.failsafe_hold_check.setChecked(info.failsafe_hold_enabled)
        self.failsafe_hold_check.blockSignals(False)
        self.failsafe_hold_check.setEnabled(self.connected)
        self._set_param_check(self.remote_control_check, info.remote_control_enabled, self.connected)
        self._set_param_check(self.four_g_check, info.four_g_enabled, self.connected)
        self._set_param_check(self.psdk_check, info.psdk_enabled, self.connected)
        self.log_panel.append(
            f"设备信息：固件={info.firmware_version or '-'} inner={info.inner_version or '-'} 硬件={info.hardware_version or '-'}"
        )

    def _handle_device_params(self, msg: dict[str, Any]) -> None:
        if msg.get("ok") is not True:
            self.log_panel.append(f"Device param command failed: {msg.get('msg', 'unknown error')}")
            return
        params = proto.parse_ext_params(msg)
        enabled = bool(params.get("failsafeHoldEnabled", False))
        self.failsafe_hold_check.blockSignals(True)
        self.failsafe_hold_check.setChecked(enabled)
        self.failsafe_hold_check.blockSignals(False)
        self.failsafe_hold_check.setEnabled(self.connected)
        self._set_param_check(self.remote_control_check, bool(params.get("remoteControlEnabled", False)), self.connected)
        self._set_param_check(self.four_g_check, bool(params.get("fourGEnabled", False)), self.connected)
        self._set_param_check(self.psdk_check, bool(params.get("psdkEnabled", False)), self.connected)
        self.param_status_label.setText("参数已保存")
        self.log_panel.append(
            "参数："
            f"断连保持={'开' if enabled else '关'}，"
            f"遥控器={'开' if params.get('remoteControlEnabled', False) else '关'}，"
            f"4G={'开' if params.get('fourGEnabled', False) else '关'}，"
            f"PSDK={'开' if params.get('psdkEnabled', False) else '关'}"
        )

    def _on_failsafe_hold_toggled(self, checked: bool) -> None:
        self._send_device_param("failsafeHoldEnabled", checked)

    def _on_remote_control_toggled(self, checked: bool) -> None:
        self._send_device_param("remoteControlEnabled", checked)

    def _on_four_g_toggled(self, checked: bool) -> None:
        self._send_device_param("fourGEnabled", checked)

    def _on_psdk_toggled(self, checked: bool) -> None:
        self._send_device_param("psdkEnabled", checked)

    @staticmethod
    def _set_param_check(check: QCheckBox, checked: bool, enabled: bool) -> None:
        check.blockSignals(True)
        check.setChecked(checked)
        check.blockSignals(False)
        check.setEnabled(enabled)

    def _send_device_param(self, name: str, checked: bool) -> None:
        if not self.connected or self.worker is None:
            return
        self.worker.send(proto.build_set_device_param_command(name, checked))
        self.log_panel.append(f"> SET_DEVICE_PARAM {name} {'1' if checked else '0'}")

    def _handle_ready_ack(self, msg: dict[str, Any]) -> None:
        if msg.get("ok") is not True:
            self.upgrade_panel.set_status("准备失败", "请重新上电设备后再试。", None)
            return

        self.handshake_timer.stop()
        self.handshake_active = False
        self.ota_ready = True
        if self.firmware_info is None:
            self.upgrade_panel.set_status("设备已就绪", "请选择升级文件。", 0)
        else:
            self.upgrade_panel.set_status("可以升级", "设备和升级文件已准备好，请点击“开始升级”。", 0)
        self.log_panel.append("Device is ready for serial OTA")
        self._refresh_ready_state()

    def _handle_status(self, msg: dict[str, Any]) -> None:
        state = str(msg.get("state", "unknown"))
        progress = int(msg.get("progress", 0))
        self.upgrade_panel.set_status(self._friendly_state(state), self._friendly_detail(state), progress)

    def _handle_start_ack(self, msg: dict[str, Any]) -> None:
        if not self.start_pending:
            if msg.get("ok") is not True:
                self.log_panel.append(f"Unexpected command response: {msg.get('msg', 'bad command')}")
            return

        self.start_ack_timer.stop()
        if msg.get("ok") is not True:
            self.ota_ready = False
            self._stop_upgrade("升级失败", "设备没有接受升级，请重新连接后再试。", None)
            return

        self.log_panel.append("Start command accepted")
        self._begin_transfer()

    def _handle_chunk_ack(self, msg: dict[str, Any]) -> None:
        if not self.upgrading or not self.waiting_ack:
            return
        if msg.get("ok") is not True:
            self._stop_upgrade("升级失败", "数据传输失败，请重新连接后再试。", None)
            return

        self.ack_timer.stop()
        self.waiting_ack = False
        self.current_retries = 0
        self.offset = int(msg.get("nextOffset", self.offset))
        self.seq += 1
        self._update_transfer_progress()
        self._send_next_frame()

    def _handle_done(self, msg: dict[str, Any]) -> None:
        self._stop_timers()
        self.upgrading = False
        self.start_pending = False
        self.waiting_ack = False
        self.upgrade_panel.set_running(False)
        if msg.get("ok") is True:
            self.ota_ready = False
            self.upgrade_panel.set_status("升级完成", "升级已完成，请稍等设备重新启动。", 100)
            self.log_panel.append("Upgrade done")
        else:
            self.upgrade_panel.set_status("升级失败", "设备升级失败，请重新连接后再试。", None)
            self.log_panel.append(f"Upgrade failed: {msg.get('msg', 'done error')}")
        self._refresh_ready_state()

    def _send_probe_commands(self) -> None:
        if not self.worker:
            return
        self.worker.send(proto.build_device_info_command())
        self.worker.send(proto.build_enter_command())
        self.handshake_send_count += 1
        if self.handshake_send_count == 1 or self.handshake_send_count % 10 == 0:
            self.log_panel.append("> GET_DEVICE_INFO / ENTER_OTA_SERIAL")

    def _send_start_command(self) -> None:
        if not self.worker or not self.firmware_info:
            return
        self.worker.send(proto.build_start_command(self.firmware_info.size, self.firmware_info.sha256))
        self.log_panel.append(
            f"> START_OTA_SERIAL(size={self.firmware_info.size}, sha256={self.firmware_info.sha256[:8]}...)"
        )

    def _begin_auto_handshake(self, force: bool = False) -> None:
        if not self.connected or self.worker is None:
            return
        if self.ota_ready and not force:
            return
        if self.handshake_active and not force:
            return

        self.ota_ready = False
        self.handshake_active = True
        self.handshake_send_count = 0
        self.upgrade_panel.set_status("正在准备设备", "请保持设备连接，必要时重新上电设备。", 0)
        self.log_panel.append("Auto enter started")
        self._send_probe_commands()
        self.handshake_timer.start()

    def _send_next_frame(self) -> None:
        if not self.worker or self.firmware_data is None:
            return
        if self.waiting_ack:
            return

        total = len(self.firmware_data)
        if self.offset >= total:
            frame = proto.build_end_frame(self.seq, self.offset)
            self._write_frame(frame, "END", timeout_ms=12000)
            return

        end = min(self.offset + self.chunk_size, total)
        chunk = self.firmware_data[self.offset:end]
        frame = proto.build_data_frame(self.seq, self.offset, chunk)
        self._write_frame(frame, f"chunk seq={self.seq} offset={self.offset} len={len(chunk)}", timeout_ms=2500)

    def _write_frame(self, frame: bytes, label: str, timeout_ms: int) -> None:
        if not self.worker:
            return
        self.current_frame = frame
        self.current_label = label
        self.current_retries = 0
        self.waiting_ack = True
        self.worker.send(frame)
        self.ack_timer.setInterval(timeout_ms)
        self.ack_timer.start()
        if label == "END" or self.seq % 16 == 0:
            self.log_panel.append(f"> {label}")

    def _retry_current_frame(self) -> None:
        if not self.worker or not self.waiting_ack or not self.current_frame:
            self.ack_timer.stop()
            return
        if self.current_retries >= 5:
            self._stop_upgrade("升级失败", "等待设备响应超时，请重新连接后再试。", None)
            return
        self.current_retries += 1
        self.worker.send(self.current_frame)
        self.log_panel.append(f"Retry {self.current_label} ({self.current_retries}/5)")

    def _handle_start_timeout(self) -> None:
        if self.start_pending:
            self._stop_upgrade("升级失败", "设备响应超时，请重新连接后再试。", None)

    def _update_transfer_progress(self) -> None:
        if not self.firmware_data:
            return
        total = len(self.firmware_data)
        percent = int((self.offset * 100) / total) if total else 0
        self.upgrade_panel.set_status("正在升级", "请不要拔掉设备。", percent)

    def _stop_upgrade(self, state: str, detail: str, progress: int | None) -> None:
        self.start_ack_timer.stop()
        self.ack_timer.stop()
        self.upgrading = False
        self.start_pending = False
        self.waiting_ack = False
        self.upgrade_panel.set_running(False)
        self.upgrade_panel.set_status(state, detail, progress)
        self._refresh_ready_state()

    def _stop_timers(self) -> None:
        self.handshake_timer.stop()
        self.start_ack_timer.stop()
        self.ack_timer.stop()

    def _refresh_ready_state(self) -> None:
        ready = (
            self.connected
            and self.firmware_info is not None
            and self.ota_ready
            and not self.upgrading
            and not self.start_pending
        )
        self.upgrade_panel.set_ready(ready)

    @staticmethod
    def _friendly_state(state: str) -> str:
        mapping = {
            "erasing": "正在准备升级",
            "ready": "正在准备升级",
            "writing": "正在升级",
            "finalizing": "正在完成升级",
            "verifying": "正在完成升级",
            "committing": "正在完成升级",
            "rebooting": "升级完成",
        }
        return mapping.get(state, "正在升级")

    @staticmethod
    def _friendly_detail(state: str) -> str:
        if state == "rebooting":
            return "升级已完成，请稍等设备重新启动。"
        if state in {"finalizing", "verifying", "committing"}:
            return "正在完成最后处理，请不要拔掉设备。"
        return "请不要拔掉设备。"

    @staticmethod
    def _friendly_error(message: str) -> str:
        if "PermissionError" in message or "拒绝访问" in message:
            return "设备连接中断，请重新插拔设备后再试。"
        if "open" in message.lower() or "打开" in message:
            return "设备连接失败，请确认设备已插好并关闭其他串口工具。"
        return "升级过程异常，请重新连接后再试。"

    def closeEvent(self, event) -> None:  # noqa: N802
        self.close_serial()
        event.accept()
