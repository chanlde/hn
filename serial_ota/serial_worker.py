from __future__ import annotations

import queue
from typing import Optional

import serial
from PyQt5.QtCore import QThread, pyqtSignal

from ota_protocol import parse_json_line


class SerialWorker(QThread):
    connected = pyqtSignal(str)
    disconnected = pyqtSignal()
    error = pyqtSignal(str)
    text_received = pyqtSignal(str)
    message_received = pyqtSignal(dict)

    def __init__(self, port: str, baudrate: int, parent=None) -> None:
        super().__init__(parent)
        self._port = port
        self._baudrate = baudrate
        self._running = False
        self._serial: Optional[serial.Serial] = None
        self._tx_queue: queue.Queue[bytes] = queue.Queue()
        self._rx_text = ""

    def run(self) -> None:
        try:
            self._serial = serial.Serial(self._port, self._baudrate, timeout=0, write_timeout=1)
        except Exception as exc:
            self.error.emit(f"打开串口失败: {exc}")
            return

        self._running = True
        self.connected.emit(self._port)

        try:
            while self._running:
                self._flush_tx()
                self._read_rx()
                self.msleep(5)
        finally:
            try:
                if self._serial and self._serial.is_open:
                    self._serial.close()
            except Exception:
                pass
            self._serial = None
            self.disconnected.emit()

    def stop(self) -> None:
        self._running = False

    def send(self, data: bytes) -> None:
        if data:
            self._tx_queue.put(data)

    def _flush_tx(self) -> None:
        if not self._serial or not self._serial.is_open:
            return

        while True:
            try:
                data = self._tx_queue.get_nowait()
            except queue.Empty:
                break
            try:
                self._serial.write(data)
            except Exception as exc:
                self.error.emit(f"串口发送失败: {exc}")
                self._running = False
                break

    def _read_rx(self) -> None:
        if not self._serial or not self._serial.is_open:
            return

        try:
            waiting = self._serial.in_waiting
            if waiting <= 0:
                return
            data = self._serial.read(waiting)
        except Exception as exc:
            self.error.emit(f"串口读取失败: {exc}；请确认设备未断开、未复位枚举、COM 未被其他软件占用")
            self._running = False
            return

        text = data.decode("utf-8", errors="replace")
        self.text_received.emit(text)
        self._rx_text += text

        while "\n" in self._rx_text:
            line, self._rx_text = self._rx_text.split("\n", 1)
            msg = parse_json_line(line)
            if msg is not None:
                self.message_received.emit(msg)
