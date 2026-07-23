from __future__ import annotations

import faulthandler
import logging
import os
import sys
import traceback
from pathlib import Path

_LOG_FILE = None


def _log_dir(app_name: str) -> Path:
    base = os.environ.get("LOCALAPPDATA") or str(Path.home())
    path = Path(base) / "SolarClean" / app_name
    path.mkdir(parents=True, exist_ok=True)
    return path


def install_diagnostics(app_name: str) -> Path:
    global _LOG_FILE
    log_path = _log_dir(app_name) / "startup.log"
    _LOG_FILE = open(log_path, "a", encoding="utf-8", buffering=1)

    logging.basicConfig(
        filename=str(log_path),
        level=logging.INFO,
        format="%(asctime)s [%(levelname)s] %(message)s",
    )
    sys.stderr = _LOG_FILE

    try:
        faulthandler.enable(file=_LOG_FILE)
    except Exception:
        pass

    logging.info("==== application start ====")
    logging.info("executable=%s", sys.executable)
    logging.info("argv=%s", sys.argv)
    logging.info("cwd=%s", os.getcwd())
    logging.info("frozen=%s", getattr(sys, "frozen", False))
    logging.info("_MEIPASS=%s", getattr(sys, "_MEIPASS", ""))

    def _hook(exc_type, exc, tb):
        logging.critical("unhandled exception", exc_info=(exc_type, exc, tb))

    sys.excepthook = _hook
    return log_path


def log_exception(title: str) -> None:
    logging.critical("%s\n%s", title, traceback.format_exc())


def show_startup_error(log_path: Path) -> None:
    try:
        import ctypes

        ctypes.windll.user32.MessageBoxW(
            None,
            f"程序启动失败。\n请把这个日志文件发给技术人员：\n{log_path}",
            "SolarClean 固件升级工具",
            0x10,
        )
    except Exception:
        pass
