from __future__ import annotations

from pathlib import Path

from PyQt5.QtCore import pyqtSignal
from PyQt5.QtWidgets import QFileDialog, QFrame, QGridLayout, QLabel, QLineEdit, QPushButton, QVBoxLayout

from firmware import FIRMWARE_FILE_FILTER, load_firmware
from ota_protocol import FirmwareInfo


class FirmwarePanel(QFrame):
    firmware_loaded = pyqtSignal(object, bytes)
    load_failed = pyqtSignal(str)

    def __init__(self, parent=None) -> None:
        super().__init__(parent)
        self.setProperty("panel", True)
        self.info: FirmwareInfo | None = None
        self.data: bytes | None = None

        root = QVBoxLayout(self)
        root.setContentsMargins(16, 14, 16, 14)
        root.setSpacing(10)

        title = QLabel("2. 选择升级文件")
        title.setStyleSheet("font-size: 16px; font-weight: 600;")
        root.addWidget(title)

        grid = QGridLayout()
        grid.setHorizontalSpacing(10)
        grid.setVerticalSpacing(8)
        root.addLayout(grid)

        self.path_edit = QLineEdit()
        self.path_edit.setReadOnly(True)
        self.path_edit.setPlaceholderText("请选择厂家提供的升级固件")
        self.choose_btn = QPushButton("选择固件")
        self.choose_btn.clicked.connect(self.choose_file)
        grid.addWidget(QLabel("升级文件"), 0, 0)
        grid.addWidget(self.path_edit, 0, 1)
        grid.addWidget(self.choose_btn, 0, 2)

        self.tip_label = QLabel("选择文件后，再点击右侧“开始升级”。")
        self.tip_label.setProperty("muted", True)
        root.addWidget(self.tip_label)

    def choose_file(self) -> None:
        path, _ = QFileDialog.getOpenFileName(self, "选择升级文件", "", FIRMWARE_FILE_FILTER)
        if not path:
            return
        try:
            info, data = load_firmware(path)
        except Exception as exc:
            self.load_failed.emit(str(exc))
            return

        self.info = info
        self.data = data
        self.path_edit.setText(Path(info.path).name)
        self.tip_label.setText("升级文件已选择，可以开始升级。")
        self.firmware_loaded.emit(info, data)
