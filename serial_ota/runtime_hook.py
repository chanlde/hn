from __future__ import annotations

import os
import sys
import time
from pathlib import Path


def _write_early_log() -> None:
    try:
        base = os.environ.get("LOCALAPPDATA") or str(Path.home())
        path = Path(base) / "SolarClean" / "FirmwareUpdater"
        path.mkdir(parents=True, exist_ok=True)
        with open(path / "early_startup.log", "a", encoding="utf-8", buffering=1) as f:
            f.write(
                f"{time.strftime('%Y-%m-%d %H:%M:%S')} "
                f"runtime_hook exe={sys.executable} cwd={os.getcwd()} "
                f"frozen={getattr(sys, 'frozen', False)} "
                f"_MEIPASS={getattr(sys, '_MEIPASS', '')}\n"
            )
    except Exception:
        pass


_write_early_log()
