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
        DEV_HEAD, CAT_STATUS, STAT_HIT_DEBUG, ZONE_HEAD, ZONE_RIM,
        PAD_TYPE_PIEZO_RIM, PAD_TYPE_DUAL_PIEZO,
        build_message, parse_message,
    )
    from ..ui.theme import apply_dark_theme
    from .transport import EmulatorTransport
except ImportError:
    from protocol.sysex import (  # type: ignore[no-redef]
        DEV_HEAD, CAT_STATUS, STAT_HIT_DEBUG, ZONE_HEAD, ZONE_RIM,
        PAD_TYPE_PIEZO_RIM, PAD_TYPE_DUAL_PIEZO,
        build_message, parse_message,
    )
    from ui.theme import apply_dark_theme  # type: ignore[no-redef]
    from emulator.transport import EmulatorTransport  # type: ignore[no-redef]

_APP_DIR        = os.path.normpath(os.path.join(os.path.dirname(__file__), ".."))
_PAD_NAMES_PATH = os.path.join(_APP_DIR, "pad_names.json")

_NUM_INPUTS  = 9
_COLS        = 3
_DUAL_ZONE_TYPES = {PAD_TYPE_PIEZO_RIM, PAD_TYPE_DUAL_PIEZO}


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
        self._transport   = transport
        self._pad_names   = _load_pad_names()
        self._sliders:    dict[int, QSlider] = {}
        self._vel_labels: dict[int, QLabel]  = {}
        self._rim_btns:   dict[int, QPushButton] = {}

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

            # Head button
            head_btn = QPushButton(name)
            head_btn.setMinimumHeight(36)
            head_btn.clicked.connect(
                lambda checked, i=input_id: self._on_hit(i, ZONE_HEAD))
            cell.addWidget(head_btn)

            # Rim button — visible only for dual-zone pad types
            rim_btn = QPushButton("Rim")
            rim_btn.setMinimumHeight(28)
            rim_btn.clicked.connect(
                lambda checked, i=input_id: self._on_hit(i, ZONE_RIM))
            is_dual = self._is_dual_zone(input_id)
            rim_btn.setVisible(is_dual)
            self._rim_btns[input_id] = rim_btn
            cell.addWidget(rim_btn)

            # Velocity slider
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
    # Pad type helpers
    # ------------------------------------------------------------------

    def _is_dual_zone(self, input_id: int) -> bool:
        """Return True if the emulator device has this input set to a dual-zone type."""
        pad_type = self._transport._device._inputs.get(input_id, {}).get("pad_type", 0)
        return pad_type in _DUAL_ZONE_TYPES

    def refresh_pad_types(self) -> None:
        """Update rim button visibility to match current device config.
        Call this after a config refresh if pad types may have changed."""
        for input_id, btn in self._rim_btns.items():
            btn.setVisible(self._is_dual_zone(input_id))
        self.adjustSize()

    # ------------------------------------------------------------------
    # Hit injection
    # ------------------------------------------------------------------

    def _on_hit(self, input_id: int, zone: int) -> None:
        vel      = self._sliders[input_id].value()
        name     = self._pad_names.get(input_id, f"Input {input_id}")

        # Don't send rim hits for single-zone pads
        if zone == ZONE_RIM and not self._is_dual_zone(input_id):
            self._status_lbl.setText(
                f"Rim blocked: {name} (input {input_id}) is not dual-zone"
            )
            return
    
        midi_vel = self._transport._device.apply_curve(input_id, vel)

        msg = build_message(DEV_HEAD, CAT_STATUS, STAT_HIT_DEBUG,
                            [input_id, zone, vel, midi_vel])
        parsed = parse_message(msg)
        if parsed is not None:
            self._transport._dispatch_to_listeners(parsed)

        zone_name = "rim" if zone == ZONE_RIM else "head"
        self._status_lbl.setText(
            f"Hit: {name} (input {input_id}) {zone_name} vel={vel}"
        )
        log.debug("Emulated hit: input=%d zone=%s raw=%d midi=%d",
                  input_id, zone_name, vel, midi_vel)

    # ------------------------------------------------------------------
    # Window lifecycle
    # ------------------------------------------------------------------

    def closeEvent(self, event) -> None:
        event.ignore()
        self.hide()