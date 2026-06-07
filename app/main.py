"""
eDrum Config — application entry point.

Run from the project root:
    app\\venv\\Scripts\\python.exe app\\main.py
"""
from __future__ import annotations

import os
import sys

# Add the app/ directory to sys.path so sub-packages resolve when run as a
# plain script (python app/main.py) rather than via the package system.
_APP_DIR = os.path.dirname(os.path.abspath(__file__))
if _APP_DIR not in sys.path:
    sys.path.insert(0, _APP_DIR)

from PyQt6.QtWidgets import QApplication
from ui.main_window import MainWindow


def main() -> int:
    app = QApplication(sys.argv)
    app.setApplicationName("eDrum Config")
    app.setOrganizationName("eDrum")

    window = MainWindow()
    window.show()

    return app.exec()


if __name__ == "__main__":
    sys.exit(main())
