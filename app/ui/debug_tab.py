from __future__ import annotations

from datetime import datetime
from typing import Optional

from PyQt6.QtCore import Qt, pyqtSignal, QObject
from PyQt6.QtGui import QFont, QTextCursor
from PyQt6.QtWidgets import (
    QCheckBox,
    QComboBox,
    QHBoxLayout,
    QLabel,
    QPlainTextEdit,
    QPushButton,
    QVBoxLayout,
    QWidget,
)

try:
    from .theme import (
        COLOR_BG_DARK, COLOR_BG_PANEL, COLOR_TEXT_PRIMARY,
        COLOR_TEXT_SECONDARY, COLOR_ACCENT, COLOR_RIM,
    )
except ImportError:
    from ui.theme import (  # type: ignore[no-redef]
        COLOR_BG_DARK, COLOR_BG_PANEL, COLOR_TEXT_PRIMARY,
        COLOR_TEXT_SECONDARY, COLOR_ACCENT, COLOR_RIM,
    )

try:
    from ..protocol.sysex import (
        CAT_SYS, CAT_PAD, CAT_MIDI, CAT_PRESET, CAT_STATUS,
        SYS_IDENT_RESP,
        ZONE_NAMES,
        parse_identify_response, parse_pad_config_response,
        parse_input_status_response, parse_midi_mapping_response,
        parse_hit_event,
    )
except ImportError:
    from protocol.sysex import (  # type: ignore[no-redef]
        CAT_SYS, CAT_PAD, CAT_MIDI, CAT_PRESET, CAT_STATUS,
        SYS_IDENT_RESP,
        ZONE_NAMES,
        parse_identify_response, parse_pad_config_response,
        parse_input_status_response, parse_midi_mapping_response,
        parse_hit_event,
    )

_MAX_LINES = 1000

_CAT_NAMES = {
    CAT_SYS:    "SYS",
    CAT_PAD:    "PAD",
    CAT_MIDI:   "MIDI",
    CAT_PRESET: "PRE",
    CAT_STATUS: "STATUS",
}

_FILTER_ALL      = "All messages"
_FILTER_TX       = "Sent only"
_FILTER_RX       = "Received only"
_FILTER_HIT      = "Hit events only"


def _ts() -> str:
    now = datetime.now()
    return now.strftime("%H:%M:%S.") + f"{now.microsecond // 1000:03d}"


def _hex(data) -> str:
    return " ".join(f"{b:02X}" for b in data)


def _summarise(direction: str, parsed: dict) -> str:
    hi  = parsed.get("cmd_high", 0)
    lo  = parsed.get("cmd_low", 0)
    pay = parsed.get("payload", b"")

    try:
        if hi == CAT_SYS and lo == 0x01:
            return "ping"
        if hi == CAT_SYS and lo == 0x02:
            return "pong"
        if hi == CAT_SYS and lo == 0x03:
            return "identify request"
        if hi == CAT_SYS and lo == SYS_IDENT_RESP and len(pay) >= 4:
            r = parse_identify_response(pay)
            return (f"FW v{r['fw_maj']}.{r['fw_min']} "
                    f"device=0x{r['device_id']:02X} inputs={r['num_inputs']}")
        if hi == CAT_PAD and lo == 0x07 and len(pay) >= 1:
            return f"pad config input={pay[0]}"
        if hi == CAT_PAD and lo == 0x0A and len(pay) >= 2:
            r = parse_input_status_response(pay)
            return f"input status input={r['input_id']} status={r['status_name']}"
        if hi == CAT_MIDI and lo == 0x05 and len(pay) >= 1:
            return f"midi mapping input={pay[0]}"
        if hi == CAT_STATUS and lo == 0x03 and len(pay) >= 3:
            r = parse_hit_event(pay)
            zone = ZONE_NAMES.get(r["zone"], f"0x{r['zone']:02X}")
            return f"input={r['input_id']} zone={zone} vel={r['velocity']}"
        if hi == CAT_STATUS and lo == 0x01 and len(pay) >= 3:
            status = {0: "ok", 1: "error", 2: "unknown"}.get(pay[2], f"0x{pay[2]:02X}")
            return f"ack cmd={pay[0]:02X} {pay[1]:02X} status={status}"
    except Exception:
        pass

    return f"(cmd {hi:02X} {lo:02X})"


def _category_label(parsed: dict) -> str:
    hi = parsed.get("cmd_high", 0)
    lo = parsed.get("cmd_low", 0)
    if hi == CAT_STATUS and lo == 0x03:
        return "HIT"
    return _CAT_NAMES.get(hi, f"{hi:02X}")


class _SignalBridge(QObject):
    log_line = pyqtSignal(str)


class DebugTab(QWidget):
    def __init__(self, parent: Optional[QWidget] = None) -> None:
        super().__init__(parent)
        self._paused   = False
        self._buffer:  list[str] = []
        self._bridge   = _SignalBridge()
        self._bridge.log_line.connect(self._append_line)
        self._build_ui()

    # ------------------------------------------------------------------
    # UI construction
    # ------------------------------------------------------------------

    def _build_ui(self) -> None:
        layout = QVBoxLayout(self)
        layout.setContentsMargins(6, 6, 6, 6)
        layout.setSpacing(4)

        # Toolbar row
        toolbar = QHBoxLayout()
        toolbar.setSpacing(6)

        clear_btn = QPushButton("Clear")
        clear_btn.setFixedWidth(60)
        clear_btn.clicked.connect(self._clear)
        toolbar.addWidget(clear_btn)

        self._pause_btn = QPushButton("Pause")
        self._pause_btn.setCheckable(True)
        self._pause_btn.setFixedWidth(60)
        self._pause_btn.toggled.connect(self._on_pause_toggled)
        toolbar.addWidget(self._pause_btn)

        copy_btn = QPushButton("Copy All")
        copy_btn.setFixedWidth(70)
        copy_btn.clicked.connect(self._copy_all)
        toolbar.addWidget(copy_btn)

        toolbar.addWidget(QLabel("Filter:"))
        self._filter_combo = QComboBox()
        self._filter_combo.addItems([
            _FILTER_ALL, _FILTER_TX, _FILTER_RX, _FILTER_HIT,
        ])
        self._filter_combo.setFixedWidth(160)
        toolbar.addWidget(self._filter_combo)

        toolbar.addStretch()

        self._autoscroll_cb = QCheckBox("Auto-scroll")
        self._autoscroll_cb.setChecked(True)
        toolbar.addWidget(self._autoscroll_cb)

        layout.addLayout(toolbar)

        # Log area
        self._log = QPlainTextEdit()
        self._log.setReadOnly(True)
        font = QFont("Courier New", 9)
        font.setStyleHint(QFont.StyleHint.Monospace)
        self._log.setFont(font)
        self._log.setStyleSheet(
            f"QPlainTextEdit {{"
            f"  background-color: {COLOR_BG_DARK};"
            f"  color: {COLOR_TEXT_PRIMARY};"
            f"  border: 1px solid #333;"
            f"}}"
        )
        self._log.setMaximumBlockCount(_MAX_LINES)
        layout.addWidget(self._log)

    # ------------------------------------------------------------------
    # Public interface
    # ------------------------------------------------------------------

    def log_tx(self, raw_bytes: bytearray) -> None:
        """Log an outgoing (TX) message."""
        from PyQt6.QtWidgets import QApplication as _QApp
        parsed = {
            "cmd_high": raw_bytes[4] if len(raw_bytes) > 4 else 0,
            "cmd_low":  raw_bytes[5] if len(raw_bytes) > 5 else 0,
            "payload":  bytes(raw_bytes[6:-1]) if len(raw_bytes) > 7 else b"",
        }
        self._emit_line("TX", parsed, bytes(raw_bytes))

    def log_rx(self, parsed: dict, raw_bytes: bytes) -> None:
        """Log an incoming (RX) message."""
        self._emit_line("RX", parsed, raw_bytes)

    def on_connected(self) -> None:
        self._append_line(f"[{_ts()}] --- connected ---")

    def on_disconnected(self) -> None:
        self._append_line(f"[{_ts()}] --- disconnected ---")

    # ------------------------------------------------------------------
    # Internal helpers
    # ------------------------------------------------------------------

    def _emit_line(self, direction: str, parsed: dict, raw: bytes) -> None:
        filt = self._filter_combo.currentText()
        if filt == _FILTER_TX and direction != "TX":
            return
        if filt == _FILTER_RX and direction != "RX":
            return
        hi = parsed.get("cmd_high", 0)
        lo = parsed.get("cmd_low", 0)
        if filt == _FILTER_HIT and not (hi == CAT_STATUS and lo == 0x03):
            return

        cat     = _category_label(parsed)
        summary = _summarise(direction, parsed)
        hex_str = _hex(raw)
        line    = f"[{_ts()}] {direction:<3} {cat:<7} {hex_str:<50}  {summary}"
        self._bridge.log_line.emit(line)

    def _append_line(self, line: str) -> None:
        if self._paused:
            self._buffer.append(line)
            return
        self._log.appendPlainText(line)
        if self._autoscroll_cb.isChecked():
            self._log.moveCursor(QTextCursor.MoveOperation.End)

    def _on_pause_toggled(self, checked: bool) -> None:
        self._paused = checked
        if not checked and self._buffer:
            for line in self._buffer:
                self._log.appendPlainText(line)
            self._buffer.clear()
            if self._autoscroll_cb.isChecked():
                self._log.moveCursor(QTextCursor.MoveOperation.End)

    def _clear(self) -> None:
        self._log.clear()
        self._buffer.clear()

    def _copy_all(self) -> None:
        from PyQt6.QtWidgets import QApplication as _QApp
        _QApp.clipboard().setText(self._log.toPlainText())
