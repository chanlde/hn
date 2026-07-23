from __future__ import annotations

import logging
import time

from PyQt5.QtCore import Qt, pyqtSignal
from PyQt5.QtWidgets import QComboBox, QFrame, QGridLayout, QHBoxLayout, QLabel, QPushButton, QSpinBox, QVBoxLayout
from serial.tools import list_ports

from ota_protocol import DEFAULT_BAUDRATE, DeviceInfo


class ConnectPanel(QFrame):
    connect_requested = pyqtSignal(str, int)
    disconnect_requested = pyqtSignal()
    probe_requested = pyqtSignal()

    def __init__(self, parent=None) -> None:
        super().__init__(parent)
        self.setProperty("panel", True)
        self._connected = False

        root = QVBoxLayout(self)
        root.setContentsMargins(16, 14, 16, 14)
        root.setSpacing(10)

        title = QLabel("1. 连接设备")
        title.setStyleSheet("font-size: 16px; font-weight: 600;")
        root.addWidget(title)

        row = QHBoxLayout()
        row.setSpacing(8)
        root.addLayout(row)

        self.port_combo = QComboBox()
        self.port_combo.setMinimumWidth(300)
        row.addWidget(self.port_combo, 1)

        self.refresh_btn = QPushButton("刷新")
        self.refresh_btn.clicked.connect(self.refresh_ports)
        row.addWidget(self.refresh_btn)

        self.probe_btn = QPushButton("重新识别")
        self.probe_btn.setEnabled(False)
        self.probe_btn.clicked.connect(self.probe_requested)
        row.addWidget(self.probe_btn)

        self.baud_box = QSpinBox()
        self.baud_box.setRange(1200, 3000000)
        self.baud_box.setValue(DEFAULT_BAUDRATE)
        self.baud_box.setVisible(False)

        self.connect_btn = QPushButton("连接")
        self.connect_btn.setProperty("primary", True)
        self.connect_btn.clicked.connect(self._toggle_connection)
        row.addWidget(self.connect_btn)

        self.status_label = QLabel("请连接 USB 虚拟串口设备，然后点击连接。")
        self.status_label.setProperty("muted", True)
        root.addWidget(self.status_label)

        self.info_frame = QFrame()
        self.info_frame.setProperty("softPanel", True)
        self.info_frame.setMinimumHeight(96)
        info_grid = QGridLayout(self.info_frame)
        info_grid.setContentsMargins(14, 12, 14, 12)
        info_grid.setHorizontalSpacing(14)
        info_grid.setVerticalSpacing(8)

        self.product_type_label = QLabel("-")
        self.product_label = QLabel("-")
        self.hardware_label = QLabel("-")
        self.version_label = QLabel("-")
        self.identity_label = QLabel("-")

        for label in (
            self.product_type_label,
            self.product_label,
            self.hardware_label,
            self.version_label,
            self.identity_label,
        ):
            label.setProperty("infoValue", True)
            label.setMinimumWidth(180)
            label.setTextInteractionFlags(Qt.TextSelectableByMouse)

        self.identity_label.setMinimumWidth(420)

        info_grid.addWidget(self._make_info_label("产品类型："), 0, 0)
        info_grid.addWidget(self.product_type_label, 0, 1)
        info_grid.addWidget(self._make_info_label("产品名："), 0, 2)
        info_grid.addWidget(self.product_label, 0, 3)
        info_grid.addWidget(self._make_info_label("硬件版本："), 1, 0)
        info_grid.addWidget(self.hardware_label, 1, 1)
        info_grid.addWidget(self._make_info_label("固件版本："), 1, 2)
        info_grid.addWidget(self.version_label, 1, 3)
        info_grid.addWidget(self._make_info_label("ID / UID："), 2, 0)
        info_grid.addWidget(self.identity_label, 2, 1, 1, 3)
        info_grid.setColumnMinimumWidth(0, 90)
        info_grid.setColumnMinimumWidth(2, 90)
        info_grid.setColumnStretch(1, 1)
        info_grid.setColumnStretch(3, 1)
        root.addWidget(self.info_frame)

        self.clear_device_info()
        self.refresh_ports()

    def set_connecting(self, connecting: bool) -> None:
        self.port_combo.setEnabled(not connecting)
        self.refresh_btn.setEnabled(not connecting)
        self.probe_btn.setEnabled(False)
        self.connect_btn.setEnabled(not connecting)
        self.connect_btn.setText("连接中..." if connecting else "连接")
        self.status_label.setText("正在打开串口..." if connecting else "请连接 USB 虚拟串口设备，然后点击连接。")

    def refresh_ports(self) -> None:
        t0 = time.perf_counter()
        current = self.port_combo.currentData()
        self.port_combo.clear()
        for port in list_ports.comports():
            label = self._display_name(port)
            self.port_combo.addItem(label, port.device)
        if current:
            index = self.port_combo.findData(current)
            if index >= 0:
                self.port_combo.setCurrentIndex(index)
        if self.port_combo.count() == 0:
            self.port_combo.addItem("未发现串口设备", "")
        logging.info("stage serial ports refreshed %.3fs", time.perf_counter() - t0)

    def set_connected(self, connected: bool, port: str = "") -> None:
        self._connected = connected
        self.port_combo.setEnabled(not connected)
        self.refresh_btn.setEnabled(not connected)
        self.probe_btn.setEnabled(connected)
        self.connect_btn.setText("断开" if connected else "连接")
        self.connect_btn.setEnabled(True)
        self.connect_btn.setProperty("primary", not connected)
        self.connect_btn.setProperty("danger", connected)
        self.connect_btn.style().unpolish(self.connect_btn)
        self.connect_btn.style().polish(self.connect_btn)
        if connected:
            self.status_label.setText(f"已连接 {port}，正在读取设备信息...")
        else:
            self.status_label.setText("请连接 USB 虚拟串口设备，然后点击连接。")
            self.clear_device_info()

    def set_device_info(self, info: DeviceInfo) -> None:
        product = info.product_name or info.product_id or "未知产品"
        self.product_type_label.setText(info.product_type or info.model or "-")
        self.product_label.setText(product)
        self.hardware_label.setText(info.hardware_version or "-")
        self.version_label.setText(info.firmware_version or "-")
        self.identity_label.setText(info.serial_number or info.mcu_uid or info.device_id or "-")
        self.status_label.setText(f"已识别：{product}")

    def clear_device_info(self) -> None:
        self.product_type_label.setText("-")
        self.product_label.setText("-")
        self.hardware_label.setText("-")
        self.version_label.setText("-")
        self.identity_label.setText("-")

    def _toggle_connection(self) -> None:
        if self._connected:
            self.disconnect_requested.emit()
            return
        port = self.port_combo.currentData()
        if port:
            self.connect_requested.emit(port, int(self.baud_box.value()))

    @staticmethod
    def _display_name(port) -> str:
        vid = port.vid
        pid = port.pid
        desc = port.description or ""
        manufacturer = port.manufacturer or ""
        hwid = port.hwid or ""
        text = f"{desc} {manufacturer} {hwid}".lower()

        is_usb_serial = (
            (vid == 0x10C4 and pid == 0xEA60)
            or "cp210" in text
            or "silicon labs" in text
            or "usb serial" in text
            or "virtual com" in text
        )
        if is_usb_serial:
            return f"USB 串口 ({port.device})"

        if desc and desc != port.device:
            return f"{desc} ({port.device})"
        return port.device

    @staticmethod
    def _make_info_label(text: str) -> QLabel:
        label = QLabel(text)
        label.setProperty("infoLabel", True)
        label.setMinimumWidth(82)
        label.setAlignment(Qt.AlignRight | Qt.AlignVCenter)
        return label
