from __future__ import annotations

import csv
import logging
import os
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
        state              = "IDLE"
        meta: dict         = {}
        head_samples: list[int] = []
        rim_samples:  list[int] = []
        expected           = 0

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

            if line.startswith("[SCOPE]"):
                # Flush any in-progress capture before starting a new one
                if state == "READING_DATA" and head_samples:
                    self.scope_capture.emit(meta, list(head_samples), list(rim_samples))
                meta         = {}
                head_samples = []
                rim_samples  = []
                for part in line.split()[1:]:
                    if "=" in part:
                        k, v = part.split("=", 1)
                        try:
                            meta[k] = int(v)
                        except ValueError:
                            meta[k] = v
                expected = meta.get("samples", 200)
                state = "READING_HEADER"

            elif line == "T,H,R" and state == "READING_HEADER":
                state = "READING_DATA"

            elif state == "READING_DATA":
                parts = line.split(",")
                if len(parts) == 3:
                    try:
                        head_samples.append(int(parts[1]))
                        rim_samples.append(int(parts[2]))
                    except ValueError:
                        self.serial_line.emit(line)
                    else:
                        if len(head_samples) >= expected:
                            self.scope_capture.emit(
                                meta, list(head_samples), list(rim_samples)
                            )
                            head_samples = []
                            rim_samples  = []
                            state = "IDLE"
                else:
                    self.serial_line.emit(line)

            else:
                if "[ADC]" in line:
                    self.adc_warning.emit()
                self.serial_line.emit(line)

    # Config response helpers
    @staticmethod
    def _parse_config_line(line: str, input_idx: int) -> Optional[dict]:
        """Parse a [Config] input line e.g. '  [0] note=36 thresh=30 ...'
        Returns dict of int values if it matches input_idx, else None."""
        import re
        m = re.match(r"\s*\[(\d+)\]", line)
        if not m or int(m.group(1)) != input_idx:
            return None
        result: dict = {}
        for kv in re.finditer(r"(\w+)=(\d+)", line):
            result[kv.group(1)] = int(kv.group(2))
        return result if result else None


class ScopeWindow(QMainWindow):
    """Floating ADC scope window — dev mode only."""

    def __init__(self, parent=None) -> None:
        super().__init__(parent)
        self.setWindowTitle("eDrum — ADC Scope")
        self.setMinimumSize(1000, 700)

        self._serial:   Optional["serial.Serial"] = None
        self._reader:   Optional[_SerialReader]   = None
        self._captures: list[tuple[dict, list, list]] = []
        self._armed:    bool = False
        self._pad_config: dict = {}           # current pad config from 's' command
        self._reading_config: bool = False    # True while parsing [Config] block
        self._auto_save_dir = os.path.normpath(
            os.path.join(os.path.dirname(__file__), "..", "logs", "scope")
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

        # Trigger at sample 100 (centre of the 200-sample window)
        self._trigger_line = pg.InfiniteLine(
            pos=100, angle=90,
            pen=pg.mkPen(color="#888888", width=1, style=Qt.PenStyle.DashLine),
            label="Trigger",
            labelOpts={"color": "#888888", "position": 0.9},
        )
        pw.addItem(self._trigger_line)

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
            brush=pg.mkBrush(color=(100, 200, 180, 25)),
            pen=pg.mkPen(color=(100, 200, 180, 80), width=1),
            movable=False,
        )
        self._scan_region.setVisible(False)
        pw.addItem(self._scan_region)

        # Mask region (double-hit lockout)
        self._mask_region = pg.LinearRegionItem(
            values=[100, 130],
            orientation="vertical",
            brush=pg.mkBrush(color=(251, 146, 60, 20)),
            pen=pg.mkPen(color=(251, 146, 60, 60), width=1),
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
            self._serial = serial.Serial(port, BAUD_RATE, timeout=0.1)
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
            self._serial.write(f"o {inp} {floor}\n".encode())
            self._armed = True
            self._arm_btn.setText("Armed ●")
            self._arm_btn.setStyleSheet(f"color: {_COLOR_HEAD};")
            if _PG:
                self._floor_line.setValue(floor)
        else:
            # Disarm
            self._serial.write(b"o off\n")
            self._armed = False
            self._arm_btn.setText("Arm")
            self._arm_btn.setStyleSheet("")

    def _on_params_changed(self) -> None:
        """Re-arm automatically if already connected and armed."""
        if self._serial and self._serial.is_open and self._armed:
            inp   = self._input_spin.value()
            floor = self._floor_spin.value()
            self._serial.write(f"o {inp} {floor}\n".encode())
            if _PG:
                self._floor_line.setValue(floor)

    def _on_load_settings(self) -> None:
        """Send 's' command; config lines are parsed in _on_serial_line."""
        if not (self._serial and self._serial.is_open):
            return
        self._reading_config = True
        self._serial.write(b"s\n")

    def _on_serial_send(self) -> None:
        """Send whatever is in the serial input bar."""
        if not (self._serial and self._serial.is_open):
            return
        text = self._serial_input.text().strip()
        if not text:
            return
        self._serial.write(f"{text}\n".encode())
        self._serial_input.clear()

    def _on_clear(self) -> None:
        self._captures.clear()
        self._session_list.clear()
        if _PG:
            self._head_curve.setData([], [])
            self._rim_curve.setData([], [])
            self._annotation.setText("")

    # ------------------------------------------------------------------
    # Incoming data
    # ------------------------------------------------------------------

    def _on_scope_capture(self, meta: dict, head: list, rim: list) -> None:
        idx       = len(self._captures)
        self._captures.append((meta, head, rim))

        decision  = str(meta.get("decision", "?"))
        head_peak = int(meta.get("head_peak", 0))
        rim_peak  = int(meta.get("rim_peak", 0))
        inp       = int(meta.get("input", 0))
        ts        = datetime.now().strftime("%H:%M:%S")

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

        # Convert scan/mask ms → approximate sample count
        # Core 1 samples all 8 channels; MCP3008 ~75k SPS total → ~9375 SPS per channel
        # ≈ 0.107 ms per sample → samples = ms / 0.107
        MS_PER_SAMPLE = 0.107
        scan_samples  = int(scan   / MS_PER_SAMPLE)
        mask_samples  = int(mask   / MS_PER_SAMPLE)
        trigger_pos   = 100   # trigger line is always at sample 100

        self._scan_region.setRegion([trigger_pos, trigger_pos + scan_samples])
        self._scan_region.setVisible(True)

        self._mask_region.setRegion([trigger_pos, trigger_pos + mask_samples])
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
        xs = list(range(len(head)))
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

        max_x = xs[-1] if xs else 200
        all_y = head + rim
        max_y = max(all_y) if all_y else 1023
        self._annotation.setPos(max_x, max_y)

        self._floor_line.setValue(self._floor_spin.value())

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
    # Cleanup
    # ------------------------------------------------------------------

    def closeEvent(self, event) -> None:
        self._disconnect_serial()
        super().closeEvent(event)
