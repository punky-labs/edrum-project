from __future__ import annotations

import threading
from typing import Optional

from PyQt6.QtCore import (
    Qt, QThread, pyqtSignal, QObject, QSize,
)
from PyQt6.QtGui import QColor, QFont, QPainter, QPen, QBrush
from PyQt6.QtWidgets import (
    QComboBox,
    QGridLayout,
    QGroupBox,
    QHBoxLayout,
    QLabel,
    QLineEdit,
    QPlainTextEdit,
    QProgressBar,
    QPushButton,
    QSizePolicy,
    QSplitter,
    QStackedWidget,
    QTabWidget,
    QVBoxLayout,
    QWidget,
)

try:
    from .theme import (
        COLOR_BG_DARK, COLOR_BG_PANEL, COLOR_BG_CARD, COLOR_BG_CARD_SEL,
        COLOR_BG_INPUT, COLOR_TEXT_PRIMARY, COLOR_TEXT_SECONDARY,
        COLOR_TEXT_DISABLED, COLOR_ACCENT, COLOR_RIM, COLOR_BORDER,
        COLOR_HIT_HEAD, COLOR_HIT_RIM,
        FONT_LABEL_SIZE, FONT_VALUE_SIZE, FONT_TITLE_SIZE,
        CARD_MIN_WIDTH, CARD_MIN_HEIGHT, HIT_LOG_BARS,
    )
    from .pad_names import PAD_NAMES, load_pad_names, save_pad_names
except ImportError:
    from ui.theme import (  # type: ignore[no-redef]
        COLOR_BG_DARK, COLOR_BG_PANEL, COLOR_BG_CARD, COLOR_BG_CARD_SEL,
        COLOR_BG_INPUT, COLOR_TEXT_PRIMARY, COLOR_TEXT_SECONDARY,
        COLOR_TEXT_DISABLED, COLOR_ACCENT, COLOR_RIM, COLOR_BORDER,
        COLOR_HIT_HEAD, COLOR_HIT_RIM,
        FONT_LABEL_SIZE, FONT_VALUE_SIZE, FONT_TITLE_SIZE,
        CARD_MIN_WIDTH, CARD_MIN_HEIGHT, HIT_LOG_BARS,
    )
    from ui.pad_names import PAD_NAMES, load_pad_names, save_pad_names  # type: ignore[no-redef]

try:
    from ..protocol.sysex import (
        CAT_PAD, CAT_MIDI, CAT_STATUS,
        PAD_TYPE_NAMES, CURVE_NAMES,
        PAD_TYPE_PIEZO_RIM, PAD_TYPE_DUAL_PIEZO,
        PAD_TYPE_HIHAT_CC, PAD_TYPE_HIHAT_SW,
        ZONE_HEAD, ZONE_RIM,
        build_get_pad_config, build_get_midi_mapping, build_get_input_status,
        parse_pad_config_response, parse_midi_mapping_response,
        parse_input_status_response, parse_hit_event,
        INPUT_RESERVED,
    )
except ImportError:
    from protocol.sysex import (  # type: ignore[no-redef]
        CAT_PAD, CAT_MIDI, CAT_STATUS,
        PAD_TYPE_NAMES, CURVE_NAMES,
        PAD_TYPE_PIEZO_RIM, PAD_TYPE_DUAL_PIEZO,
        PAD_TYPE_HIHAT_CC, PAD_TYPE_HIHAT_SW,
        ZONE_HEAD, ZONE_RIM,
        build_get_pad_config, build_get_midi_mapping, build_get_input_status,
        parse_pad_config_response, parse_midi_mapping_response,
        parse_input_status_response, parse_hit_event,
        INPUT_RESERVED,
    )

try:
    from ..transport.midi import DrumMidiTransport
except ImportError:
    from transport.midi import DrumMidiTransport  # type: ignore[no-redef]

_DUAL_ZONE_TYPES = {PAD_TYPE_PIEZO_RIM, PAD_TYPE_DUAL_PIEZO}
_HIHAT_TYPES     = {PAD_TYPE_HIHAT_CC, PAD_TYPE_HIHAT_SW}

_CURVE_DESCRIPTIONS = {
    "Natural":    "Even response — what you play is what you get",
    "Expressive": "Wide dynamics — easy to play softly",
    "Sensitive":  "Very touch-responsive — rewards light playing",
    "Punchy":     "Present on moderate hits — loud and direct",
    "Aggressive": "Maximum punch — less dynamic variation",
    "Custom":     "Custom curve",
}


def midi_note_name(note: int) -> str:
    """Return note name e.g. 60 -> 'C3', 38 -> 'D2' (middle C = C3 = 60)."""
    names = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]
    octave = (note // 12) - 2
    return f"{names[note % 12]}{octave}"


# ---------------------------------------------------------------------------
# InputCard
# ---------------------------------------------------------------------------

class InputCard(QWidget):
    clicked = pyqtSignal(int)

    def __init__(self, input_id: int, parent: Optional[QWidget] = None) -> None:
        super().__init__(parent)
        self.setObjectName("InputCard")
        self.setAttribute(Qt.WidgetAttribute.WA_StyledBackground, True)
        self.setStyleSheet("QLabel { background: transparent; }")
        self._input_id  = input_id
        self._selected  = False
        self._reserved  = False
        self._name      = "Unassigned"
        self._type_name = ""

        self.setMinimumSize(CARD_MIN_WIDTH, CARD_MIN_HEIGHT)
        self.setSizePolicy(QSizePolicy.Policy.Expanding, QSizePolicy.Policy.Preferred)
        self.setCursor(Qt.CursorShape.PointingHandCursor)

        layout = QVBoxLayout(self)
        layout.setContentsMargins(8, 6, 8, 6)
        layout.setSpacing(2)

        num_lbl = QLabel(str(input_id))
        num_lbl.setStyleSheet(
            f"color: {COLOR_TEXT_SECONDARY}; font-size: {FONT_LABEL_SIZE}px;"
        )
        layout.addWidget(num_lbl, alignment=Qt.AlignmentFlag.AlignLeft)

        self._name_lbl = QLabel(self._name)
        self._name_lbl.setAlignment(Qt.AlignmentFlag.AlignCenter)
        f = self._name_lbl.font()
        f.setBold(True)
        f.setPointSize(FONT_VALUE_SIZE)
        self._name_lbl.setFont(f)
        layout.addWidget(self._name_lbl, alignment=Qt.AlignmentFlag.AlignCenter)

        self._type_lbl = QLabel("")
        self._type_lbl.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self._type_lbl.setStyleSheet(
            f"color: {COLOR_TEXT_SECONDARY}; font-size: {FONT_LABEL_SIZE}px;"
        )
        layout.addWidget(self._type_lbl, alignment=Qt.AlignmentFlag.AlignCenter)

        self._refresh_style()

    def set_selected(self, selected: bool) -> None:
        self._selected = selected
        self._refresh_style()

    def set_status(self, pad_cfg: Optional[dict], type_name: str = "") -> None:
        if pad_cfg is not None:
            self._reserved  = pad_cfg.get("pad_type", 0) in {}
            status = pad_cfg.get("_status", 0)
            self._reserved = (status == INPUT_RESERVED)
        self._type_name = type_name
        self._type_lbl.setText(type_name)
        self._refresh_style()

    def set_name(self, name: str) -> None:
        self._name = name
        self._name_lbl.setText(name)

    def set_reserved(self, reserved: bool) -> None:
        self._reserved = reserved
        self._refresh_style()

    def _refresh_style(self) -> None:
        if self._reserved:
            text_color   = COLOR_TEXT_DISABLED
            border_color = COLOR_BORDER
            bg_color     = COLOR_BG_CARD
        elif self._selected:
            text_color   = COLOR_TEXT_PRIMARY
            border_color = COLOR_ACCENT
            bg_color     = COLOR_BG_CARD_SEL
        else:
            text_color   = COLOR_TEXT_PRIMARY
            border_color = COLOR_BORDER
            bg_color     = COLOR_BG_CARD

        self.setStyleSheet(
            f"#InputCard {{"
            f"  background-color: {bg_color};"
            f"  border: 2px solid {border_color};"
            f"  border-radius: 6px;"
            f"}}"
            f"#InputCard QLabel {{"
            f"  background: transparent;"
            f"}}"
        )
        self._name_lbl.setStyleSheet(
            f"color: {text_color}; font-weight: bold;"
            f" font-size: {FONT_VALUE_SIZE}px;"
        )

    def mousePressEvent(self, event) -> None:
        if not self._reserved:
            self.clicked.emit(self._input_id)
        super().mousePressEvent(event)


# ---------------------------------------------------------------------------
# HitLogWidget
# ---------------------------------------------------------------------------

class HitLogWidget(QWidget):
    def __init__(self, parent: Optional[QWidget] = None) -> None:
        super().__init__(parent)
        self._bars:  list[tuple[int, int]] = []  # (velocity, zone)
        self._count: int = 0
        self.setMinimumHeight(80)
        self.setSizePolicy(QSizePolicy.Policy.Expanding, QSizePolicy.Policy.Expanding)

    def add_hit(self, velocity: int, zone: int) -> None:
        self._bars.append((velocity, zone))
        if len(self._bars) > HIT_LOG_BARS:
            self._bars.pop(0)
        self._count = (self._count % 255) + 1
        self.update()

    def clear(self) -> None:
        self._bars.clear()
        self._count = 0
        self.update()

    @property
    def count(self) -> int:
        return self._count

    def last_velocity(self) -> int:
        return self._bars[-1][0] if self._bars else 0

    def paintEvent(self, event) -> None:
        painter = QPainter(self)
        painter.setRenderHint(QPainter.RenderHint.Antialiasing, False)

        dpr = self.devicePixelRatioF()
        w   = self.width()
        h   = self.height()

        painter.fillRect(0, 0, w, h, QColor(COLOR_BG_DARK))

        if not self._bars:
            painter.end()
            return

        n_bars  = len(self._bars)
        bar_w   = max(4, (w - 2) // HIT_LOG_BARS)
        x_start = w - n_bars * bar_w

        label_h = 14
        bar_area_h = h - label_h

        for i, (vel, zone) in enumerate(self._bars):
            x     = x_start + i * bar_w
            bar_h = int((vel / 127.0) * bar_area_h)
            y     = bar_area_h - bar_h
            color = QColor(COLOR_HIT_HEAD if zone == ZONE_HEAD else COLOR_HIT_RIM)
            painter.fillRect(x + 1, y, bar_w - 2, bar_h, color)

        # Footer text
        if self._bars:
            last_vel = self._bars[-1][0]
            painter.setPen(QColor(COLOR_TEXT_SECONDARY))
            painter.setFont(QFont("Arial", FONT_LABEL_SIZE))
            painter.drawText(
                2, bar_area_h + 1, w - 4, label_h,
                Qt.AlignmentFlag.AlignLeft | Qt.AlignmentFlag.AlignVCenter,
                f"hits: {self._count}  last: {last_vel}",
            )
        painter.end()


# ---------------------------------------------------------------------------
# Refresh worker
# ---------------------------------------------------------------------------

class _RefreshSignals(QObject):
    done   = pyqtSignal(dict)
    failed = pyqtSignal(str)


class _RefreshWorker(QThread):
    def __init__(
        self,
        transport: DrumMidiTransport,
        num_inputs: int = 9,
    ) -> None:
        super().__init__()
        self._transport  = transport
        self._num_inputs = num_inputs
        self.signals     = _RefreshSignals()

    def run(self) -> None:
        previous_cb = self._transport._sysex_callback
        results: dict[int, dict] = {}

        try:
            for i in range(self._num_inputs):
                cfg = self._fetch_input(i)
                results[i] = cfg
        except Exception as exc:
            self.signals.failed.emit(str(exc))
        else:
            self.signals.done.emit(results)
        finally:
            self._transport.set_sysex_callback(previous_cb)

    def _fetch_input(self, input_id: int) -> dict:
        transport = self._transport
        result: dict = {"_input_id": input_id}

        # --- status ---
        event  = threading.Event()
        status: dict = {}

        def on_status(msg: dict) -> None:
            if (msg["cmd_high"] == CAT_PAD and msg["cmd_low"] == 0x0A
                    and len(msg["payload"]) >= 2
                    and msg["payload"][0] == input_id):
                status.update(parse_input_status_response(msg["payload"]))
                event.set()

        transport.set_sysex_callback(on_status)
        transport.send(build_get_input_status(input_id))
        event.wait(2.0)
        if status:
            result["_status"] = status.get("status", 0)
            result["_status_name"] = status.get("status_name", "")

        # --- pad config ---
        event2 = threading.Event()
        pad_cfg: dict = {}

        def on_pad(msg: dict) -> None:
            if (msg["cmd_high"] == CAT_PAD and msg["cmd_low"] == 0x07
                    and len(msg["payload"]) >= 18
                    and msg["payload"][0] == input_id):
                pad_cfg.update(parse_pad_config_response(msg["payload"]))
                event2.set()

        transport.set_sysex_callback(on_pad)
        transport.send(build_get_pad_config(input_id))
        event2.wait(2.0)
        if pad_cfg:
            result.update(pad_cfg)

        # --- midi mapping ---
        event3 = threading.Event()
        midi_cfg: dict = {}

        def on_midi(msg: dict) -> None:
            if (msg["cmd_high"] == CAT_MIDI and msg["cmd_low"] == 0x05
                    and len(msg["payload"]) >= 7
                    and msg["payload"][0] == input_id):
                midi_cfg.update(parse_midi_mapping_response(msg["payload"]))
                event3.set()

        transport.set_sysex_callback(on_midi)
        transport.send(build_get_midi_mapping(input_id))
        event3.wait(2.0)
        if midi_cfg:
            result.update(midi_cfg)

        return result


# ---------------------------------------------------------------------------
# PadConfigTab
# ---------------------------------------------------------------------------

class PadConfigTab(QWidget):
    # emitted by background thread via signal bridge
    _configs_ready = pyqtSignal(dict)
    _hit_received  = pyqtSignal(int, int, int)  # input_id, zone, velocity

    def __init__(
        self,
        transport: DrumMidiTransport,
        parent: Optional[QWidget] = None,
    ) -> None:
        super().__init__(parent)
        self._transport    = transport
        self._loaded       = False
        self._active_tab   = False
        self._worker: Optional[_RefreshWorker] = None
        self._selected_id: Optional[int] = None
        self._configs:     dict[int, dict] = {}
        self._pad_names    = load_pad_names()

        self._configs_ready.connect(self._on_configs_ready)
        self._hit_received.connect(self._on_hit)

        self._build_ui()

    # ------------------------------------------------------------------
    # UI construction
    # ------------------------------------------------------------------

    def _build_ui(self) -> None:
        root = QHBoxLayout(self)
        root.setContentsMargins(0, 0, 0, 0)
        root.setSpacing(0)

        splitter = QSplitter(Qt.Orientation.Horizontal)
        splitter.setHandleWidth(2)
        root.addWidget(splitter)

        # Left panel
        left_panel = self._build_left_panel()
        left_panel.setMinimumWidth(200)
        left_panel.setMaximumWidth(320)
        splitter.addWidget(left_panel)

        # Right panel
        right_panel = self._build_right_panel()
        splitter.addWidget(right_panel)

        splitter.setStretchFactor(0, 0)
        splitter.setStretchFactor(1, 1)

    def _build_left_panel(self) -> QWidget:
        w = QWidget()
        w.setStyleSheet(f"background-color: {COLOR_BG_PANEL};")
        layout = QVBoxLayout(w)
        layout.setContentsMargins(8, 8, 8, 8)
        layout.setSpacing(8)

        title = QLabel("INPUTS")
        title.setStyleSheet(
            f"color: {COLOR_TEXT_SECONDARY}; font-size: {FONT_TITLE_SIZE}px;"
            " font-weight: bold; letter-spacing: 2px;"
        )
        layout.addWidget(title)

        grid = QGridLayout()
        grid.setSpacing(6)
        self._cards: list[InputCard] = []
        for i in range(9):
            card = InputCard(i)
            card.set_name(self._pad_names.get(i, "Unassigned"))
            card.clicked.connect(self._on_card_clicked)
            row, col = divmod(i, 2)
            grid.addWidget(card, row, col)
            self._cards.append(card)
        # empty slot for position 9
        grid.addWidget(QWidget(), 4, 1)
        layout.addLayout(grid)

        layout.addStretch()

        self._autotrack_btn = QPushButton("AUTOTRACK")
        self._autotrack_btn.setCheckable(True)
        self._autotrack_btn.setStyleSheet(
            f"QPushButton {{"
            f"  background-color: {COLOR_BG_CARD};"
            f"  color: {COLOR_TEXT_SECONDARY};"
            f"  border: 1px solid {COLOR_BORDER};"
            f"  border-radius: 4px; padding: 4px 8px;"
            f"}}"
            f"QPushButton:checked {{"
            f"  background-color: {COLOR_ACCENT};"
            f"  color: #ffffff;"
            f"}}"
        )
        layout.addWidget(self._autotrack_btn)

        return w

    def _build_right_panel(self) -> QWidget:
        w = QWidget()
        w.setStyleSheet(f"background-color: {COLOR_BG_DARK};")
        layout = QVBoxLayout(w)
        layout.setContentsMargins(0, 0, 0, 0)
        layout.setSpacing(0)

        # Header row (Refresh button)
        header = QHBoxLayout()
        header.setContentsMargins(12, 8, 12, 8)
        header.addStretch()
        self._loading_lbl = QLabel("⟳")
        self._loading_lbl.setStyleSheet(f"color: {COLOR_ACCENT}; font-size: 14px;")
        self._loading_lbl.hide()
        header.addWidget(self._loading_lbl)
        refresh_btn = QPushButton("Refresh")
        refresh_btn.setFixedWidth(80)
        refresh_btn.clicked.connect(self._start_refresh)
        header.addWidget(refresh_btn)
        layout.addLayout(header)

        # Stacked: placeholder vs detail
        self._stack = QStackedWidget()
        layout.addWidget(self._stack)

        # Page 0 — placeholder
        placeholder = QLabel("Connect to device and select an input")
        placeholder.setAlignment(Qt.AlignmentFlag.AlignCenter)
        placeholder.setStyleSheet(f"color: {COLOR_TEXT_SECONDARY}; font-size: 13px;")
        self._stack.addWidget(placeholder)

        # Page 1 — detail
        detail = self._build_detail()
        self._stack.addWidget(detail)

        self._stack.setCurrentIndex(0)
        return w

    def _build_detail(self) -> QWidget:
        w = QWidget()
        layout = QVBoxLayout(w)
        layout.setContentsMargins(12, 4, 12, 12)
        layout.setSpacing(10)

        # Section A — header
        header_row = QHBoxLayout()
        self._name_combo = QComboBox()
        self._name_combo.addItems(PAD_NAMES)
        self._name_combo.setFixedWidth(160)
        self._name_combo.currentTextChanged.connect(self._on_name_changed)
        header_row.addWidget(QLabel("Name:"))
        header_row.addWidget(self._name_combo)
        header_row.addSpacing(12)
        self._type_combo = QComboBox()
        for k, v in PAD_TYPE_NAMES.items():
            self._type_combo.addItem(v, k)
        self._type_combo.setEnabled(False)
        self._type_combo.setFixedWidth(140)
        header_row.addWidget(QLabel("Type:"))
        header_row.addWidget(self._type_combo)
        header_row.addStretch()
        layout.addLayout(header_row)

        # Section B — zone selector
        self._zone_row = QWidget()
        zone_h = QHBoxLayout(self._zone_row)
        zone_h.setContentsMargins(0, 0, 0, 0)
        zone_h.setSpacing(4)
        self._btn_head = QPushButton("HEAD")
        self._btn_head.setCheckable(True)
        self._btn_head.setChecked(True)
        self._btn_head.setFixedWidth(80)
        self._btn_rim  = QPushButton("RIM")
        self._btn_rim.setCheckable(True)
        self._btn_rim.setFixedWidth(80)
        for b in (self._btn_head, self._btn_rim):
            b.setStyleSheet(
                f"QPushButton {{"
                f"  background-color: {COLOR_BG_CARD};"
                f"  color: {COLOR_TEXT_SECONDARY};"
                f"  border: 1px solid {COLOR_BORDER};"
                f"  border-radius: 4px; padding: 4px 8px;"
                f"}}"
                f"QPushButton:checked {{"
                f"  background-color: {COLOR_ACCENT}; color: #fff;"
                f"}}"
            )
        self._btn_head.clicked.connect(lambda: self._select_zone(ZONE_HEAD))
        self._btn_rim.clicked.connect(lambda: self._select_zone(ZONE_RIM))
        zone_h.addWidget(self._btn_head)
        zone_h.addWidget(self._btn_rim)
        zone_h.addStretch()
        self._zone_row.hide()
        layout.addWidget(self._zone_row)

        # Section C — Curve + Hit Log
        c_row = QHBoxLayout()
        c_row.setSpacing(10)
        c_row.addWidget(self._build_curve_panel(), stretch=1)
        c_row.addWidget(self._build_hitlog_panel(), stretch=1)
        layout.addLayout(c_row)

        # Section D — Trigger settings
        layout.addWidget(self._build_trigger_panel())

        # Section E — MIDI tab
        layout.addWidget(self._build_midi_tabs())

        return w

    def _build_curve_panel(self) -> QGroupBox:
        box = QGroupBox("VELOCITY CURVE")
        box.setStyleSheet(self._group_style())
        vl = QVBoxLayout(box)

        self._curve_combo = QComboBox()
        for k, v in CURVE_NAMES.items():
            self._curve_combo.addItem(v, k)
        self._curve_combo.setEnabled(False)
        vl.addWidget(self._curve_combo)

        self._curve_desc = QLabel("")
        self._curve_desc.setWordWrap(True)
        self._curve_desc.setStyleSheet(
            f"color: {COLOR_TEXT_SECONDARY}; font-size: {FONT_LABEL_SIZE}px;"
        )
        vl.addWidget(self._curve_desc)

        self._vel_bar = QProgressBar()
        self._vel_bar.setRange(0, 127)
        self._vel_bar.setValue(0)
        self._vel_bar.setTextVisible(False)
        self._vel_bar.setOrientation(Qt.Orientation.Vertical)
        self._vel_bar.setFixedHeight(80)
        self._vel_bar.setStyleSheet(
            f"QProgressBar {{ background: {COLOR_BG_INPUT}; border: 1px solid {COLOR_BORDER}; }}"
            f"QProgressBar::chunk {{ background: {COLOR_ACCENT}; }}"
        )
        vl.addWidget(self._vel_bar, alignment=Qt.AlignmentFlag.AlignHCenter)

        self._vel_lbl = QLabel("—")
        self._vel_lbl.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self._vel_lbl.setStyleSheet(f"color: {COLOR_TEXT_SECONDARY};")
        vl.addWidget(self._vel_lbl)

        return box

    def _build_hitlog_panel(self) -> QGroupBox:
        box = QGroupBox("HIT LOG")
        box.setStyleSheet(self._group_style())
        vl = QVBoxLayout(box)

        hdr = QHBoxLayout()
        hdr.addStretch()
        clear_btn = QPushButton("Clear")
        clear_btn.setFixedWidth(50)
        clear_btn.clicked.connect(self._clear_hitlog)
        hdr.addWidget(clear_btn)
        vl.addLayout(hdr)

        self._hitlog = HitLogWidget()
        vl.addWidget(self._hitlog)

        return box

    def _build_trigger_panel(self) -> QGroupBox:
        box = QGroupBox("TRIGGER SETTINGS")
        box.setStyleSheet(self._group_style())
        grid = QGridLayout(box)
        grid.setSpacing(8)

        def _param(label: str, units: str = "") -> tuple[QLabel, QLineEdit, QWidget]:
            lbl  = QLabel(label)
            lbl.setStyleSheet(f"color: {COLOR_TEXT_SECONDARY}; font-size: {FONT_LABEL_SIZE}px;")
            edit = QLineEdit()
            edit.setReadOnly(True)
            edit.setFixedWidth(80)
            edit.setStyleSheet(
                f"QLineEdit {{ background: {COLOR_BG_INPUT}; color: {COLOR_TEXT_PRIMARY};"
                f" border: 1px solid {COLOR_BORDER}; border-radius: 3px; padding: 2px 4px; }}"
            )
            row_w = QWidget()
            rh = QHBoxLayout(row_w)
            rh.setContentsMargins(0, 0, 0, 0)
            rh.setSpacing(4)
            rh.addWidget(edit)
            if units:
                u = QLabel(units)
                u.setStyleSheet(f"color: {COLOR_TEXT_SECONDARY}; font-size: {FONT_LABEL_SIZE}px;")
                rh.addWidget(u)
            rh.addStretch()
            return lbl, edit, row_w

        params = [
            ("Threshold",        "",   "_thresh"),
            ("Sensitivity",      "",   "_sens"),
            ("Scan Time",        "ms", "_scan"),
            ("Double-Hit Guard", "ms", "_mask"),
            ("Retrigger",        "ms", "_retrig"),
            ("Rim Threshold",    "",   "_rim_thresh"),
            ("Rim Sensitivity",  "",   "_rim_sens"),
        ]

        self._param_widgets: dict[str, tuple[QWidget, QWidget]] = {}
        for idx, (label, units, key) in enumerate(params):
            lbl, edit, row_w = _param(label, units)
            r, c = divmod(idx, 2)
            grid.addWidget(lbl,   r, c * 2)
            grid.addWidget(row_w, r, c * 2 + 1)
            self._param_widgets[key] = (lbl, row_w)
            setattr(self, f"_edit{key}", edit)

        return box

    def _build_midi_tabs(self) -> QTabWidget:
        tabs = QTabWidget()
        tabs.setStyleSheet(
            f"QTabWidget::pane {{ background: {COLOR_BG_PANEL}; border: 1px solid {COLOR_BORDER}; }}"
            f"QTabBar::tab {{ background: {COLOR_BG_CARD}; color: {COLOR_TEXT_SECONDARY};"
            f"  padding: 4px 12px; }}"
            f"QTabBar::tab:selected {{ background: {COLOR_BG_PANEL}; color: {COLOR_TEXT_PRIMARY}; }}"
        )

        midi_tab = self._build_midi_panel()
        tabs.addTab(midi_tab, "MIDI")

        for name in ("Options", "Advanced"):
            ph = QWidget()
            tabs.addTab(ph, name)
            tabs.setTabEnabled(tabs.count() - 1, False)

        self._midi_tabs = tabs
        return tabs

    def _build_midi_panel(self) -> QWidget:
        w = QWidget()
        grid = QGridLayout(w)
        grid.setSpacing(8)
        grid.setContentsMargins(8, 8, 8, 8)

        def _row(label: str) -> QLineEdit:
            lbl = QLabel(label)
            lbl.setStyleSheet(
                f"color: {COLOR_TEXT_SECONDARY}; font-size: {FONT_LABEL_SIZE}px;"
            )
            edit = QLineEdit()
            edit.setReadOnly(True)
            edit.setFixedWidth(140)
            edit.setStyleSheet(
                f"QLineEdit {{ background: {COLOR_BG_INPUT}; color: {COLOR_TEXT_PRIMARY};"
                f" border: 1px solid {COLOR_BORDER}; border-radius: 3px; padding: 2px 4px; }}"
            )
            return lbl, edit

        midi_fields = [
            ("Head Note",    "_midi_head_note"),
            ("Head Channel", "_midi_head_ch"),
            ("Rim Note",     "_midi_rim_note"),
            ("Rim Channel",  "_midi_rim_ch"),
            ("CC Number",    "_midi_cc_num"),
            ("CC Channel",   "_midi_cc_ch"),
        ]
        self._midi_row_widgets: dict[str, tuple[QWidget, QWidget]] = {}
        for idx, (label, key) in enumerate(midi_fields):
            lbl, edit = _row(label)
            r, c = divmod(idx, 2)
            grid.addWidget(lbl,  r, c * 2)
            grid.addWidget(edit, r, c * 2 + 1)
            self._midi_row_widgets[key] = (lbl, edit)
            setattr(self, key, edit)

        return w

    @staticmethod
    def _group_style() -> str:
        return (
            f"QGroupBox {{"
            f"  background-color: {COLOR_BG_PANEL};"
            f"  border: 1px solid {COLOR_BORDER};"
            f"  border-radius: 6px;"
            f"  margin-top: 12px;"
            f"  font-size: {FONT_TITLE_SIZE}px;"
            f"  color: {COLOR_TEXT_SECONDARY};"
            f"}}"
            f"QGroupBox::title {{"
            f"  subcontrol-origin: margin;"
            f"  left: 8px;"
            f"  padding: 0 4px;"
            f"}}"
        )

    # ------------------------------------------------------------------
    # Event overrides
    # ------------------------------------------------------------------

    def showEvent(self, event) -> None:
        super().showEvent(event)
        if self._transport.is_connected() and not self._loaded:
            self._start_refresh()

    # ------------------------------------------------------------------
    # Public interface called by MainWindow
    # ------------------------------------------------------------------

    def on_connected(self) -> None:
        self._loaded = False
        if self.isVisible():
            self._start_refresh()

    def on_disconnected(self) -> None:
        self._transport.set_sysex_callback(None)
        self._stack.setCurrentIndex(0)
        self._loaded = False

    def set_active(self, active: bool) -> None:
        """Called when tab is focused/unfocused."""
        self._active_tab = active
        if active and self._transport.is_connected():
            self._transport.set_sysex_callback(self._on_sysex)
        else:
            if self._transport.is_connected():
                self._transport.set_sysex_callback(None)

    # ------------------------------------------------------------------
    # SysEx callback (runs on rtmidi thread)
    # ------------------------------------------------------------------

    def _on_sysex(self, msg: dict) -> None:
        hi  = msg.get("cmd_high", 0)
        lo  = msg.get("cmd_low",  0)
        pay = msg.get("payload",  b"")
        if hi == CAT_STATUS and lo == 0x03 and len(pay) >= 3:
            try:
                r = parse_hit_event(pay)
                self._hit_received.emit(r["input_id"], r["zone"], r["velocity"])
            except Exception:
                pass

    # ------------------------------------------------------------------
    # Refresh
    # ------------------------------------------------------------------

    def _start_refresh(self) -> None:
        if not self._transport.is_connected():
            return
        if self._worker and self._worker.isRunning():
            return
        self._loading_lbl.show()
        worker = _RefreshWorker(self._transport)
        worker.signals.done.connect(self._on_configs_ready)
        worker.signals.failed.connect(self._on_refresh_failed)
        worker.finished.connect(self._loading_lbl.hide)
        self._worker = worker
        worker.start()

    def _on_configs_ready(self, configs: dict) -> None:
        self._configs = configs
        self._loaded  = True

        for i, card in enumerate(self._cards):
            cfg = configs.get(i, {})
            pad_type = cfg.get("pad_type", 0)
            type_name = PAD_TYPE_NAMES.get(pad_type, "")
            card.set_status(cfg, type_name)
            card.set_reserved(cfg.get("_status", 0) == INPUT_RESERVED)

        if self._selected_id is not None:
            self._populate_detail(self._selected_id)

    def _on_refresh_failed(self, error: str) -> None:
        self._loading_lbl.hide()

    # ------------------------------------------------------------------
    # Card selection
    # ------------------------------------------------------------------

    def _on_card_clicked(self, input_id: int) -> None:
        self._select_input(input_id)

    def _select_input(self, input_id: int) -> None:
        if self._selected_id is not None:
            self._cards[self._selected_id].set_selected(False)
        self._selected_id = input_id
        self._cards[input_id].set_selected(True)
        self._stack.setCurrentIndex(1)
        if self._configs:
            self._populate_detail(input_id)
        else:
            # No device data yet — just update the name combo from saved names
            name = self._pad_names.get(input_id, "Unassigned")
            self._name_combo.blockSignals(True)
            idx = self._name_combo.findText(name)
            self._name_combo.setCurrentIndex(max(0, idx))
            self._name_combo.blockSignals(False)

    # ------------------------------------------------------------------
    # Detail population
    # ------------------------------------------------------------------

    def _populate_detail(self, input_id: int) -> None:
        cfg = self._configs.get(input_id, {})
        if not cfg:
            return

        pad_type   = cfg.get("pad_type", 0)
        is_dual    = pad_type in _DUAL_ZONE_TYPES
        is_hihat   = pad_type in _HIHAT_TYPES

        # Name combo
        name = self._pad_names.get(input_id, "Unassigned")
        self._name_combo.blockSignals(True)
        idx = self._name_combo.findText(name)
        self._name_combo.setCurrentIndex(max(0, idx))
        self._name_combo.blockSignals(False)

        # Type combo
        type_idx = self._type_combo.findData(pad_type)
        self._type_combo.setCurrentIndex(max(0, type_idx))

        # Zone row
        self._zone_row.setVisible(is_dual)

        # Curve
        curve  = cfg.get("velocity_curve", 0)
        c_name = CURVE_NAMES.get(curve, "Natural")
        c_idx  = self._curve_combo.findData(curve)
        self._curve_combo.setCurrentIndex(max(0, c_idx))
        self._curve_desc.setText(_CURVE_DESCRIPTIONS.get(c_name, ""))

        # Trigger settings
        def _set(edit, value):
            edit.setText(str(value))

        _set(self._edit_thresh,     cfg.get("threshold", "—"))
        _set(self._edit_sens,       cfg.get("head_sensitivity", "—"))
        _set(self._edit_scan,       cfg.get("scan_time", "—"))
        _set(self._edit_mask,       cfg.get("mask_time", "—"))
        _set(self._edit_retrig,     cfg.get("retrigger_time", "—"))
        _set(self._edit_rim_thresh, cfg.get("rim_threshold", "—"))
        _set(self._edit_rim_sens,   cfg.get("rim_sensitivity", "—"))

        for key in ("_rim_thresh", "_rim_sens"):
            lbl, row_w = self._param_widgets[key]
            lbl.setVisible(is_dual)
            row_w.setVisible(is_dual)

        # MIDI
        note = cfg.get("midi_note", 0)
        ch   = cfg.get("midi_channel", 1)
        self._midi_head_note.setText(f"{note} — {midi_note_name(note)}")
        self._midi_head_ch.setText(str(ch))

        for key in ("_midi_rim_note", "_midi_rim_ch"):
            lbl, edit = self._midi_row_widgets[key]
            lbl.setVisible(is_dual)
            edit.setVisible(is_dual)

        if is_dual:
            z2n = cfg.get("zone2_note", 0)
            z2c = cfg.get("zone2_channel", 1)
            self._midi_rim_note.setText(f"{z2n} — {midi_note_name(z2n)}")
            self._midi_rim_ch.setText(str(z2c))

        for key in ("_midi_cc_num", "_midi_cc_ch"):
            lbl, edit = self._midi_row_widgets[key]
            lbl.setVisible(is_hihat)
            edit.setVisible(is_hihat)

        if is_hihat:
            self._midi_cc_num.setText(str(cfg.get("cc_number", "—")))
            self._midi_cc_ch.setText(str(cfg.get("cc_channel", "—")))

    # ------------------------------------------------------------------
    # Zone tabs
    # ------------------------------------------------------------------

    def _select_zone(self, zone: int) -> None:
        self._btn_head.setChecked(zone == ZONE_HEAD)
        self._btn_rim.setChecked(zone == ZONE_RIM)

    # ------------------------------------------------------------------
    # Name change
    # ------------------------------------------------------------------

    def _on_name_changed(self, name: str) -> None:
        if self._selected_id is None:
            return
        self._pad_names[self._selected_id] = name
        self._cards[self._selected_id].set_name(name)
        save_pad_names(self._pad_names)

    # ------------------------------------------------------------------
    # Hit events
    # ------------------------------------------------------------------

    def _on_hit(self, input_id: int, zone: int, velocity: int) -> None:
        self._vel_bar.setValue(velocity)
        self._vel_lbl.setText(str(velocity))

        if self._selected_id == input_id:
            self._hitlog.add_hit(velocity, zone)
        elif self._autotrack_btn.isChecked():
            self._select_input(input_id)
            self._hitlog.add_hit(velocity, zone)

    def _clear_hitlog(self) -> None:
        self._hitlog.clear()
        self._vel_bar.setValue(0)
        self._vel_lbl.setText("—")
