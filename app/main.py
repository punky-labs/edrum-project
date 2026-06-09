"""
eDrum Config — application entry point.

Run from the project root:
    app\\venv\\Scripts\\python.exe app\\main.py
"""
from __future__ import annotations

import logging
import logging.handlers
import os
import platform
import sys

# Add the app/ directory to sys.path so sub-packages resolve when run as a
# plain script (python app/main.py) rather than via the package system.
_APP_DIR = os.path.dirname(os.path.abspath(__file__))
if _APP_DIR not in sys.path:
    sys.path.insert(0, _APP_DIR)

from PyQt6.QtWidgets import QApplication
from ui.main_window import MainWindow

# app/logs/ is git-ignored — see .gitignore


def setup_logging() -> None:
    log_dir = os.path.join(_APP_DIR, "logs")
    os.makedirs(log_dir, exist_ok=True)
    log_path = os.path.join(log_dir, "edrum.log")

    root = logging.getLogger()
    root.setLevel(logging.DEBUG)

    # Rotating file handler — DEBUG and above
    fh = logging.handlers.RotatingFileHandler(
        log_path,
        maxBytes=1 * 1024 * 1024,   # 1MB
        backupCount=3,
        encoding="utf-8",
    )
    fh.setLevel(logging.DEBUG)
    fh.setFormatter(logging.Formatter(
        "%(asctime)s.%(msecs)03d | %(levelname)-8s | %(name)-30s | %(message)s",
        datefmt="%Y-%m-%d %H:%M:%S",
    ))

    # Console handler — WARNING and above only
    ch = logging.StreamHandler()
    ch.setLevel(logging.WARNING)
    ch.setFormatter(logging.Formatter("%(levelname)s %(name)s: %(message)s"))

    root.addHandler(fh)
    root.addHandler(ch)


def main() -> int:
    setup_logging()

    log = logging.getLogger("edrum.main")
    log.info("=" * 60)
    log.info("eDrum Config starting")
    log.info("Python %s", sys.version)
    log.info("Platform: %s", platform.platform())
    log.info("App dir: %s", _APP_DIR)

    app = QApplication(sys.argv)
    app.setApplicationName("eDrum Config")
    app.setOrganizationName("eDrum")

    window = MainWindow()
    window.show()

    return app.exec()


if __name__ == "__main__":
    sys.exit(main())
