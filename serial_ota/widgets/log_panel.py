from __future__ import annotations

from datetime import datetime

from PyQt5.QtWidgets import QFrame, QHBoxLayout, QLabel, QPlainTextEdit, QPushButton, QVBoxLayout


class LogPanel(QFrame):
    def __init__(self, parent=None) -> None:
        super().__init__(parent)
        self.setProperty("panel", True)

        root = QVBoxLayout(self)
        root.setContentsMargins(16, 14, 16, 14)
        root.setSpacing(10)

        row = QHBoxLayout()
        title = QLabel("运行日志")
        title.setStyleSheet("font-size: 16px; font-weight: 600;")
        row.addWidget(title)
        row.addStretch(1)
        clear_btn = QPushButton("清空")
        clear_btn.clicked.connect(self.clear)
        row.addWidget(clear_btn)
        root.addLayout(row)

        self.text = QPlainTextEdit()
        self.text.setReadOnly(True)
        root.addWidget(self.text)

    def append(self, text: str) -> None:
        if not text:
            return
        stamp = datetime.now().strftime("%H:%M:%S")
        for line in text.rstrip().splitlines():
            self.text.appendPlainText(f"[{stamp}] {line}")
        self.text.verticalScrollBar().setValue(self.text.verticalScrollBar().maximum())

    def clear(self) -> None:
        self.text.clear()
