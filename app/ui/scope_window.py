from __future__ import annotations

import csv
import logging
import os
import re
from datetime import datetime
from typing import Optional

log = logging.getLogger("edrum.scope_window")

from PyQt6.QtCore import Qt, QThread, pyqtSignal
from PyQt6.QtGui import QBrush, QColor, QFont
from PyQt6.QtWidgets import (
    QApplication,
    QCheckBox,
    QComboBox,
    QFileDialog,
    QHBoxLayout,
    QLabel,
    QLineEdit,
    QListWidget,
    QListWidgetItem,
    QMainWindow,
    QPushButton,
    QSpinBox,
    QVBoxLayout,
    QWidget,
)

try:
    import serial
    import serial.tools.list_ports
    _SERIAL = True
except ImportError:
    serial = None  # type: ignore[assignment]
    _SERIAL = False

try:
    import pyqtgraph as pg
    _PG = True
except ImportError:
    pg = None  # type: ignore[assignment]
    _PG = False

try:
    from .theme import (
        COLOR_BG_INPUT,
        COLOR_TEXT_PRIMARY,
        COLOR_TEXT_SECONDARY,
    )
except ImportError:
    from ui.theme import (  # type: ignore[no-redef]
        COLOR_BG_INPUT,
        COLOR_TEXT_PRIMARY,
        COLOR_TEXT_SECONDARY,
    )

_COLOR_HEAD  = "#2dd4bf"   # teal — head channel
_COLOR_RIM   = "#fb923c"   # orange — rim channel
_COLOR_CHART = "#1a1a1a"
_COLOR_GRID  = "#2a2a2a"
_COLOR_AMBER = "#f59e0b"

BAUD_RATE = 115200


class _ScopeParser:
    """Line-by-line state machine for the firmware's [SCOPE] / T,H,R / CSV dump
    format. Extracted so BOTH the live serial reader and the log-file loader parse
    exactly the same protocol from a single implementation (the live path used to
    inline this in _SerialReader.run()).

    Stateful — feed() one already-decoded, prefix-free line at a time; it returns a
    list of events, in order:
        ("capture", meta: dict, head: list[int], rim: list[int])
        ("line",    text: str)   # pass-through, non-scope line
        ("adc",)                 # an [ADC] line was seen (device ADC dump active)

    The caller decides what to do with each event (emit a Qt signal on the live
    path; collect captures on the file path). Behaviour is identical to the old
    inline loop, so the live path is unchanged.
    """

    def __init__(self) -> None:
        self.reset()

    def reset(self) -> None:
        self._state = "IDLE"
        self._meta: dict = {}
        self._head: list[int] = []
        self._rim:  list[int] = []
        self._expected = 0

    def feed(self, line: str) -> list:
        events: list = []

        if line.startswith("[SCOPE]"):
            # Flush any in-progress capture before starting a new one
            if self._state == "READING_DATA" and self._head:
                events.append(("capture", self._meta, list(self._head), list(self._rim)))
            self._meta = {}
            self._head = []
            self._rim  = []
            for part in line.split()[1:]:
                if "=" in part:
                    k, v = part.split("=", 1)
                    try:
                        self._meta[k] = int(v)
                    except ValueError:
                        self._meta[k] = v
            self._expected = self._meta.get("samples", 200)
            self._state = "READING_HEADER"

        elif line == "T,H,R" and self._state == "READING_HEADER":
            self._state = "READING_DATA"

        elif self._state == "READING_DATA":
            parts = line.split(",")
            if len(parts) == 3:
                try:
                    h = int(parts[1])
                    r = int(parts[2])
                except ValueError:
                    events.append(("line", line))
                else:
                    self._head.append(h)
                    self._rim.append(r)
                    if len(self._head) >= self._expected:
                        events.append(("capture", self._meta,
                                       list(self._head), list(self._rim)))
                        self._head = []
                        self._rim  = []
                        self._state = "IDLE"
            else:
                events.append(("line", line))

        else:
            if "[ADC]" in line:
                events.append(("adc",))
            events.append(("line", line))

        return events

    def finish(self) -> list:
        """Flush a capture still in progress at end-of-input. Only the file loader
        calls this (the live reader just stops its thread); additive, so live
        behaviour is unaffected. The flushed capture will be sub-`samples` length,
        which the file loader treats as malformed/partial."""
        events: list = []
        if self._state == "READING_DATA" and self._head:
            events.append(("capture", self._meta, list(self._head), list(self._rim)))
        self.reset()
        return events


class _SerialReader(QThread):
    """Reads lines from a serial port; emits scope captures and pass-through lines."""

    scope_capture = pyqtSignal(dict, list, list)   # metadata, head_samples, rim_samples
    serial_line   = pyqtSignal(str)                 # non-scope lines
    adc_warning   = pyqtSignal()                    # [ADC] line detected

    def __init__(self, port: "serial.Serial") -> None:
        super().__init__()
        self._port    = port
        self._running = True

    def stop(self) -> None:
        self._running = False

    def run(self) -> None:
        parser = _ScopeParser()

        while self._running:
            try:
                raw = self._port.readline()
            except Exception:
                break
            if not raw:
                continue
            try:
                line = raw.decode("utf-8", errors="replace").rstrip("\r\n")
            except Exception:
                continue
            if not line:
                continue

            for event in parser.feed(line):
                kind = event[0]
                if kind == "capture":
                    self.scope_capture.emit(event[1], event[2], event[3])
                elif kind == "adc":
                    self.adc_warning.emit()
                else:  # "line"
                    self.serial_line.emit(event[1])

    # Config response helpers
    @staticmethod
    def _parse_config_row(line: str) -> Optional[tuple[int, dict]]:
        """Parse one [Config] input row, e.g. '  [0] note=36 thresh=30 scan=3 ...'.
        Returns (input_number, {key: int_value, ...}), or None if the line is not a
        config row. Single shared parsing primitive used by BOTH the live 's'
        handler (via _parse_config_line) and the log-file loader, so the two parse
        the exact same format."""
        m = re.match(r"\s*\[(\d+)\]", line)
        if not m:
            return None
        result: dict = {}
        for kv in re.finditer(r"(\w+)=(\d+)", line):
            result[kv.group(1)] = int(kv.group(2))
        if not result:
            return None
        return int(m.group(1)), result

    @staticmethod
    def _parse_config_line(line: str, input_idx: int) -> Optional[dict]:
        """Parse a [Config] input line e.g. '  [0] note=36 thresh=30 ...'
        Returns dict of int values if it matches input_idx, else None. Behaviour
        unchanged (live 's' path); now delegates to _parse_config_row."""
        row = _SerialReader._parse_config_row(line)
        if row is None or row[0] != input_idx:
            return None
        return row[1]


class ScopeWindow(QMainWindow):
    """Floating ADC scope window — dev mode only."""

    def __init__(self, parent=None) -> None:
        super().__init__(parent)
        self.setWindowTitle("eDrum — ADC Scope")
        self.setMinimumSize(1000, 700)

        self._serial:   Optional["serial.Serial"] = None
        self._reader:   Optional[_SerialReader]   = None
        self._captures: list[tuple[dict, list, list]] = []
        # Per-capture overlay config, index-parallel to _captures:
        #   None = live capture   → fall back to the global _pad_config (unchanged
        #                           live behaviour)
        #   {}   = loaded capture → no applicable [Config] block; draw no overlay
        #   dict = loaded capture → that capture's own scan/mask/thresh config
        self._capture_configs: list[Optional[dict]] = []
        self._armed:    bool = False
        self._pad_config: dict = {}           # current pad config from 's' command
        self._reading_config: bool = False    # True while parsing [Config] block
        self._auto_save_dir = os.path.normpath(
            os.path.join(os.path.dirname(__file__), "..", "logs", "scope")
        )
        # telnet_logger.py writes its session logs here (project-root/tools/logs);
        # used as the default directory for the "Load Log…" file picker.
        self._tools_logs_dir = os.path.normpath(
            os.path.join(os.path.dirname(__file__), "..", "..", "tools", "logs")
        )

        self._build_ui()
        self._refresh_ports()

    # ------------------------------------------------------------------
    # UI construction
    # ------------------------------------------------------------------

    def _build_ui(self) -> None:
        central = QWidget()
        self.setCentralWidget(central)
        root = QVBoxLayout(central)
        root.setContentsMargins(8, 8, 8, 8)
        root.setSpacing(6)

        root.addLayout(self._build_conn_bar())
        root.addLayout(self._build_settings_bar())

        self._adc_warn = QLabel(
            "⚠  ADC dump active on device — scope data may be incomplete"
        )
        self._adc_warn.setStyleSheet(f"color: {_COLOR_AMBER};")
        self._adc_warn.setVisible(False)
        root.addWidget(self._adc_warn)

        root.addWidget(self._build_chart(), stretch=1)

        root.addLayout(self._build_serial_bar())

        log_lbl = QLabel("Session Log")
        log_lbl.setStyleSheet(f"color: {COLOR_TEXT_SECONDARY}; font-size: 9px;")
        root.addWidget(log_lbl)

        self._session_list = QListWidget()
        self._session_list.setFixedHeight(180)
        mono = QFont("IBM Plex Mono", 9)
        mono.setStyleHint(QFont.StyleHint.Monospace)
        self._session_list.setFont(mono)
        self._session_list.setStyleSheet(
            f"QListWidget {{ background-color: {COLOR_BG_INPUT}; "
            f"color: {COLOR_TEXT_PRIMARY}; border: none; }}"
        )
        self._session_list.itemClicked.connect(self._on_log_item_clicked)
        root.addWidget(self._session_list)

        serial_out_lbl = QLabel("Serial Output")
        serial_out_lbl.setStyleSheet(f"color: {COLOR_TEXT_SECONDARY}; font-size: 9px;")
        root.addWidget(serial_out_lbl)

        self._serial_output = QListWidget()
        self._serial_output.setFixedHeight(100)
        mono2 = QFont("IBM Plex Mono", 8)
        mono2.setStyleHint(QFont.StyleHint.Monospace)
        self._serial_output.setFont(mono2)
        self._serial_output.setStyleSheet(
            f"QListWidget {{ background-color: {COLOR_BG_INPUT}; "
            f"color: {COLOR_TEXT_SECONDARY}; border: none; }}"
        )
        self._serial_output.setSelectionMode(
            QListWidget.SelectionMode.ExtendedSelection
        )
        self._serial_output.setContextMenuPolicy(
            Qt.ContextMenuPolicy.ActionsContextMenu
        )
        copy_action = self._serial_output.addAction("Copy")
        copy_action.setShortcut("Ctrl+C")
        copy_action.triggered.connect(self._copy_serial_output)
        root.addWidget(self._serial_output)

    def _build_conn_bar(self) -> QHBoxLayout:
        bar = QHBoxLayout()
        bar.setSpacing(6)

        bar.addWidget(QLabel("Port:"))
        self._port_combo = QComboBox()
        self._port_combo.setMinimumWidth(180)
        bar.addWidget(self._port_combo)

        refresh_btn = QPushButton("⟳")
        refresh_btn.setFixedWidth(28)
        refresh_btn.setToolTip("Refresh serial ports")
        refresh_btn.clicked.connect(self._refresh_ports)
        bar.addWidget(refresh_btn)

        self._connect_btn = QPushButton("Connect")
        self._connect_btn.setFixedWidth(100)
        self._connect_btn.clicked.connect(self._on_connect_toggle)
        bar.addWidget(self._connect_btn)

        bar.addSpacing(12)

        bar.addWidget(QLabel("Input:"))
        self._input_spin = QSpinBox()
        self._input_spin.setRange(0, 4)
        self._input_spin.setFixedWidth(52)
        self._input_spin.valueChanged.connect(self._on_params_changed)
        bar.addWidget(self._input_spin)

        bar.addWidget(QLabel("Floor:"))
        self._floor_spin = QSpinBox()
        self._floor_spin.setRange(0, 100)
        self._floor_spin.setValue(10)
        self._floor_spin.setFixedWidth(52)
        self._floor_spin.valueChanged.connect(self._on_params_changed)
        bar.addWidget(self._floor_spin)

        self._arm_btn = QPushButton("Arm")
        self._arm_btn.setFixedWidth(80)
        self._arm_btn.setEnabled(False)
        self._arm_btn.setCheckable(True)
        self._arm_btn.clicked.connect(self._on_arm_toggle)
        bar.addWidget(self._arm_btn)

        self._clear_btn = QPushButton("Clear")
        self._clear_btn.setFixedWidth(56)
        self._clear_btn.clicked.connect(self._on_clear)
        bar.addWidget(self._clear_btn)

        self._export_btn = QPushButton("Export CSV")
        self._export_btn.setFixedWidth(88)
        self._export_btn.clicked.connect(self._on_export_csv)
        bar.addWidget(self._export_btn)

        # Load captures from a saved telnet_logger.py session log. Works offline —
        # no serial connection required (deliberately always enabled).
        self._load_log_btn = QPushButton("Load Log…")
        self._load_log_btn.setFixedWidth(84)
        self._load_log_btn.setToolTip(
            "Load [SCOPE] captures from a saved telnet_logger.py log file"
        )
        self._load_log_btn.clicked.connect(self._on_load_log)
        bar.addWidget(self._load_log_btn)

        self._autosave_cb = QCheckBox("Auto-save")
        bar.addWidget(self._autosave_cb)

        bar.addStretch()
        return bar

    def _build_settings_bar(self) -> QHBoxLayout:
        bar = QHBoxLayout()
        bar.setSpacing(6)

        self._load_settings_btn = QPushButton("Load Settings")
        self._load_settings_btn.setFixedWidth(110)
        self._load_settings_btn.setEnabled(False)
        self._load_settings_btn.setToolTip(
            "Send 's' command and overlay pad config on graph"
        )
        self._load_settings_btn.clicked.connect(self._on_load_settings)
        bar.addWidget(self._load_settings_btn)

        bar.addWidget(QLabel("Threshold:"))
        self._thresh_lbl = QLabel("—")
        self._thresh_lbl.setStyleSheet(f"color: {_COLOR_HEAD};")
        bar.addWidget(self._thresh_lbl)

        bar.addSpacing(8)
        bar.addWidget(QLabel("Scan:"))
        self._scan_lbl = QLabel("—")
        self._scan_lbl.setStyleSheet(f"color: {COLOR_TEXT_SECONDARY};")
        bar.addWidget(self._scan_lbl)

        bar.addSpacing(8)
        bar.addWidget(QLabel("Mask:"))
        self._mask_lbl = QLabel("—")
        self._mask_lbl.setStyleSheet(f"color: {COLOR_TEXT_SECONDARY};")
        bar.addWidget(self._mask_lbl)

        bar.addSpacing(8)
        bar.addWidget(QLabel("Retrig:"))
        self._retrig_lbl = QLabel("—")
        self._retrig_lbl.setStyleSheet(f"color: {COLOR_TEXT_SECONDARY};")
        bar.addWidget(self._retrig_lbl)

        bar.addStretch()
        return bar

    def _build_serial_bar(self) -> QHBoxLayout:
        bar = QHBoxLayout()
        bar.setSpacing(6)

        bar.addWidget(QLabel("Serial:"))
        self._serial_input = QLineEdit()
        self._serial_input.setPlaceholderText("Enter command and press Send or Return…")
        mono = QFont("IBM Plex Mono", 9)
        mono.setStyleHint(QFont.StyleHint.Monospace)
        self._serial_input.setFont(mono)
        self._serial_input.returnPressed.connect(self._on_serial_send)
        bar.addWidget(self._serial_input, stretch=1)

        send_btn = QPushButton("Send")
        send_btn.setFixedWidth(56)
        send_btn.clicked.connect(self._on_serial_send)
        bar.addWidget(send_btn)

        return bar

    def _build_chart(self) -> QWidget:
        if not _PG:
            lbl = QLabel("pyqtgraph not installed — pip install pyqtgraph")
            lbl.setAlignment(Qt.AlignmentFlag.AlignCenter)
            lbl.setStyleSheet(f"color: {COLOR_TEXT_SECONDARY};")
            return lbl

        pw = pg.PlotWidget()
        pw.setBackground(_COLOR_CHART)
        pw.showGrid(x=True, y=True, alpha=0.3)

        grid_pen = pg.mkPen(color=_COLOR_GRID)
        mono = QFont("IBM Plex Mono", 8)
        mono.setStyleHint(QFont.StyleHint.Monospace)
        for axis_name in ("left", "bottom"):
            ax = pw.getPlotItem().getAxis(axis_name)
            ax.setPen(grid_pen)
            ax.setTickFont(mono)

        self._head_curve = pw.plot(
            pen=pg.mkPen(color=_COLOR_HEAD, width=2), name="Head"
        )
        self._rim_curve = pw.plot(
            pen=pg.mkPen(color=_COLOR_RIM, width=2), name="Rim"
        )

        self._floor_line = pg.InfiniteLine(
            pos=10, angle=0,
            pen=pg.mkPen(color="#aaaaaa", width=1, style=Qt.PenStyle.DashLine),
            label="Floor",
            labelOpts={"color": "#aaaaaa", "position": 0.05},
        )
        pw.addItem(self._floor_line)

        # Trigger at sample 100 (centre of the 200-sample window) = ~10.7ms
        self._trigger_line = pg.InfiniteLine(
            pos=100 * 0.107, angle=90,
            pen=pg.mkPen(color="#888888", width=1, style=Qt.PenStyle.DashLine),
            label="Trigger",
            labelOpts={"color": "#888888", "position": 0.9},
        )
        pw.addItem(self._trigger_line)
        pw.getPlotItem().getAxis("bottom").setLabel("Time (ms)")

        # Threshold line (from pad config)
        self._thresh_line = pg.InfiniteLine(
            pos=30, angle=0,
            pen=pg.mkPen(color=_COLOR_HEAD, width=1, style=Qt.PenStyle.DashLine),
            label="Threshold",
            labelOpts={"color": _COLOR_HEAD, "position": 0.15},
        )
        self._thresh_line.setVisible(False)
        pw.addItem(self._thresh_line)

        # Scan window region (from pad config)
        self._scan_region = pg.LinearRegionItem(
            values=[100, 110],
            orientation="vertical",
            brush=pg.mkBrush(color=(100, 200, 180, 60)),
            pen=pg.mkPen(color=(100, 200, 180, 180), width=1),
            movable=False,
        )
        self._scan_region.setVisible(False)
        pw.addItem(self._scan_region)

        # Mask region (double-hit lockout)
        self._mask_region = pg.LinearRegionItem(
            values=[100, 130],
            orientation="vertical",
            brush=pg.mkBrush(color=(251, 146, 60, 40)),
            pen=pg.mkPen(color=(251, 146, 60, 120), width=1),
            movable=False,
        )
        self._mask_region.setVisible(False)
        pw.addItem(self._mask_region)

        # Annotation — top-right corner, positioned in data coordinates after each plot
        self._annotation = pg.TextItem(anchor=(1.0, 0.0))
        self._annotation.setColor(QColor(COLOR_TEXT_PRIMARY))
        pw.addItem(self._annotation)

        self._plot_widget = pw
        return pw

    # ------------------------------------------------------------------
    # Port management
    # ------------------------------------------------------------------

    def _refresh_ports(self) -> None:
        if not _SERIAL:
            return
        current = self._port_combo.currentText()
        self._port_combo.clear()
        ports = [p.device for p in serial.tools.list_ports.comports()]
        self._port_combo.addItems(ports)
        idx = self._port_combo.findText(current)
        if idx >= 0:
            self._port_combo.setCurrentIndex(idx)

    def _on_connect_toggle(self) -> None:
        if self._serial and self._serial.is_open:
            self._disconnect_serial()
        else:
            self._connect_serial()

    def _connect_serial(self) -> None:
        if not _SERIAL:
            return
        port = self._port_combo.currentText()
        if not port:
            return
        try:
            # write_timeout is essential: the reader QThread blocks on readline()
            # on the same port object, and on Windows a GUI-thread write to a
            # contended port can otherwise hang indefinitely and freeze the app.
            # A bounded write_timeout turns that into a fast, catchable failure.
            self._serial = serial.Serial(port, BAUD_RATE, timeout=0.1, write_timeout=0.5)
        except Exception as exc:
            log.error("Serial connect failed: %s", exc)
            return
        self._reader = _SerialReader(self._serial)
        self._reader.scope_capture.connect(self._on_scope_capture)
        self._reader.serial_line.connect(self._on_serial_line)
        self._reader.adc_warning.connect(self._on_adc_warning)
        self._reader.start()
        self._connect_btn.setText("Disconnect")
        self._arm_btn.setEnabled(True)
        self._load_settings_btn.setEnabled(True)

    def _disconnect_serial(self) -> None:
        if self._reader:
            self._reader.stop()
            self._reader.wait(2000)
            self._reader = None
        if self._serial and self._serial.is_open:
            self._serial.close()
            self._serial = None
        self._connect_btn.setText("Connect")
        self._arm_btn.setEnabled(False)
        self._arm_btn.setChecked(False)
        self._arm_btn.setText("Arm")
        self._load_settings_btn.setEnabled(False)
        self._armed = False

    # ------------------------------------------------------------------
    # Scope controls
    # ------------------------------------------------------------------

    def _on_arm_toggle(self) -> None:
        if not (self._serial and self._serial.is_open):
            self._arm_btn.setChecked(False)
            return
        if self._arm_btn.isChecked():
            # Arm
            inp   = self._input_spin.value()
            floor = self._floor_spin.value()
            self._safe_write(f"o {inp} {floor}\n".encode())
            self._armed = True
            self._arm_btn.setText("Armed ●")
            self._arm_btn.setStyleSheet(f"color: {_COLOR_HEAD};")
            if _PG:
                self._floor_line.setValue(floor)
        else:
            # Disarm
            self._safe_write(b"o off\n")
            self._armed = False
            self._arm_btn.setText("Arm")
            self._arm_btn.setStyleSheet("")

    def _safe_write(self, data: bytes) -> bool:
        """Write to the serial port without ever letting a write timeout crash or
        freeze the GUI. The reader QThread holds the same port; on Windows a
        contended GUI-thread write can time out (SerialTimeoutException). We catch
        it, log it, and return False rather than propagating."""
        if not (self._serial and self._serial.is_open):
            return False
        try:
            self._serial.write(data)
            return True
        except Exception as exc:  # SerialTimeoutException and friends
            log.warning("serial write failed (%s): %r", exc, data)
            self._serial_output_append(
                f"⚠ serial write failed: {exc}", color=_COLOR_AMBER
            )
            return False

    def _on_params_changed(self) -> None:
        """Re-arm automatically if already connected and armed."""
        if self._serial and self._serial.is_open and self._armed:
            inp   = self._input_spin.value()
            floor = self._floor_spin.value()
            self._safe_write(f"o {inp} {floor}\n".encode())
            if _PG:
                self._floor_line.setValue(floor)

    def _on_load_settings(self) -> None:
        """Send 's' command; config lines are parsed in _on_serial_line."""
        if not (self._serial and self._serial.is_open):
            return
        self._reading_config = True
        self._safe_write(b"s\n")

    def _on_serial_send(self) -> None:
        """Send whatever is in the serial input bar."""
        if not (self._serial and self._serial.is_open):
            return
        text = self._serial_input.text().strip()
        if not text:
            return
        self._safe_write(f"{text}\n".encode())
        self._serial_input.clear()

    def _on_clear(self) -> None:
        self._captures.clear()
        self._capture_configs.clear()
        self._session_list.clear()
        if _PG:
            self._head_curve.setData([], [])
            self._rim_curve.setData([], [])
            self._annotation.setText("")

    # ------------------------------------------------------------------
    # Incoming data
    # ------------------------------------------------------------------

    def _add_capture(self, meta: dict, head: list, rim: list,
                     ts: Optional[str] = None,
                     config: Optional[dict] = None) -> int:
        """Append a capture to the session log and plot it. Shared by the live
        serial path and the log-file loader so a loaded capture behaves exactly
        like one that arrived live. ts defaults to now() (live); the file loader
        passes the log's own timestamp. config is the per-capture overlay config
        (see _capture_configs) — None for live captures."""
        idx = len(self._captures)
        self._captures.append((meta, head, rim))
        self._capture_configs.append(config)

        decision  = str(meta.get("decision", "?"))
        head_peak = int(meta.get("head_peak", 0))
        rim_peak  = int(meta.get("rim_peak", 0))
        inp       = int(meta.get("input", 0))
        if ts is None:
            ts = datetime.now().strftime("%H:%M:%S")

        text  = (
            f"#{idx + 1:>3}  [{decision:<3}]  "
            f"head={head_peak:>4}  rim={rim_peak:>4}  input={inp}  {ts}"
        )
        item  = QListWidgetItem(text)
        color = _COLOR_HEAD if decision == "HEAD" else _COLOR_RIM
        item.setForeground(QBrush(QColor(color)))
        item.setData(Qt.ItemDataRole.UserRole, idx)
        self._session_list.addItem(item)
        self._session_list.scrollToBottom()

        self._plot_capture(idx)
        return idx

    def _on_scope_capture(self, meta: dict, head: list, rim: list) -> None:
        idx = self._add_capture(meta, head, rim)
        if self._autosave_cb.isChecked():
            self._auto_save(idx)

    def _serial_output_append(self, line: str, color: str = "") -> None:
        """Append a line to the serial output widget."""
        item = QListWidgetItem(line)
        if color:
            item.setForeground(QBrush(QColor(color)))
        self._serial_output.addItem(item)
        self._serial_output.scrollToBottom()
        # Keep output manageable
        while self._serial_output.count() > 200:
            self._serial_output.takeItem(0)

    def _on_serial_line(self, line: str) -> None:
        """Handle non-scope serial lines; parse config block if pending."""
        log.debug("serial: %s", line)

        # Always show in serial output
        self._serial_output_append(line)

        # Config block parsing
        if line.startswith("[Config]"):
            self._reading_config = True
            return
        if self._reading_config:
            if line.strip() == "":
                # Blank line = end of config block
                self._reading_config = False
                return
            cfg = _SerialReader._parse_config_line(line, self._input_spin.value())
            if cfg:
                self._pad_config = cfg
                self._apply_pad_config(cfg)
                self._serial_output_append(
                    f"  → loaded config for input {self._input_spin.value()}",
                    color=_COLOR_HEAD
                )

    def _apply_pad_config(self, cfg: dict) -> None:
        """Update settings bar labels and graph overlays from pad config."""
        thresh = cfg.get("thresh", 0)
        scan   = cfg.get("scan",   0)
        mask   = cfg.get("mask",   0)
        retrig = cfg.get("retrig", 0)

        self._thresh_lbl.setText(str(thresh))
        self._scan_lbl.setText(f"{scan} ms")
        self._mask_lbl.setText(f"{mask} ms")
        self._retrig_lbl.setText(f"{retrig} ms")

        if not _PG:
            return

        # Threshold horizontal line
        self._thresh_line.setValue(thresh)
        self._thresh_line.setVisible(True)

        # Chart is now in ms — use ms values directly
        trigger_ms = 100 * 0.107   # trigger line position in ms

        scan_end = trigger_ms + scan
        mask_end = trigger_ms + mask

        self._scan_region.setRegion([trigger_ms, scan_end])
        self._scan_region.setVisible(True)

        self._mask_region.setRegion([scan_end, mask_end])
        self._mask_region.setVisible(True)

    def _copy_serial_output(self) -> None:
        """Copy selected serial output lines to clipboard."""
        items = self._serial_output.selectedItems()
        if not items:
            # Nothing selected — copy everything
            items = [
                self._serial_output.item(i)
                for i in range(self._serial_output.count())
            ]
        text = "\n".join(item.text() for item in items)
        QApplication.clipboard().setText(text)

    def _on_adc_warning(self) -> None:
        self._adc_warn.setVisible(True)

    # ------------------------------------------------------------------
    # Chart
    # ------------------------------------------------------------------

    def _plot_capture(self, idx: int) -> None:
        if not _PG:
            return
        meta, head, rim = self._captures[idx]
        MS_PER_SAMPLE = 0.107
        xs = [i * MS_PER_SAMPLE for i in range(len(head))]
        self._head_curve.setData(xs, head)
        self._rim_curve.setData(xs, rim)

        decision  = str(meta.get("decision", "?"))
        head_peak = int(meta.get("head_peak", 0))
        rim_peak  = int(meta.get("rim_peak", 0))
        color     = _COLOR_HEAD if decision == "HEAD" else _COLOR_RIM

        self._annotation.setColor(QColor(color))
        self._annotation.setText(
            f"decision={decision}\nhead_peak={head_peak}\nrim_peak={rim_peak}"
        )

        max_x = xs[-1] if xs else 200 * MS_PER_SAMPLE
        all_y = head + rim
        max_y = max(all_y) if all_y else 1023
        self._annotation.setPos(max_x, max_y)

        self._floor_line.setValue(self._floor_spin.value())

        # Update trigger line and overlays to ms coordinates
        trigger_ms = 100 * MS_PER_SAMPLE
        self._trigger_line.setValue(trigger_ms)
        self._plot_widget.getPlotItem().getAxis("bottom").setLabel("Time (ms)")

        # Scan/Mask/Threshold overlay. A loaded capture carries its own config (dict)
        # or an explicit "no config" ({}); a live capture (None) uses the global
        # _pad_config from "Load Settings" — the original, unchanged live behaviour.
        entry = self._capture_configs[idx] if idx < len(self._capture_configs) else None
        if entry is None:
            if self._pad_config:
                self._apply_pad_config(self._pad_config)
        elif entry:
            self._apply_pad_config(entry)
        else:
            self._hide_overlays()

    def _hide_overlays(self) -> None:
        """Blank the settings-bar labels and hide the config-driven chart overlays,
        for a loaded capture that has no applicable [Config] block. Reuses the
        existing overlay items — only toggles their visibility."""
        self._thresh_lbl.setText("—")
        self._scan_lbl.setText("—")
        self._mask_lbl.setText("—")
        self._retrig_lbl.setText("—")
        if not _PG:
            return
        self._thresh_line.setVisible(False)
        self._scan_region.setVisible(False)
        self._mask_region.setVisible(False)

    def _on_log_item_clicked(self, item: QListWidgetItem) -> None:
        idx = item.data(Qt.ItemDataRole.UserRole)
        if idx is not None and 0 <= idx < len(self._captures):
            self._plot_capture(idx)

    # ------------------------------------------------------------------
    # CSV export / auto-save
    # ------------------------------------------------------------------

    def _auto_save(self, idx: int) -> None:
        os.makedirs(self._auto_save_dir, exist_ok=True)
        meta, head, rim = self._captures[idx]
        ts   = datetime.now().strftime("%Y%m%d_%H%M%S_%f")
        path = os.path.join(self._auto_save_dir, f"scope_{ts}.csv")
        self._write_capture_csv(path, meta, head, rim)

    def _on_export_csv(self) -> None:
        if not self._captures:
            return
        path, _ = QFileDialog.getSaveFileName(
            self, "Export Session Log", "", "CSV files (*.csv)"
        )
        if not path:
            return
        with open(path, "w", newline="", encoding="utf-8") as f:
            w = csv.writer(f)
            w.writerow(["capture", "sample", "head", "rim",
                        "decision", "head_peak", "rim_peak", "input"])
            for i, (meta, head, rim) in enumerate(self._captures):
                decision  = meta.get("decision", "")
                head_peak = meta.get("head_peak", 0)
                rim_peak  = meta.get("rim_peak", 0)
                inp       = meta.get("input", 0)
                for s, (h, r) in enumerate(zip(head, rim)):
                    w.writerow([i + 1, s, h, r, decision, head_peak, rim_peak, inp])

    def _write_capture_csv(self, path: str, meta: dict, head: list, rim: list) -> None:
        with open(path, "w", newline="", encoding="utf-8") as f:
            w = csv.writer(f)
            w.writerow([f"# {meta}"])
            w.writerow(["sample", "head", "rim"])
            for s, (h, r) in enumerate(zip(head, rim)):
                w.writerow([s, h, r])

    # ------------------------------------------------------------------
    # Log file loading (offline — no serial connection needed)
    # ------------------------------------------------------------------

    _LOG_PREFIX_RE = re.compile(r"^\[(\d{2}:\d{2}:\d{2}\.\d{3})\]\s(.*)$")

    @classmethod
    def _split_log_prefix(cls, line: str) -> tuple[Optional[str], str]:
        """Split telnet_logger.py's per-line '[HH:MM:SS.mmm] ' timestamp prefix.
        Returns (timestamp, remainder). If the line has no such prefix, returns
        (None, line unchanged). The digit-only time pattern means a real
        '[SCOPE] ...' line is never mistaken for a prefix (S is not a digit) —
        the parser never sees the prefix at all."""
        m = cls._LOG_PREFIX_RE.match(line)
        if m:
            return m.group(1), m.group(2)
        return None, line

    def _on_load_log(self) -> None:
        start_dir = self._tools_logs_dir if os.path.isdir(self._tools_logs_dir) else ""
        path, _ = QFileDialog.getOpenFileName(
            self, "Load Scope Log File", start_dir,
            "Log files (*.log *.txt);;All files (*)"
        )
        if not path:
            return
        self._load_log_file(path)

    def _load_log_file(self, path: str) -> None:
        """Parse every [SCOPE] capture in a saved telnet_logger.py log and add each
        to the session log, reusing the exact live parser (_ScopeParser) and the
        live session-log/plot path (_add_capture).

        Also parses any [Config] blocks (same format the live 's' handler reads, via
        the shared _parse_config_row) so each capture is overlaid with the scan/mask
        that was actually in effect: a capture uses the most recent [Config] block
        that PRECEDES it in the file, matched on the capture's own input number. A
        capture with no preceding block, or whose input has no row in that block,
        loads fine but draws no overlay (never a later block applied retroactively).
        Malformed/partial captures are skipped and reported."""
        try:
            with open(path, "r", encoding="utf-8", errors="replace") as f:
                raw_lines = f.readlines()
        except OSError as exc:
            self._serial_output_append(
                f"⚠ could not read {os.path.basename(path)}: {exc}", color=_COLOR_AMBER
            )
            return

        parser = _ScopeParser()
        loaded = 0
        overlaid = 0
        malformed = 0
        config_block_count = 0
        last_ts: Optional[str] = None

        # current_config: {input_num: cfg_dict} from the most recently COMPLETED
        # [Config] block seen so far. pending_config: snapshot taken at the active
        # capture's [SCOPE] header, so a capture always resolves against the block
        # that preceded it — never one printed later in the file.
        current_config: dict = {}
        pending_config: dict = {}
        in_config = False
        building: Optional[dict] = None

        def _commit_config() -> None:
            nonlocal in_config, building, current_config
            if building:
                current_config = building     # rebind (never mutate) so earlier
            in_config = False                 # pending_config snapshots stay valid
            building = None

        def _consume(events: list) -> None:
            nonlocal loaded, overlaid, malformed
            for event in events:
                if event[0] != "capture":
                    continue   # ignore pass-through [HIT]/[ADC]/etc. lines
                meta, head, rim = event[1], event[2], event[3]
                expected = meta.get("samples", 200)
                if not isinstance(expected, int):
                    expected = 200
                # A capture is only well-formed if it reached its declared sample
                # count. Sub-length emits come from a flush (a new [SCOPE] or EOF
                # interrupting a capture mid-stream) → skip as partial/malformed.
                if head and len(head) == len(rim) == expected:
                    inp = meta.get("input", 0)
                    cfg = pending_config.get(inp) if pending_config else None
                    if cfg:
                        overlaid += 1
                    # {} = "no applicable config" → no overlay (see _plot_capture).
                    self._add_capture(meta, head, rim, ts=last_ts,
                                      config=(cfg if cfg else {}))
                    loaded += 1
                else:
                    malformed += 1

        for raw in raw_lines:
            ts, line = self._split_log_prefix(raw.rstrip("\r\n"))

            # A blank line terminates a [Config] block (mirrors the live handler).
            if not line:
                if in_config:
                    _commit_config()
                continue

            # --- [Config] block accumulation (file-level; shared row parsing) ---
            if line.startswith("[Config]"):
                _commit_config()             # close any prior unterminated block
                in_config = True
                building = {}
                config_block_count += 1
                continue
            if in_config:
                row = _SerialReader._parse_config_row(line)
                if row is not None:
                    building[row[0]] = row[1]
                    continue
                # A non-row line ends the block here (these logs terminate a block
                # with the next console line, e.g. '[w] ...' / '[eDrum] ...', not a
                # blank line); commit, then fall through to handle this line.
                _commit_config()

            # --- capture parsing ---
            # Remember the header timestamp so the session-log row shows when the hit
            # happened, and snapshot the config in effect for THIS capture.
            is_header = bool(ts) and line.startswith("[SCOPE]") and "samples=" in line
            if is_header:
                last_ts = ts
            _consume(parser.feed(line))
            if is_header:
                pending_config = current_config
        _consume(parser.finish())

        fname = os.path.basename(path)
        if loaded:
            msg = f"Loaded {loaded} capture(s) from {fname}"
            if malformed:
                msg += f"  ({malformed} malformed/partial skipped)"
            if config_block_count == 0:
                msg += "  — no [Config] block in file; no scan/mask overlay"
            else:
                msg += (f"  — {overlaid}/{loaded} with scan/mask overlay "
                        f"from {config_block_count} [Config] block(s)")
            self._serial_output_append(msg, color=_COLOR_HEAD)
        else:
            msg = f"No valid [SCOPE] captures found in {fname}"
            if malformed:
                msg += f"  ({malformed} malformed/partial skipped)"
            self._serial_output_append(msg, color=_COLOR_AMBER)

    # ------------------------------------------------------------------
    # Cleanup
    # ------------------------------------------------------------------

    def closeEvent(self, event) -> None:
        self._disconnect_serial()
        super().closeEvent(event)
