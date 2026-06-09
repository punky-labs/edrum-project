"""
eDrum emulator window — manual pad-hit trigger for UI testing.

Shows a grid of 9 pad buttons with velocity sliders. Clicking a button
injects a synthetic 05 03 hit event through the transport listener
registry so the app reacts as if the hardware sent it.
"""
from __future__ import annotations

import json
import logging
import os
from typing import Optional

log = logging.getLogger("edrum.emulator.window")

from PyQt6.QtCore import Qt
from PyQt6.QtWidgets import (
    QApplication,
    QGridLayout,
    QHBoxLayout,
    QLabel,
    QPushButton,
    QSlider,
    QVBoxLayout,
    QWidget,
)

try:
    from ..protocol.sysex import (
        DEV_HEAD, CAT_STATUS, STAT_HIT_DEBUG, ZONE_HEAD,
        build_message, parse_message,
    )
    from ..ui.theme import apply_dark_theme
    from .transport import EmulatorTransport
except ImportError:
    from protocol.sysex import (  # type: ignore[no-redef]
        DEV_HEAD, CAT_STATUS, STAT_HIT_DEBUG, ZONE_HEAD,
        build_message, parse_message,
    )
    from ui.theme import apply_dark_theme  # type: ignore[no-redef]
    from emulator.transport import EmulatorTransport  # type: ignore[no-redef]

_APP_DIR       = os.path.normpath(os.path.join(os.path.dirname(__file__), ".."))
_PAD_NAMES_PATH = os.path.join(_APP_DIR, "pad_names.json")

_NUM_INPUTS = 9
_COLS       = 3


def _load_pad_names() -> dict[int, str]:
    try:
        with open(_PAD_NAMES_PATH, "r", encoding="utf-8") as f:
            raw = json.load(f)
        return {int(k): v for k, v in raw.items()}
    except Exception as exc:
        log.warning("Could not load pad names from %s: %s", _PAD_NAMES_PATH, exc)
        return {i: f"Input {i}" for i in range(_NUM_INPUTS)}


class EmulatorWindow(QWidget):
    """
    Standalone window for manually triggering pad hit events during
    UI testing without physical hardware.
    """

    def __init__(self, transport: EmulatorTransport,
                 parent: Optional[QWidget] = None) -> None:
        super().__init__(parent)
        self._transport  = transport
        self._pad_names  = _load_pad_names()
        self._sliders:   dict[int, QSlider] = {}
        self._vel_labels: dict[int, QLabel] = {}

        self.setWindowTitle("eDrum Emulator")
        apply_dark_theme(QApplication.instance())

        self._build_ui()
        log.debug("EmulatorWindow created")

    # ------------------------------------------------------------------
    # UI construction
    # ------------------------------------------------------------------

    def _build_ui(self) -> None:
        root = QVBoxLayout(self)
        root.setContentsMargins(8, 8, 8, 8)
        root.setSpacing(8)

        grid = QGridLayout()
        grid.setSpacing(6)

        for input_id in range(_NUM_INPUTS):
            row = input_id // _COLS
            col = input_id % _COLS

            cell = QVBoxLayout()
            cell.setSpacing(4)

            name = self._pad_names.get(input_id, f"Input {input_id}")
            btn  = QPushButton(name)
            btn.setMinimumHeight(36)
            btn.clicked.connect(lambda checked, i=input_id: self._on_hit(i))
            cell.addWidget(btn)

            slider = QSlider(Qt.Orientation.Horizontal)
            slider.setRange(1, 127)
            slider.setValue(100)
            self._sliders[input_id] = slider
            cell.addWidget(slider)

            vel_lbl = QLabel("Vel: 100")
            vel_lbl.setAlignment(Qt.AlignmentFlag.AlignCenter)
            self._vel_labels[input_id] = vel_lbl
            slider.valueChanged.connect(
                lambda val, lbl=vel_lbl: lbl.setText(f"Vel: {val}")
            )
            cell.addWidget(vel_lbl)

            container = QWidget()
            container.setLayout(cell)
            grid.addWidget(container, row, col)

        root.addLayout(grid)

        self._status_lbl = QLabel("Ready")
        self._status_lbl.setAlignment(Qt.AlignmentFlag.AlignCenter)
        root.addWidget(self._status_lbl)

        self.adjustSize()

    # ------------------------------------------------------------------
    # Hit injection
    # ------------------------------------------------------------------

    def _on_hit(self, input_id: int) -> None:
        vel  = self._sliders[input_id].value()
        name = self._pad_names.get(input_id, f"Input {input_id}")

        # Build 05 03 hit event: [input_id, zone, raw_vel, midi_vel]
        msg = build_message(DEV_HEAD, CAT_STATUS, STAT_HIT_DEBUG,
                            [input_id, ZONE_HEAD, vel, vel])
        parsed = parse_message(msg)
        if parsed is not None:
            self._transport._dispatch_to_listeners(parsed)

        self._status_lbl.setText(
            f"Hit: {name} (input {input_id}) vel={vel}"
        )
        log.debug("Emulated hit: input=%d name='%s' vel=%d", input_id, name, vel)

    # ------------------------------------------------------------------
    # Window lifecycle
    # ------------------------------------------------------------------

    def closeEvent(self, event) -> None:
        event.ignore()
        self.hide()
