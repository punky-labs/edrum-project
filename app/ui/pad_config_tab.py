from __future__ import annotations

import logging
import math
import threading
from typing import Optional

log = logging.getLogger("edrum.pad_config")

from PyQt6.QtCore import (
    Qt, QThread, pyqtSignal, QObject, QPoint, QSize,
)
from PyQt6.QtGui import QColor, QFont, QPainter, QPen, QPolygon
from PyQt6.QtWidgets import (
    QComboBox,
    QFrame,
    QGridLayout,
    QGroupBox,
    QHBoxLayout,
    QLabel,
    QProgressBar,
    QPushButton,
    QSizePolicy,
    QSlider,
    QSpinBox,
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
        COLOR_TEXT_DISABLED, COLOR_ACCENT, COLOR_RIM, COLOR_HIT_OTHER,
         COLOR_BORDER, COLOR_HIT_HEAD, COLOR_HIT_RIM, COLOR_WARNING,
        FONT_LABEL_SIZE, FONT_VALUE_SIZE, FONT_TITLE_SIZE,
        CARD_MIN_WIDTH, CARD_MIN_HEIGHT, HIT_LOG_BARS, SLIDER_HEIGHT,
    )
    from .pad_names import PAD_NAMES, load_pad_names, save_pad_names
    from .write_worker import WriteCommand, WriteWorker
except ImportError:
    from ui.theme import (  # type: ignore[no-redef]
        COLOR_BG_DARK, COLOR_BG_PANEL, COLOR_BG_CARD, COLOR_BG_CARD_SEL,
        COLOR_BG_INPUT, COLOR_TEXT_PRIMARY, COLOR_TEXT_SECONDARY,
        COLOR_TEXT_DISABLED, COLOR_ACCENT, COLOR_RIM, COLOR_BORDER,
        COLOR_HIT_HEAD, COLOR_HIT_RIM, COLOR_WARNING,
        FONT_LABEL_SIZE, FONT_VALUE_SIZE, FONT_TITLE_SIZE,
        CARD_MIN_WIDTH, CARD_MIN_HEIGHT, HIT_LOG_BARS, SLIDER_HEIGHT,
    )
    from ui.pad_names import PAD_NAMES, load_pad_names, save_pad_names  # type: ignore[no-redef]
    from ui.write_worker import WriteCommand, WriteWorker  # type: ignore[no-redef]

try:
    from .asset_loader import load_pad_icon
except ImportError:
    from ui.asset_loader import load_pad_icon  # type: ignore[no-redef]

try:
    from ..protocol.sysex import (
        CAT_PAD, CAT_MIDI, CAT_STATUS, CAT_SYS,
        NUM_INPUTS,
        PAD_TYPE_NAMES, CURVE_NAMES,
        PAD_TYPE_PIEZO_RIM, PAD_TYPE_DUAL_PIEZO,
        PAD_TYPE_HIHAT_CC, PAD_TYPE_HIHAT_SW,
        ZONE_HEAD, ZONE_RIM,
        PAD_SET_TYPE, PAD_SET_THRESH, PAD_SET_CURVE, PAD_SET_RETRIG,
        PAD_SET_SENS, PAD_SET_SCAN, PAD_SET_MASK, PAD_SET_RIM_SENS, PAD_SET_RIM_THRESH,
        MIDI_SET_NOTE, MIDI_SET_Z2, MIDI_SET_CC,
        SYS_SAVE,
        build_get_pad_config, build_get_midi_mapping, build_get_input_status,
        build_set_pad_type, build_set_threshold, build_set_velocity_curve,
        build_set_retrigger_time, build_set_head_sensitivity,
        build_set_scan_time, build_set_mask_time,
        build_set_rim_sensitivity, build_set_rim_threshold,
        build_set_note_mapping, build_set_zone2_mapping, build_set_cc_mapping,
        build_save_to_flash,
        parse_pad_config_response, parse_midi_mapping_response,
        parse_input_status_response, parse_hit_event,
        INPUT_ACTIVE, INPUT_RESERVED,
    )
except ImportError:
    from protocol.sysex import (  # type: ignore[no-redef]
        CAT_PAD, CAT_MIDI, CAT_STATUS, CAT_SYS,
        NUM_INPUTS,
        PAD_TYPE_NAMES, CURVE_NAMES,
        PAD_TYPE_PIEZO_RIM, PAD_TYPE_DUAL_PIEZO,
        PAD_TYPE_HIHAT_CC, PAD_TYPE_HIHAT_SW,
        ZONE_HEAD, ZONE_RIM,
        PAD_SET_TYPE, PAD_SET_THRESH, PAD_SET_CURVE, PAD_SET_RETRIG,
        PAD_SET_SENS, PAD_SET_SCAN, PAD_SET_MASK, PAD_SET_RIM_SENS, PAD_SET_RIM_THRESH,
        MIDI_SET_NOTE, MIDI_SET_Z2, MIDI_SET_CC,
        SYS_SAVE,
        build_get_pad_config, build_get_midi_mapping, build_get_input_status,
        build_set_pad_type, build_set_threshold, build_set_velocity_curve,
        build_set_retrigger_time, build_set_head_sensitivity,
        build_set_scan_time, build_set_mask_time,
        build_set_rim_sensitivity, build_set_rim_threshold,
        build_set_note_mapping, build_set_zone2_mapping, build_set_cc_mapping,
        build_save_to_flash,
        parse_pad_config_response, parse_midi_mapping_response,
        parse_input_status_response, parse_hit_event,
        INPUT_ACTIVE, INPUT_RESERVED,
    )

try:
    from ..transport.midi import DrumMidiTransport
except ImportError:
    from transport.midi import DrumMidiTransport  # type: ignore[no-redef]

_DUAL_ZONE_TYPES = {PAD_TYPE_PIEZO_RIM, PAD_TYPE_DUAL_PIEZO}
_HIHAT_TYPES     = {PAD_TYPE_HIHAT_CC, PAD_TYPE_HIHAT_SW}

# Input 4 is hardwired to the hi-hat controller jack (A0 on RP2040)
_HIHAT_INPUT_ID   = 4
_HIHAT_INPUT_TYPE = PAD_TYPE_HIHAT_CC

_ICON_SIZE = 56   # logical pixels for card icons

_CURVE_DESCRIPTIONS = {
    "Natural":    "Even response — what you play is what you get",
    "Expressive": "Wide dynamics — easy to play softly",
    "Sensitive":  "Very touch-responsive — rewards light playing",
    "Punchy":     "Present on moderate hits — loud and direct",
    "Aggressive": "Maximum punch — less dynamic variation",
    "Custom":     "Custom curve",
}

# GM percussion note map — note: name
# Starting at note 33 as agreed; gaps in the standard are omitted.
GM_PERCUSSION: dict[int, str] = {
    33: "Metronome click",
    34: "Metronome bell",
    35: "Bass drum",
    36: "Kick drum",
    37: "Snare cross stick",
    38: "Snare drum",
    39: "Hand clap",
    40: "Electric snare drum",
    41: "Floor tom 2",
    42: "Hi-hat closed",
    43: "Floor tom 1",
    44: "Hi-hat foot",
    45: "Low tom",
    46: "Hi-hat open",
    47: "Low-mid tom",
    48: "High-mid tom",
    49: "Crash cymbal",
    50: "High tom",
    51: "Ride cymbal",
    52: "China cymbal",
    53: "Ride bell",
    54: "Tambourine",
    55: "Splash cymbal",
    56: "Cowbell",
    57: "Crash cymbal 2",
    58: "Vibraslap",
    60: "High bongo",
    61: "Low bongo",
    62: "Conga dead stroke",
    63: "Conga",
    64: "Tumba",
    65: "High timbale",
    66: "Low timbale",
    67: "High agogo",
    68: "Low agogo",
    69: "Cabasa",
    70: "Maracas",
    71: "Whistle short",
    72: "Whistle long",
    73: "Guiro short",
    74: "Guiro long",
    75: "Claves",
    76: "High woodblock",
    77: "Low woodblock",
    78: "Cuica high",
    79: "Cuica low",
    80: "Triangle mute",
    81: "Triangle open",
    82: "Shaker",
    83: "Sleigh bell",
    84: "Bell tree",
    85: "Castanets",
    86: "Surdu dead stroke",
    87: "Surdu",
}


def gm_note_display(note: int) -> str:
    """Return display string for a note number.
    Format: 'Snare drum (38)' for mapped notes, 'Note 32' for unmapped notes."""
    name = GM_PERCUSSION.get(note)
    if name:
        return f"{name} ({note})"
    return f"Note {note}"


# (builder_fn, ack_hi, ack_lo, param_name, vmin, vmax, suffix)
_TRIGGER_BUILDERS: dict[str, tuple] = {
    "_thresh":     (build_set_threshold,        CAT_PAD, PAD_SET_THRESH,     "threshold",        0, 1023, ""),
    "_sens":       (build_set_head_sensitivity, CAT_PAD, PAD_SET_SENS,       "head_sensitivity", 0, 1023, ""),
    "_scan":       (build_set_scan_time,        CAT_PAD, PAD_SET_SCAN,       "scan_time",        0, 100,  " ms"),
    "_mask":       (build_set_mask_time,        CAT_PAD, PAD_SET_MASK,       "mask_time",        0, 500,  " ms"),
    "_retrig":     (build_set_retrigger_time,   CAT_PAD, PAD_SET_RETRIG,     "retrigger_time",   0, 1000, " ms"),
    "_rim_thresh": (build_set_rim_threshold,    CAT_PAD, PAD_SET_RIM_THRESH, "rim_threshold",    0, 1023, ""),
    "_rim_sens":   (build_set_rim_sensitivity,  CAT_PAD, PAD_SET_RIM_SENS,   "rim_sensitivity",  0, 1023, ""),
}


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

        self.setMinimumSize(CARD_MIN_WIDTH, CARD_MIN_HEIGHT + _ICON_SIZE + 4)
        self.setSizePolicy(QSizePolicy.Policy.Expanding, QSizePolicy.Policy.Preferred)
        self.setCursor(Qt.CursorShape.PointingHandCursor)

        layout = QVBoxLayout(self)
        layout.setContentsMargins(8, 4, 8, 4)
        layout.setSpacing(2)

        num_lbl = QLabel(str(input_id))
        num_lbl.setStyleSheet(
            f"color: {COLOR_TEXT_SECONDARY}; font-size: {FONT_LABEL_SIZE}px;"
        )
        layout.addWidget(num_lbl, alignment=Qt.AlignmentFlag.AlignLeft)

        self._icon_lbl = QLabel()
        self._icon_lbl.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self._icon_lbl.setFixedSize(_ICON_SIZE, _ICON_SIZE)
        layout.addWidget(self._icon_lbl, alignment=Qt.AlignmentFlag.AlignCenter)

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

        self._update_icon("Unassigned")
        self._refresh_style()

    def _update_icon(self, pad_name: str) -> None:
        """Load and display the icon for the given pad name."""
        pixmap = load_pad_icon(pad_name, _ICON_SIZE)
        if pixmap is not None:
            self._icon_lbl.setPixmap(pixmap)
        else:
            self._icon_lbl.clear()
            self._icon_lbl.setText("?")

    def set_selected(self, selected: bool) -> None:
        self._selected = selected
        self._refresh_style()

    def set_status(self, pad_cfg: Optional[dict], type_name: str = "") -> None:
        if pad_cfg is not None:
            status = pad_cfg.get("_status", 0)
            self._reserved = (status == INPUT_RESERVED)
        self._type_name = type_name
        self._type_lbl.setText(type_name)
        self._refresh_style()

    def set_name(self, name: str) -> None:
        self._name = name
        self._name_lbl.setText(name)
        self._update_icon(name)

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
# VelocityCurveWidget
# ---------------------------------------------------------------------------

class VelocityCurveWidget(QWidget):
    """
    Draws the velocity response curve for the currently selected
    curve type. Shows a live dot at the last hit position.
    """

    def __init__(self, parent: Optional[QWidget] = None) -> None:
        super().__init__(parent)
        self._curve_type:   int = 0
        self._last_vel_in:  int = -1
        self._last_vel_out: int = -1

        self.setMinimumHeight(120)
        self.setSizePolicy(
            QSizePolicy.Policy.Expanding,
            QSizePolicy.Policy.Expanding,
        )

    def set_curve(self, curve_type: int) -> None:
        self._curve_type = curve_type
        self.update()

    def set_last_hit(self, raw_vel: int, midi_vel: int) -> None:
        """
        Place the hit dot at (raw_vel, midi_vel) on the curve.
        raw_vel:  X position (pre-curve input, 0-127)
        midi_vel: Y position (post-curve output, 0-127)
        """
        self._last_vel_in  = raw_vel
        self._last_vel_out = midi_vel
        self.update()

    def clear_hit(self) -> None:
        self._last_vel_in  = -1
        self._last_vel_out = -1
        self.update()

    def _calc_output(self, x: int) -> float:
        if x <= 0:
            return 0.0
        ct = self._curve_type

        if ct == 0 or ct == 5:          # Natural or Custom (linear)
            return float(x)
        elif ct == 1:                   # Expressive (exp 1.02)
            b = 1.02
            return (126.0 / (b**126 - 1)) * (b**(x - 1) - 1) + 1
        elif ct == 2:                   # Sensitive (exp 1.05)
            b = 1.05
            return (126.0 / (b**126 - 1)) * (b**(x - 1) - 1) + 1
        elif ct == 3:                   # Punchy (log 0.98)
            b = 0.98
            denom = b**126 - 1
            if abs(denom) < 1e-10:
                return float(x)
            return (126.0 / denom) * (b**(x - 1) - 1) + 1
        elif ct == 4:                   # Aggressive (log 0.95)
            b = 0.95
            denom = b**126 - 1
            if abs(denom) < 1e-10:
                return float(x)
            return (126.0 / denom) * (b**(x - 1) - 1) + 1
        return float(x)

    def _build_curve_points(self) -> list[tuple[float, float]]:
        points = []
        for x in range(128):
            y = max(0.0, min(127.0, self._calc_output(x)))
            points.append((x / 127.0, y / 127.0))
        return points

    def paintEvent(self, event) -> None:
        painter = QPainter(self)
        painter.setRenderHint(QPainter.RenderHint.Antialiasing, True)

        w = self.width()
        h = self.height()

        margin_l = 8
        margin_r = 8
        margin_t = 8
        margin_b = 8
        plot_w = w - margin_l - margin_r
        plot_h = h - margin_t - margin_b

        painter.fillRect(0, 0, w, h, QColor(COLOR_BG_DARK))

        grid_pen = QPen(QColor(COLOR_BORDER))
        grid_pen.setWidth(1)
        painter.setPen(grid_pen)
        for frac in (0.25, 0.5, 0.75):
            gy = margin_t + int((1.0 - frac) * plot_h)
            painter.drawLine(margin_l, gy, margin_l + plot_w, gy)
            gx = margin_l + int(frac * plot_w)
            painter.drawLine(gx, margin_t, gx, margin_t + plot_h)

        border_pen = QPen(QColor(COLOR_BORDER))
        border_pen.setWidth(1)
        painter.setPen(border_pen)
        painter.drawRect(margin_l, margin_t, plot_w, plot_h)

        points = self._build_curve_points()

        curve_pen = QPen(QColor(COLOR_ACCENT))
        curve_pen.setWidth(2)
        curve_pen.setCapStyle(Qt.PenCapStyle.RoundCap)
        curve_pen.setJoinStyle(Qt.PenJoinStyle.RoundJoin)
        painter.setPen(curve_pen)

        def to_px(x_norm: float, y_norm: float) -> tuple[int, int]:
            px = margin_l + int(x_norm * plot_w)
            py = margin_t + int((1.0 - y_norm) * plot_h)
            return px, py

        poly_points = [QPoint(*to_px(xn, yn)) for xn, yn in points]
        painter.drawPolyline(QPolygon(poly_points))

        if self._last_vel_in >= 0:
            x_norm = self._last_vel_in  / 127.0
            y_norm = self._last_vel_out / 127.0   # actual firmware output value
            dot_x, dot_y = to_px(x_norm, y_norm)

            glow_pen = QPen(QColor(COLOR_ACCENT))
            glow_pen.setWidth(1)
            painter.setPen(glow_pen)
            painter.setBrush(Qt.BrushStyle.NoBrush)
            painter.drawEllipse(dot_x - 7, dot_y - 7, 14, 14)

            painter.setPen(Qt.PenStyle.NoPen)
            painter.setBrush(QColor(COLOR_ACCENT))
            painter.drawEllipse(dot_x - 4, dot_y - 4, 8, 8)

        painter.end()


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

    def add_hit(self, velocity: int, zone: int, is_selected: bool = True) -> None:
        self._bars.append((velocity, zone, is_selected))
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

        w = self.width()
        h = self.height()

        painter.fillRect(0, 0, w, h, QColor(COLOR_BG_DARK))

        if not self._bars:
            painter.end()
            return

        n_bars     = len(self._bars)
        bar_w      = max(4, (w - 2) // HIT_LOG_BARS)
        x_start    = w - n_bars * bar_w
        label_h    = 14
        bar_area_h = h - label_h

        for i, (vel, zone, is_selected) in enumerate(self._bars):
            x     = x_start + i * bar_w
            bar_h = int((vel / 127.0) * bar_area_h)
            y     = bar_area_h - bar_h
            if not is_selected:
                color = QColor(COLOR_HIT_OTHER)
            elif zone == ZONE_HEAD:
                color = QColor(COLOR_HIT_HEAD)
            else:
                color = QColor(COLOR_HIT_RIM)
            painter.fillRect(x + 1, y, bar_w - 2, bar_h, color)

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
        num_inputs: int = NUM_INPUTS,
    ) -> None:
        super().__init__()
        self._transport  = transport
        self._num_inputs = num_inputs
        self.signals     = _RefreshSignals()

    def run(self) -> None:
        results: dict[int, dict] = {}

        try:
            for i in range(self._num_inputs):
                cfg = self._fetch_input(i)
                results[i] = cfg
        except Exception as exc:
            self.signals.failed.emit(str(exc))
        else:
            log.info("Refresh complete: %d inputs loaded", len(results))
            self.signals.done.emit(results)
        finally:
            self._transport.remove_listener("refresh_worker")

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

        transport.add_listener("refresh_worker", on_status)
        transport.send(build_get_input_status(input_id))
        if not event.wait(2.0):
            log.warning("Timeout fetching input %d (step=%s)", input_id, "status")
        if status:
            result["_status"] = status.get("status", 0)
            result["_status_name"] = status.get("status_name", "")

        # --- pad config ---
        event2  = threading.Event()
        pad_cfg: dict = {}

        def on_pad(msg: dict) -> None:
            if (msg["cmd_high"] == CAT_PAD and msg["cmd_low"] == 0x07
                    and len(msg["payload"]) >= 18
                    and msg["payload"][0] == input_id):
                pad_cfg.update(parse_pad_config_response(msg["payload"]))
                event2.set()

        transport.add_listener("refresh_worker", on_pad)
        transport.send(build_get_pad_config(input_id))
        if not event2.wait(2.0):
            log.warning("Timeout fetching input %d (step=%s)", input_id, "pad_config")
        if pad_cfg:
            log.debug("Fetched input %d: pad_type=%s threshold=%s",
                      input_id,
                      pad_cfg.get("pad_type", "?"),
                      pad_cfg.get("threshold", "?"))
            result.update(pad_cfg)

        # --- midi mapping ---
        event3   = threading.Event()
        midi_cfg: dict = {}

        def on_midi(msg: dict) -> None:
            if (msg["cmd_high"] == CAT_MIDI and msg["cmd_low"] == 0x05
                    and len(msg["payload"]) >= 7
                    and msg["payload"][0] == input_id):
                midi_cfg.update(parse_midi_mapping_response(msg["payload"]))
                event3.set()

        transport.add_listener("refresh_worker", on_midi)
        transport.send(build_get_midi_mapping(input_id))
        if not event3.wait(2.0):
            log.warning("Timeout fetching input %d (step=%s)", input_id, "midi_mapping")
        if midi_cfg:
            result.update(midi_cfg)

        return result


# ---------------------------------------------------------------------------
# PadConfigTab
# ---------------------------------------------------------------------------

class PadConfigTab(QWidget):
    _configs_ready = pyqtSignal(dict)
    _hit_received  = pyqtSignal(int, int, int, int)  # input_id, zone, raw_vel, midi_vel
    status_message = pyqtSignal(str, int)        # msg, timeout_ms

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
        self._writer: Optional[WriteWorker]    = None
        self._dirty        = False
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

        left_panel = self._build_left_panel()
        left_panel.setMinimumWidth(200)
        left_panel.setMaximumWidth(320)
        splitter.addWidget(left_panel)

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

        # Create all 5 cards (inputs 0-4 only)
        for i in range(5):
            card = InputCard(i)
            card.set_name(self._pad_names.get(i, "Unassigned"))
            card.clicked.connect(self._on_card_clicked)
            self._cards.append(card)

        # Pad inputs 0-3 in 2x2 grid
        grid.addWidget(self._cards[0], 0, 0)
        grid.addWidget(self._cards[1], 0, 1)
        grid.addWidget(self._cards[2], 1, 0)
        grid.addWidget(self._cards[3], 1, 1)

        layout.addLayout(grid)

        # Separator line between pads and hi-hat
        sep = QFrame()
        sep.setFrameShape(QFrame.Shape.HLine)
        sep.setStyleSheet(f"color: {COLOR_BORDER};")
        layout.addWidget(sep)

        # Hi-hat controller card — full width
        hihat_grid = QGridLayout()
        hihat_grid.setSpacing(6)
        hihat_grid.addWidget(self._cards[4], 0, 0)
        hihat_grid.addWidget(QWidget(), 0, 1)   # empty slot beside hi-hat
        layout.addLayout(hihat_grid)

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

        # Header row
        header = QHBoxLayout()
        header.setContentsMargins(12, 8, 12, 8)
        header.addStretch()

        self._loading_lbl = QLabel("⟳")
        self._loading_lbl.setStyleSheet(f"color: {COLOR_ACCENT}; font-size: 14px;")
        self._loading_lbl.hide()
        header.addWidget(self._loading_lbl)

        refresh_btn = QPushButton("Refresh")
        refresh_btn.setFixedWidth(70)
        refresh_btn.clicked.connect(self._start_refresh)
        header.addWidget(refresh_btn)

        header.addSpacing(8)

        self._save_btn = QPushButton("Save to Flash")
        self._save_btn.setFixedWidth(110)
        self._save_btn.clicked.connect(self._enqueue_save_to_flash)
        self._save_btn.setEnabled(False)
        header.addWidget(self._save_btn)

        layout.addLayout(header)

        # Stacked: placeholder vs detail
        self._stack = QStackedWidget()
        layout.addWidget(self._stack)

        placeholder = QLabel("Connect to device and select an input")
        placeholder.setAlignment(Qt.AlignmentFlag.AlignCenter)
        placeholder.setStyleSheet(f"color: {COLOR_TEXT_SECONDARY}; font-size: 13px;")
        self._stack.addWidget(placeholder)

        detail = self._build_detail()
        self._stack.addWidget(detail)

        self._stack.setCurrentIndex(0)
        return w

    def _build_detail(self) -> QWidget:
        w = QWidget()
        layout = QVBoxLayout(w)
        layout.setContentsMargins(12, 4, 12, 12)
        layout.setSpacing(10)

        # Section A — name + type
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
        self._type_combo.setEnabled(True)
        self._type_combo.setFixedWidth(140)
        self._type_combo.currentIndexChanged.connect(self._on_type_changed)
        header_row.addWidget(QLabel("Type:"))
        header_row.addWidget(self._type_combo)
        header_row.addStretch()
        layout.addLayout(header_row)

        # Section C — Curve + Hit Log
        c_row = QHBoxLayout()
        c_row.setSpacing(10)
        c_row.addWidget(self._build_curve_panel(), stretch=1)
        c_row.addWidget(self._build_hitlog_panel(), stretch=1)
        layout.addLayout(c_row)

        # Section D+E — Trigger settings and MIDI side by side
        bottom_row = QHBoxLayout()
        bottom_row.setSpacing(10)
        bottom_row.addWidget(self._build_trigger_panel(), stretch=0)
        bottom_row.addWidget(self._build_midi_tabs(), stretch=1)
        layout.addLayout(bottom_row)

        return w

    def _build_curve_panel(self) -> QGroupBox:
        box = QGroupBox("VELOCITY CURVE")
        box.setStyleSheet(self._group_style())
        vl = QVBoxLayout(box)

        self._curve_combo = QComboBox()
        for k, v in CURVE_NAMES.items():
            self._curve_combo.addItem(v, k)
        self._curve_combo.setEnabled(True)
        self._curve_combo.currentIndexChanged.connect(self._on_curve_changed)
        vl.addWidget(self._curve_combo)

        self._curve_desc = QLabel("")
        self._curve_desc.setWordWrap(True)
        self._curve_desc.setStyleSheet(
            f"color: {COLOR_TEXT_SECONDARY}; font-size: {FONT_LABEL_SIZE}px;"
        )
        vl.addWidget(self._curve_desc)

        self._curve_widget = VelocityCurveWidget()

        curve_row = QHBoxLayout()
        curve_row.setSpacing(4)
        curve_row.setContentsMargins(0, 0, 0, 0)
        curve_row.addWidget(self._curve_widget, stretch=1)

        # Velocity bar — right-aligned inside the group box
        vel_col = QWidget()
        vcl = QVBoxLayout(vel_col)
        vcl.setContentsMargins(0, 0, 0, 0)
        vcl.setSpacing(2)

        self._vel_bar = QProgressBar()
        self._vel_bar.setRange(0, 127)
        self._vel_bar.setValue(0)
        self._vel_bar.setTextVisible(False)
        self._vel_bar.setOrientation(Qt.Orientation.Vertical)
        self._vel_bar.setFixedWidth(18)
        self._vel_bar.setStyleSheet(
            f"QProgressBar {{ background: {COLOR_BG_INPUT};"
            f" border: 1px solid {COLOR_BORDER}; border-radius: 3px; }}"
            f"QProgressBar::chunk {{ background: {COLOR_ACCENT};"
            f" border-radius: 2px; }}"
        )
        vcl.addWidget(self._vel_bar, stretch=1)

        self._vel_lbl = QLabel("—")
        self._vel_lbl.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self._vel_lbl.setFixedWidth(28)
        self._vel_lbl.setStyleSheet(
            f"color: {COLOR_TEXT_SECONDARY}; font-size: {FONT_LABEL_SIZE}px;"
        )
        vcl.addWidget(self._vel_lbl)

        curve_row.addWidget(vel_col)
        vl.addLayout(curve_row)

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

        outer = QHBoxLayout(box)
        outer.setSpacing(4)
        outer.setContentsMargins(8, 16, 8, 8)

        params = [
            ("Threshold",         "_thresh"),
            ("Sensitivity",       "_sens"),
            ("Scan Time\n(ms)",   "_scan"),
            ("Double-Hit\n(ms)",  "_mask"),
            ("Retrigger\n(ms)",   "_retrig"),
            ("Rim\nThreshold",    "_rim_thresh"),
            ("Rim\nSensitivity",  "_rim_sens"),
        ]

        self._param_widgets: dict[str, tuple[QWidget, QWidget]] = {}
        self._slider_value_labels: dict[str, QLabel] = {}

        for label_text, key in params:
            _, _, _, _, vmin, vmax, _ = _TRIGGER_BUILDERS[key]

            col = QWidget()
            col_layout = QVBoxLayout(col)
            col_layout.setContentsMargins(2, 0, 2, 0)
            col_layout.setSpacing(4)
            col_layout.setAlignment(Qt.AlignmentFlag.AlignHCenter)

            val_lbl = QLabel("0")
            val_lbl.setAlignment(Qt.AlignmentFlag.AlignCenter)
            val_lbl.setFixedWidth(50)
            val_lbl.setStyleSheet(
                f"color: {COLOR_TEXT_PRIMARY}; font-size: {FONT_VALUE_SIZE}px;"
                f" background: {COLOR_BG_INPUT}; border: 1px solid {COLOR_BORDER};"
                f" border-radius: 3px; padding: 1px 4px;"
            )
            col_layout.addWidget(val_lbl, alignment=Qt.AlignmentFlag.AlignHCenter)
            self._slider_value_labels[key] = val_lbl

            slider = QSlider(Qt.Orientation.Vertical)
            slider.setRange(vmin, vmax)
            slider.setValue(0)
            slider.setFixedHeight(SLIDER_HEIGHT)
            slider.setFixedWidth(30)
            slider.setInvertedAppearance(False)
            slider.setInvertedControls(True)
            slider.setStyleSheet(
                f"QSlider::groove:vertical {{"
                f"  background: {COLOR_BG_CARD};"
                f"  width: 6px;"
                f"  border-radius: 3px;"
                f"}}"
                f"QSlider::handle:vertical {{"
                f"  background: {COLOR_ACCENT};"
                f"  border: none;"
                f"  height: 14px;"
                f"  width: 14px;"
                f"  margin: 0 -4px;"
                f"  border-radius: 7px;"
                f"}}"
                f"QSlider::add-page:vertical {{"
                f"  background: {COLOR_ACCENT};"
                f"  border-radius: 3px;"
                f"}}"
                f"QSlider::sub-page:vertical {{"
                f"  background: {COLOR_BG_CARD};"
                f"}}"
            )
            slider.valueChanged.connect(
                lambda val, k=key, lbl=val_lbl: self._on_slider_changed(k, val, lbl)
            )
            setattr(self, f"_slider{key}", slider)
            col_layout.addWidget(slider, alignment=Qt.AlignmentFlag.AlignHCenter)

            param_lbl = QLabel(label_text)
            param_lbl.setAlignment(Qt.AlignmentFlag.AlignCenter)
            param_lbl.setStyleSheet(
                f"color: {COLOR_TEXT_SECONDARY}; font-size: {FONT_LABEL_SIZE}px;"
            )
            col_layout.addWidget(param_lbl, alignment=Qt.AlignmentFlag.AlignHCenter)

            outer.addWidget(col)
            self._param_widgets[key] = (col, slider)

        outer.addStretch()
        return box

    def _make_note_combo(self) -> QComboBox:
        combo = QComboBox()
        combo.setMinimumWidth(200)
        combo.setStyleSheet(
            f"QComboBox {{ background: {COLOR_BG_INPUT};"
            f" color: {COLOR_TEXT_PRIMARY};"
            f" border: 1px solid {COLOR_BORDER};"
            f" border-radius: 3px; padding: 2px 6px; }}"
            f"QComboBox::drop-down {{ border: none; }}"
            f"QComboBox QAbstractItemView {{"
            f" background: {COLOR_BG_PANEL};"
            f" color: {COLOR_TEXT_PRIMARY};"
            f" selection-background-color: {COLOR_ACCENT}; }}"
        )
        for note, name in GM_PERCUSSION.items():
            combo.addItem(f"{name} ({note})", note)
        return combo

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
        outer = QVBoxLayout(w)
        outer.setContentsMargins(0, 0, 0, 0)
        outer.setSpacing(0)

        grid_widget = QWidget()
        grid = QGridLayout(grid_widget)
        grid.setSpacing(8)
        grid.setContentsMargins(8, 8, 8, 8)
        grid.setAlignment(Qt.AlignmentFlag.AlignTop)

        spin_style = (
            f"QSpinBox {{ background: {COLOR_BG_INPUT}; color: {COLOR_TEXT_PRIMARY};"
            f" border: 1px solid {COLOR_BORDER}; border-radius: 3px; padding: 2px 4px; }}"
            f"QSpinBox::up-button, QSpinBox::down-button {{"
            f" width: 16px; background: {COLOR_BG_CARD}; border: none; }}"
        )

        def _lbl(text: str) -> QLabel:
            l = QLabel(text)
            l.setStyleSheet(
                f"color: {COLOR_TEXT_SECONDARY}; font-size: {FONT_LABEL_SIZE}px;"
            )
            return l

        def _ch_spin() -> QSpinBox:
            s = QSpinBox()
            s.setRange(1, 16)
            s.setFixedWidth(55)
            s.setStyleSheet(spin_style)
            return s

        # Row 0: Head note + channel (always visible)
        lbl_hn = _lbl("Head Note")
        self._combo_midi_head_note = self._make_note_combo()
        lbl_hch = _lbl("Head Channel")
        self._spin_midi_head_ch = _ch_spin()

        grid.addWidget(lbl_hn,                        0, 0)
        grid.addWidget(self._combo_midi_head_note,    0, 1)
        grid.addWidget(lbl_hch,                       0, 3)
        grid.addWidget(self._spin_midi_head_ch,       0, 4)

        self._combo_midi_head_note.currentIndexChanged.connect(self._on_midi_head_changed)
        self._spin_midi_head_ch.valueChanged.connect(self._on_midi_head_changed)

        # Row 1: Rim note + channel (dual-zone only)
        self._lbl_rim_note        = _lbl("Rim Note")
        self._combo_midi_rim_note = self._make_note_combo()
        self._lbl_rim_ch          = _lbl("Rim Channel")
        self._spin_midi_rim_ch    = _ch_spin()

        grid.addWidget(self._lbl_rim_note,            1, 0)
        grid.addWidget(self._combo_midi_rim_note,     1, 1)
        grid.addWidget(self._lbl_rim_ch,              1, 3)
        grid.addWidget(self._spin_midi_rim_ch,        1, 4)

        self._combo_midi_rim_note.currentIndexChanged.connect(self._on_midi_rim_changed)
        self._spin_midi_rim_ch.valueChanged.connect(self._on_midi_rim_changed)

        # Row 2: CC number + channel (hihat only)
        self._lbl_cc_num       = _lbl("CC Number")
        self._spin_midi_cc_num = QSpinBox()
        self._spin_midi_cc_num.setRange(0, 127)
        self._spin_midi_cc_num.setFixedWidth(70)
        self._spin_midi_cc_num.setStyleSheet(spin_style)
        self._lbl_cc_ch        = _lbl("CC Channel")
        self._spin_midi_cc_ch  = _ch_spin()

        grid.addWidget(self._lbl_cc_num,              2, 0)
        grid.addWidget(self._spin_midi_cc_num,        2, 1)
        grid.addWidget(self._lbl_cc_ch,               2, 3)
        grid.addWidget(self._spin_midi_cc_ch,         2, 4)

        self._spin_midi_cc_num.valueChanged.connect(self._on_midi_cc_changed)
        self._spin_midi_cc_ch.valueChanged.connect(self._on_midi_cc_changed)

        self._rim_midi_widgets: list[QWidget] = [
            self._lbl_rim_note, self._combo_midi_rim_note,
            self._lbl_rim_ch,   self._spin_midi_rim_ch,
        ]
        self._hihat_midi_widgets: list[QWidget] = [
            self._lbl_cc_num, self._spin_midi_cc_num,
            self._lbl_cc_ch,  self._spin_midi_cc_ch,
        ]

        outer.addWidget(grid_widget)
        outer.addStretch()

        self._midi_monitor = QLabel("—")
        self._midi_monitor.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self._midi_monitor.setStyleSheet(
            f"color: {COLOR_ACCENT}; font-size: {FONT_LABEL_SIZE}px;"
            f" background: {COLOR_BG_CARD}; border: 1px solid {COLOR_BORDER};"
            f" border-radius: 3px; padding: 4px 8px;"
        )
        self._midi_monitor.setFixedHeight(28)
        outer.addWidget(self._midi_monitor)

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
        log.info("Connected — starting refresh")
        self._loaded = False
        self._dirty  = False
        self._writer = WriteWorker(self._transport)
        self._writer.signals.write_ok.connect(self._on_write_ok)
        self._writer.signals.write_failed.connect(self._on_write_failed)
        self._writer.start()
        self._save_btn.setEnabled(True)
        self._update_save_button_style()
        if self.isVisible():
            self._start_refresh()

    def on_disconnected(self) -> None:
        log.info("Disconnected")
        if self._writer:
            self._writer.stop()
            self._writer.wait(3000)
            self._writer = None
        self._transport.remove_listener("pad_config")
        self._transport.remove_listener("refresh_worker")
        self._stack.setCurrentIndex(0)
        self._loaded = False
        self._dirty  = False
        self._save_btn.setEnabled(False)
        self._update_save_button_style()

    def set_active(self, active: bool) -> None:
        self._active_tab = active
        if active and self._transport.is_connected():
            self._transport.add_listener("pad_config", self._on_sysex)
        else:
            self._transport.remove_listener("pad_config")

    # ------------------------------------------------------------------
    # SysEx callback (runs on rtmidi thread)
    # ------------------------------------------------------------------

    def _on_sysex(self, msg: dict) -> None:
        hi  = msg.get("cmd_high", 0)
        lo  = msg.get("cmd_low",  0)
        pay = msg.get("payload",  b"")
        if hi == CAT_STATUS and lo == 0x03 and len(pay) >= 4:
            try:
                r = parse_hit_event(pay)
                self._hit_received.emit(
                    r["input_id"], r["zone"],
                    r["raw_velocity"], r["midi_velocity"]
                )
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
        log.info("Starting full refresh")
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

        for i in range(5):
            cfg       = configs.get(i, {})
            pad_type  = cfg.get("pad_type", 0)
            type_name = PAD_TYPE_NAMES.get(pad_type, "")
            self._cards[i].set_status(cfg, type_name)
            self._cards[i].set_reserved(cfg.get("_status", 0) == INPUT_RESERVED)

        # Input 4 is always the hi-hat controller — lock its type
        hihat_card = self._cards[_HIHAT_INPUT_ID]
        hihat_card.set_reserved(False)
        hihat_card.set_status({"_status": INPUT_ACTIVE}, "Hi-Hat Controller")

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
        self._populate_detail(input_id)

    # ------------------------------------------------------------------
    # Detail population
    # ------------------------------------------------------------------

    def _set_slider(self, key: str, value: int) -> None:
        """Set slider value and update its label without triggering writes."""
        slider = getattr(self, f"_slider{key}", None)
        lbl    = self._slider_value_labels.get(key)
        if slider:
            slider.blockSignals(True)
            slider.setValue(int(value))
            slider.blockSignals(False)
        if lbl:
            lbl.setText(str(int(value)))

    def _set_note_combo(self, combo: QComboBox, note: int) -> None:
        """Set combo to the given note. Falls back to a temporary item if not in GM map."""
        idx = combo.findData(note)
        if idx >= 0:
            combo.setCurrentIndex(idx)
        else:
            combo.blockSignals(True)
            combo.insertItem(0, gm_note_display(note), note)
            combo.setCurrentIndex(0)
            combo.blockSignals(False)

    def _populate_detail(self, input_id: int) -> None:
        log.debug("Populating detail for input %d", input_id)
        cfg = self._configs.get(input_id, {})

        pad_type = cfg.get("pad_type", 0)
        is_dual  = pad_type in _DUAL_ZONE_TYPES
        is_hihat = pad_type in _HIHAT_TYPES

        # Input 4 is always hi-hat regardless of what the device has stored
        if input_id == _HIHAT_INPUT_ID:
            pad_type = PAD_TYPE_HIHAT_CC
            is_dual  = False
            is_hihat = True

        # Block all interactive widgets to prevent cascade writes during load
        for widget in self._all_editable_widgets():
            widget.blockSignals(True)

        try:
            # Name combo
            name = self._pad_names.get(input_id, "Unassigned")
            idx  = self._name_combo.findText(name)
            self._name_combo.setCurrentIndex(max(0, idx))

            # Type combo
            type_idx = self._type_combo.findData(pad_type)
            self._type_combo.setCurrentIndex(max(0, type_idx))

            # Input 4 is hardwired to hi-hat — lock type dropdown
            is_hihat_input = (input_id == _HIHAT_INPUT_ID)
            self._type_combo.setEnabled(not is_hihat_input)
            if is_hihat_input:
                hihat_idx = self._type_combo.findData(PAD_TYPE_HIHAT_CC)
                if hihat_idx >= 0:
                    self._type_combo.setCurrentIndex(hihat_idx)

            # Curve
            curve = cfg.get("velocity_curve", 0)
            c_idx = self._curve_combo.findData(curve)
            self._curve_combo.setCurrentIndex(max(0, c_idx))
            c_name = CURVE_NAMES.get(curve, "Natural")
            self._curve_desc.setText(_CURVE_DESCRIPTIONS.get(c_name, ""))

            # Trigger sliders
            self._set_slider("_thresh",     cfg.get("threshold", 0))
            self._set_slider("_sens",       cfg.get("head_sensitivity", 0))
            self._set_slider("_scan",       cfg.get("scan_time", 0))
            self._set_slider("_mask",       cfg.get("mask_time", 0))
            self._set_slider("_retrig",     cfg.get("retrigger_time", 0))
            self._set_slider("_rim_thresh", cfg.get("rim_threshold", 0))
            self._set_slider("_rim_sens",   cfg.get("rim_sensitivity", 0))

            # Head MIDI
            note = cfg.get("midi_note", 38)
            ch   = cfg.get("midi_channel", 1)
            self._set_note_combo(self._combo_midi_head_note, note)
            self._spin_midi_head_ch.setValue(ch)

            # Rim MIDI
            z2n = cfg.get("zone2_note", 39)
            z2c = cfg.get("zone2_channel", 1)
            self._set_note_combo(self._combo_midi_rim_note, z2n)
            self._spin_midi_rim_ch.setValue(z2c)

            # CC MIDI
            self._spin_midi_cc_num.setValue(cfg.get("cc_number", 0))
            self._spin_midi_cc_ch.setValue(cfg.get("cc_channel", 1))

        finally:
            for widget in self._all_editable_widgets():
                widget.blockSignals(False)

        # Update curve widget directly (signals were blocked during populate)
        self._curve_widget.set_curve(cfg.get("velocity_curve", 0))
        self._curve_widget.clear_hit()

        # Visibility
        self._update_zone_visibility(is_dual, is_hihat)

        self._midi_monitor.setText("—")

    def _all_editable_widgets(self) -> list[QWidget]:
        return [
            self._name_combo, self._type_combo, self._curve_combo,
            self._slider_thresh, self._slider_sens, self._slider_scan,
            self._slider_mask, self._slider_retrig,
            self._slider_rim_thresh, self._slider_rim_sens,
            self._combo_midi_head_note, self._spin_midi_head_ch,
            self._combo_midi_rim_note,  self._spin_midi_rim_ch,
            self._spin_midi_cc_num,     self._spin_midi_cc_ch,
        ]

    def _update_zone_visibility(self, is_dual: bool, is_hihat: bool) -> None:
        # Rim sliders: always visible, disabled for single-zone pads
        for key in ("_rim_thresh", "_rim_sens"):
            col, slider = self._param_widgets[key]
            slider.setEnabled(is_dual)
            lbl = self._slider_value_labels.get(key)
            val_color    = COLOR_TEXT_PRIMARY if is_dual else COLOR_TEXT_DISABLED
            border_color = COLOR_BORDER
            bg_color     = COLOR_BG_INPUT if is_dual else COLOR_BG_DARK
            if lbl:
                lbl.setStyleSheet(
                    f"color: {val_color}; font-size: {FONT_VALUE_SIZE}px;"
                    f" background: {bg_color}; border: 1px solid {border_color};"
                    f" border-radius: 3px; padding: 1px 4px;"
                )
            for widget in col.findChildren(QLabel):
                if widget is not lbl:
                    widget.setStyleSheet(
                        f"color: {COLOR_TEXT_SECONDARY if is_dual else COLOR_TEXT_DISABLED};"
                        f" font-size: {FONT_LABEL_SIZE}px;"
                    )

        # MIDI rim fields: hide for single-zone (these are in the MIDI panel)
        for widget in self._rim_midi_widgets:
            widget.setVisible(is_dual)

        # MIDI CC fields: show for hi-hat types only
        for widget in self._hihat_midi_widgets:
            widget.setVisible(is_hihat)

    # ------------------------------------------------------------------
    # Write helpers
    # ------------------------------------------------------------------

    def _enqueue_write(
        self, input_id: int, param: str,
        message: bytearray, ack_hi: int, ack_lo: int,
    ) -> None:
        if not self._writer or not self._transport.is_connected():
            return
        cmd = WriteCommand(input_id, param, message, ack_hi, ack_lo)
        self._writer.enqueue(cmd)
        self._set_dirty(True)

    def _on_slider_changed(self, key: str, value: int, lbl: QLabel) -> None:
        lbl.setText(str(value))
        if self._selected_id is None:
            return
        fn, ack_hi, ack_lo, param, *_ = _TRIGGER_BUILDERS[key]
        log.debug("Slider changed: input=%d key=%s value=%d",
                  self._selected_id, key, value)
        msg = fn(self._selected_id, value)
        self._enqueue_write(self._selected_id, param, msg, ack_hi, ack_lo)

    def _on_type_changed(self, index: int) -> None:
        if self._selected_id is None:
            return
        if self._selected_id == _HIHAT_INPUT_ID:
            return   # type is locked for hi-hat input
        pad_type = self._type_combo.itemData(index)
        if pad_type is None:
            return
        is_dual  = pad_type in _DUAL_ZONE_TYPES
        is_hihat = pad_type in _HIHAT_TYPES
        self._update_zone_visibility(is_dual, is_hihat)
        msg = build_set_pad_type(self._selected_id, pad_type)
        self._enqueue_write(self._selected_id, "pad_type", msg, CAT_PAD, PAD_SET_TYPE)

    def _on_curve_changed(self, index: int) -> None:
        if self._selected_id is None:
            return
        curve = self._curve_combo.itemData(index)
        if curve is None:
            return
        c_name = CURVE_NAMES.get(curve, "")
        self._curve_desc.setText(_CURVE_DESCRIPTIONS.get(c_name, ""))
        self._curve_widget.set_curve(curve)
        msg = build_set_velocity_curve(self._selected_id, curve)
        self._enqueue_write(self._selected_id, "velocity_curve", msg, CAT_PAD, PAD_SET_CURVE)

    def _on_midi_head_changed(self) -> None:
        if self._selected_id is None:
            return
        note = self._combo_midi_head_note.currentData()
        ch   = self._spin_midi_head_ch.value()
        if note is None:
            return
        msg = build_set_note_mapping(self._selected_id, note, ch)
        self._enqueue_write(self._selected_id, "midi_note", msg, CAT_MIDI, MIDI_SET_NOTE)

    def _on_midi_rim_changed(self) -> None:
        if self._selected_id is None:
            return
        note = self._combo_midi_rim_note.currentData()
        ch   = self._spin_midi_rim_ch.value()
        if note is None:
            return
        msg = build_set_zone2_mapping(self._selected_id, note, ch)
        self._enqueue_write(self._selected_id, "midi_z2", msg, CAT_MIDI, MIDI_SET_Z2)

    def _on_midi_cc_changed(self) -> None:
        if self._selected_id is None:
            return
        cc_num = self._spin_midi_cc_num.value()
        cc_ch  = self._spin_midi_cc_ch.value()
        msg = build_set_cc_mapping(self._selected_id, cc_num, cc_ch)
        self._enqueue_write(self._selected_id, "midi_cc", msg, CAT_MIDI, MIDI_SET_CC)

    def _enqueue_save_to_flash(self) -> None:
        if not self._writer or not self._transport.is_connected():
            return
        log.info("Save to flash requested")
        msg = build_save_to_flash()
        cmd = WriteCommand(-1, "save_to_flash", msg, CAT_SYS, SYS_SAVE)
        self._writer.enqueue(cmd)
        self.status_message.emit("Saving to flash…", 0)

    # ------------------------------------------------------------------
    # Write result handlers
    # ------------------------------------------------------------------

    def _on_write_ok(self, input_id: int, param: str) -> None:
        if param == "save_to_flash":
            log.info("Save to flash: OK")
            self._set_dirty(False)
            self.status_message.emit("Saved to flash.", 3000)

    def _on_write_failed(self, input_id: int, param: str, reason: str) -> None:
        if param == "save_to_flash":
            log.info("Save to flash: FAILED")
        log.error("Write error: %s", reason)
        self.status_message.emit(f"Write failed ({param}): {reason}", 4000)

    # ------------------------------------------------------------------
    # Dirty state
    # ------------------------------------------------------------------

    def _set_dirty(self, dirty: bool) -> None:
        if self._dirty == dirty:
            return
        self._dirty = dirty
        self._update_save_button_style()

    def _update_save_button_style(self) -> None:
        if self._dirty:
            self._save_btn.setStyleSheet(
                f"QPushButton {{"
                f"  background-color: {COLOR_BG_CARD};"
                f"  color: {COLOR_WARNING};"
                f"  border: 2px solid {COLOR_WARNING};"
                f"  border-radius: 4px; padding: 4px 8px; font-weight: bold;"
                f"}}"
            )
        else:
            self._save_btn.setStyleSheet("")

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

    def _on_hit(self, input_id: int, zone: int,
                raw_vel: int, midi_vel: int) -> None:
        
        is_selected = (self._selected_id == input_id)
        self._hitlog.add_hit(raw_vel, zone, is_selected)

        if is_selected:
            self._vel_bar.setValue(midi_vel)
            self._vel_lbl.setText(str(midi_vel))
            self._curve_widget.set_last_hit(raw_vel, midi_vel)

            cfg        = self._configs.get(self._selected_id, {})
            note       = cfg.get("midi_note", 0) if zone == ZONE_HEAD \
                         else cfg.get("zone2_note", 0)
            note_name  = gm_note_display(note)
            ch         = cfg.get("midi_channel", 1) if zone == ZONE_HEAD \
                         else cfg.get("zone2_channel", 1)
            zone_label = "Head" if zone == ZONE_HEAD else "Rim"
            self._midi_monitor.setText(
                f"► {zone_label}  {note_name}  vel {midi_vel}  ch {ch}"
            )

        if not is_selected and self._autotrack_btn.isChecked():
            self._select_input(input_id)

    def _clear_hitlog(self) -> None:
        self._hitlog.clear()
        self._vel_bar.setValue(0)
        self._vel_lbl.setText("—")
        self._curve_widget.clear_hit()
