from __future__ import annotations

import logging
import sys
import time

from diagnostics import install_diagnostics, log_exception, show_startup_error


def main() -> None:
    t0 = time.perf_counter()
    log_path = install_diagnostics("FirmwareUpdater")
    try:
        logging.info("stage diagnostics installed %.3fs", time.perf_counter() - t0)

        from PyQt5.QtWidgets import QApplication

        logging.info("stage imported QApplication %.3fs", time.perf_counter() - t0)

        from main_window import MainWindow
        from style import QSS

        logging.info("stage imported window/style %.3fs", time.perf_counter() - t0)

        app = QApplication(sys.argv)
        logging.info("stage QApplication created %.3fs", time.perf_counter() - t0)

        app.setStyleSheet(QSS)
        window = MainWindow()
        logging.info("stage MainWindow created %.3fs", time.perf_counter() - t0)

        window.show()
        logging.info("stage window shown %.3fs", time.perf_counter() - t0)
        sys.exit(app.exec_())
    except Exception:
        log_exception("startup failed")
        show_startup_error(log_path)
        raise


if __name__ == "__main__":
    main()
