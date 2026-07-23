from __future__ import annotations

from PyQt5.QtCore import pyqtSignal
from PyQt5.QtWidgets import QFrame, QHBoxLayout, QLabel, QProgressBar, QPushButton, QVBoxLayout


class UpgradePanel(QFrame):
    start_requested = pyqtSignal()
    cancel_requested = pyqtSignal()

    def __init__(self, parent=None) -> None:
        super().__init__(parent)
        self.setProperty("panel", True)

        root = QVBoxLayout(self)
        root.setContentsMargins(16, 14, 16, 14)
        root.setSpacing(12)

        title = QLabel("3. 开始升级")
        title.setStyleSheet("font-size: 16px; font-weight: 600;")
        root.addWidget(title)

        self.state_label = QLabel("等待操作")
        self.state_label.setStyleSheet("font-size: 20px; font-weight: 700;")
        root.addWidget(self.state_label)

        self.detail_label = QLabel("请先连接设备并选择升级文件。")
        self.detail_label.setProperty("muted", True)
        self.detail_label.setWordWrap(True)
        root.addWidget(self.detail_label)

        self.progress = QProgressBar()
        self.progress.setRange(0, 100)
        self.progress.setValue(0)
        self.progress.setFormat("0%")
        root.addWidget(self.progress)

        row = QHBoxLayout()
        self.start_btn = QPushButton("开始升级")
        self.start_btn.setProperty("primary", True)
        self.start_btn.clicked.connect(self.start_requested)
        row.addWidget(self.start_btn)

        self.cancel_btn = QPushButton("停止")
        self.cancel_btn.clicked.connect(self.cancel_requested)
        self.cancel_btn.setEnabled(False)
        row.addWidget(self.cancel_btn)
        row.addStretch(1)
        root.addLayout(row)

    def set_ready(self, ready: bool) -> None:
        self.start_btn.setEnabled(ready)

    def set_running(self, running: bool) -> None:
        self.start_btn.setEnabled(not running)
        self.cancel_btn.setEnabled(running)

    def set_status(self, state: str, detail: str = "", progress: int | None = None) -> None:
        self.state_label.setText(state)
        self.detail_label.setText(detail)
        if progress is not None:
            progress = max(0, min(100, int(progress)))
            self.progress.setValue(progress)
            self.progress.setFormat(f"{progress}%")
