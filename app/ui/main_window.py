from __future__ import annotations

import logging
import sys
from typing import Optional

log = logging.getLogger("edrum.main_window")

from PyQt6.QtCore import Qt, QThread, pyqtSignal
from PyQt6.QtGui import QAction, QCloseEvent, QKeySequence, QShortcut
from PyQt6.QtWidgets import (
    QApplication,
    QComboBox,
    QLabel,
    QMainWindow,
    QMessageBox,
    QPushButton,
    QTabWidget,
    QToolBar,
    QVBoxLayout,
    QWidget,
)

try:
    import qtawesome as qta
    _QTA = True
except ImportError:
    _QTA = False

try:
    from ..transport.midi import DrumMidiTransport, request_identify
    from .connection_widget import ConnectionWidget
    from .pad_config_tab import PadConfigTab
    from .debug_tab import DebugTab
    from .presets_tab import PresetsTab
    from .theme import apply_dark_theme
except ImportError:
    from transport.midi import DrumMidiTransport, request_identify  # type: ignore[no-redef]
    from ui.connection_widget import ConnectionWidget               # type: ignore[no-redef]
    from ui.pad_config_tab import PadConfigTab                     # type: ignore[no-redef]
    from ui.debug_tab import DebugTab                              # type: ignore[no-redef]
    from ui.presets_tab import PresetsTab                          # type: ignore[no-redef]
    from ui.theme import apply_dark_theme                          # type: ignore[no-redef]

try:
    from ..emulator.transport import EmulatorTransport
    from ..emulator.window import EmulatorWindow
except ImportError:
    from emulator.transport import EmulatorTransport  # type: ignore[no-redef]
    from emulator.window import EmulatorWindow        # type: ignore[no-redef]


class _IdentifyWorker(QThread):
    finished = pyqtSignal(dict)
    failed   = pyqtSignal(str)

    def __init__(self, transport: DrumMidiTransport) -> None:
        super().__init__()
        self._transport = transport

    def run(self) -> None:
        try:
            result = request_identify(self._transport, timeout=2.0)
            self.finished.emit(result)
        except Exception as exc:
            self.failed.emit(str(exc))


class MainWindow(QMainWindow):
    def __init__(self, dev_mode: bool = False) -> None:
        super().__init__()
        self._dev_mode = dev_mode
        self._identify_worker: Optional[_IdentifyWorker] = None
        self._emulator_window: Optional[EmulatorWindow]  = None

        # Transport — must exist before _setup_central() builds tabs
        if "--emulator" in sys.argv:
            self._transport: DrumMidiTransport | EmulatorTransport = EmulatorTransport()
        else:
            self._transport = DrumMidiTransport()

        apply_dark_theme(QApplication.instance())

        self.setWindowTitle("eDrum Config")
        self.setMinimumSize(1280, 800)

        self._setup_menu_bar()
        self._setup_toolbar()
        self._setup_central()
        self._setup_status_bar()
        self._setup_shortcuts()

        # Emulator window — created after UI so transport is fully wired
        if isinstance(self._transport, EmulatorTransport):
            self._emulator_window = EmulatorWindow(self._transport)
            self._act_launch_emulator.setEnabled(True)
            self._emulator_window.show()

    # ------------------------------------------------------------------
    # UI construction
    # ------------------------------------------------------------------

    def _setup_menu_bar(self) -> None:
        mb = self.menuBar()

        file_menu = mb.addMenu("&File")
        quit_act  = QAction("&Quit", self)
        quit_act.setShortcut("Ctrl+Q")
        quit_act.triggered.connect(QApplication.instance().quit)
        file_menu.addAction(quit_act)

        dev_menu = mb.addMenu("&Device")

        self._act_connect = QAction("&Connect", self)
        self._act_connect.triggered.connect(self._on_connect)
        dev_menu.addAction(self._act_connect)

        self._act_disconnect = QAction("&Disconnect", self)
        self._act_disconnect.setEnabled(False)
        self._act_disconnect.triggered.connect(self._on_disconnect)
        dev_menu.addAction(self._act_disconnect)

        dev_menu.addSeparator()

        self._act_identify = QAction("&Identify device", self)
        self._act_identify.setEnabled(False)
        self._act_identify.triggered.connect(self._on_identify)
        dev_menu.addAction(self._act_identify)

        self._act_show_debug: Optional[QAction] = None
        if self._dev_mode:
            debug_menu = mb.addMenu("&Debug")
            self._act_show_debug = QAction("Show &Debug Tab", self)
            self._act_show_debug.setCheckable(True)
            self._act_show_debug.triggered.connect(self._toggle_debug_tab)
            debug_menu.addAction(self._act_show_debug)

        dev_menu = mb.addMenu("De&v")
        self._act_launch_emulator = QAction("Launch &Emulator", self)
        self._act_launch_emulator.setEnabled(False)
        self._act_launch_emulator.triggered.connect(self._on_launch_emulator)
        dev_menu.addAction(self._act_launch_emulator)

        help_menu = mb.addMenu("&Help")
        about_act = QAction("&About", self)
        about_act.triggered.connect(self._on_about)
        help_menu.addAction(about_act)

    def _setup_toolbar(self) -> None:
        tb = QToolBar("Main", self)
        tb.setMovable(False)
        self.addToolBar(tb)

        tb.addWidget(QLabel("Port: "))

        self._port_combo = QComboBox()
        self._port_combo.setMinimumWidth(220)
        tb.addWidget(self._port_combo)

        refresh_btn = QPushButton("⟳")
        refresh_btn.setFixedWidth(28)
        refresh_btn.setToolTip("Refresh MIDI ports")
        refresh_btn.clicked.connect(self._refresh_ports)
        if _QTA:
            refresh_btn.setIcon(qta.icon('fa5s.sync-alt', color='#888888'))
            refresh_btn.setText("")
        tb.addWidget(refresh_btn)

        tb.addSeparator()

        # Single toggle button — Connect when disconnected, Disconnect when connected
        self._btn_connect_toggle = QPushButton("Connect")
        self._btn_connect_toggle.setObjectName("btn_connect_toggle")
        self._btn_connect_toggle.setFixedWidth(130)
        self._btn_connect_toggle.clicked.connect(self._on_connect_toggle)
        self._btn_connect_toggle.setToolTip("Connect to selected MIDI port")
        if _QTA:
            self._btn_connect_toggle.setIcon(qta.icon('fa5s.plug', color='#e0e0e0'))
        tb.addWidget(self._btn_connect_toggle)

        tb.addSeparator()

        self._status_dot = QLabel("●")
        self._status_dot.setObjectName("status_dot")
        self._status_dot.setToolTip("Not connected")
        tb.addWidget(self._status_dot)

        tb.addSeparator()

        self._btn_refresh = QPushButton(" Refresh")
        self._btn_refresh.setFixedWidth(100)
        self._btn_refresh.setEnabled(False)
        self._btn_refresh.setToolTip("Re-read all pad settings from device")
        self._btn_refresh.clicked.connect(self._on_toolbar_refresh)
        if _QTA:
            self._btn_refresh.setIcon(qta.icon('fa5s.sync', color='#e0e0e0'))
        tb.addWidget(self._btn_refresh)

        self._btn_save_flash = QPushButton(" Flash")
        self._btn_save_flash.setFixedWidth(90)
        self._btn_save_flash.setEnabled(False)
        self._btn_save_flash.setToolTip("Write current settings to device flash")
        self._btn_save_flash.clicked.connect(self._on_toolbar_save_flash)
        if _QTA:
            self._btn_save_flash.setIcon(qta.icon('fa5s.save', color='#e0e0e0'))
        tb.addWidget(self._btn_save_flash)

        self._refresh_ports()

    def _setup_central(self) -> None:
        self._pad_config_tab = PadConfigTab(self._transport)
        self._debug_tab      = DebugTab()

        tabs = QTabWidget()
        tabs.addTab(self._pad_config_tab, "Pad Config")   # always 0

        self._PAD_CONFIG_IDX    = 0
        self._PRESETS_EDITOR_IDX = -1
        self._DEBUG_IDX          = -1

        if self._dev_mode:
            self._presets_editor_tab = PresetsTab()
            tabs.addTab(self._presets_editor_tab, "Presets (Editor)")  # 2
            tabs.addTab(self._debug_tab,           "Debug")             # 3
            self._PRESETS_EDITOR_IDX = 2
            self._DEBUG_IDX          = 3
        # User mode: no Presets editor, no Debug tab — clean minimal UI

        tabs.currentChanged.connect(self._on_tab_changed)
        self._pad_config_tab.status_message.connect(self.show_status)
        self._tabs = tabs
        self.setCentralWidget(tabs)

    def _setup_status_bar(self) -> None:
        self._conn_widget = ConnectionWidget()
        self.statusBar().addPermanentWidget(self._conn_widget)
        self.statusBar().showMessage("Ready")

    def _setup_shortcuts(self) -> None:
        if self._dev_mode:
            sc = QShortcut(QKeySequence("Ctrl+D"), self)
            sc.activated.connect(self._toggle_debug_tab)

    # ------------------------------------------------------------------
    # Port management
    # ------------------------------------------------------------------

    def _on_launch_emulator(self) -> None:
        if self._emulator_window is not None:
            self._emulator_window.show()

    def _refresh_ports(self) -> None:
        try:
            if isinstance(self._transport, EmulatorTransport):
                ports = EmulatorTransport.list_ports()
            else:
                ports = DrumMidiTransport.list_ports()
        except Exception:
            return

        import re
        seen, clean_ports = set(), []
        for name in ports.get("inputs", []):
            clean = re.sub(r'\s+\d+$', '', name).strip()
            if clean not in seen:
                seen.add(clean)
                clean_ports.append(clean)

        current = self._port_combo.currentText()
        self._port_combo.clear()
        self._port_combo.addItems(clean_ports)

        idx = self._port_combo.findText(current)
        if idx >= 0:
            self._port_combo.setCurrentIndex(idx)

    # ------------------------------------------------------------------
    # Transport send helper (logs to debug tab)
    # ------------------------------------------------------------------

    def _send(self, msg: bytearray) -> None:
        self._transport.send(msg)
        self._debug_tab.log_tx(msg)

    # ------------------------------------------------------------------
    # Tab management
    # ------------------------------------------------------------------

    def _on_tab_changed(self, index: int) -> None:
        self._pad_config_tab.set_active(index == self._PAD_CONFIG_IDX)
        if self._act_show_debug is not None:
            self._act_show_debug.setChecked(index == self._DEBUG_IDX)

    def _toggle_debug_tab(self) -> None:
        if not self._dev_mode:
            return
        if self._tabs.currentIndex() == self._DEBUG_IDX:
            self._tabs.setCurrentIndex(self._PAD_CONFIG_IDX)
        else:
            self._tabs.setCurrentIndex(self._DEBUG_IDX)

    def _on_connect_toggle(self) -> None:
        if self._transport.is_connected():
            self._on_disconnect()
        else:
            self._on_connect()

    def _on_toolbar_refresh(self) -> None:
        self._pad_config_tab._start_refresh()

    def _on_toolbar_save_flash(self) -> None:
        self._pad_config_tab._enqueue_save_to_flash()

    # ------------------------------------------------------------------
    # Connection lifecycle
    # ------------------------------------------------------------------

    def _on_connect(self) -> None:
        port = self._port_combo.currentText().strip()
        if not port:
            QMessageBox.warning(self, "No port selected",
                                "Please select a MIDI port from the drop-down.")
            return

        log.info("User initiated connect to '%s'", port)
        import re
        port_search = re.sub(r'\s+\d+$', '', port).strip()

        try:
            self._transport.connect(port_search)
        except Exception as exc:
            QMessageBox.critical(self, "Connection failed", str(exc))
            return

        self._set_connected_ui(self._transport._port_name or port)
        self._pad_config_tab.on_connected()
        self._debug_tab.on_connected()

        self._transport.add_listener("main_window", self._on_sysex_rx)

    def _on_disconnect(self) -> None:
        log.info("User initiated disconnect")
        self._transport.remove_listener("main_window")
        self._transport.disconnect()
        self._set_disconnected_ui()
        self._pad_config_tab.on_disconnected()
        self._debug_tab.on_disconnected()

    def _on_sysex_rx(self, parsed: dict) -> None:
        pay = parsed.get("payload", b"")
        raw = bytes([
            0xF0, 0x00, 0x7D,
            parsed.get("device_id", 0),
            parsed.get("cmd_high",  0),
            parsed.get("cmd_low",   0),
            *pay,
            0xF7,
        ])
        self._debug_tab.log_rx(parsed, raw)

    def _set_connected_ui(self, port_name: str) -> None:
        log.info("Connected to '%s'", port_name)
        self._btn_connect_toggle.setText("Disconnect")
        self._btn_connect_toggle.setToolTip(f"Disconnect from {port_name}")
        if _QTA:
            self._btn_connect_toggle.setIcon(qta.icon('fa5s.unlink', color='#2ecc71'))
        self._btn_connect_toggle.setStyleSheet(
            "QPushButton {"
            f"  background-color: #1a3a2a;"
            f"  color: #2ecc71;"
            f"  border: 1px solid #2ecc71;"
            "  border-radius: 3px; padding: 4px 8px;"
            "}"
            "QPushButton:hover {"
            f"  background-color: #3a1a1a;"
            f"  color: #e74c3c;"
            f"  border: 1px solid #e74c3c;"
            "}"
        )
        self._btn_refresh.setEnabled(True)
        self._btn_save_flash.setEnabled(True)
        self._act_connect.setEnabled(False)
        self._act_disconnect.setEnabled(True)
        self._act_identify.setEnabled(True)
        self._status_dot.setToolTip(f"Connected: {port_name}")
        self._conn_widget.set_connected(port_name)
        self.statusBar().showMessage(f"Connected to {port_name}")

    def _set_disconnected_ui(self) -> None:
        log.info("Disconnected")
        self._btn_connect_toggle.setText("Connect")
        self._btn_connect_toggle.setToolTip("Connect to selected MIDI port")
        self._btn_connect_toggle.setStyleSheet("")
        if _QTA:
            self._btn_connect_toggle.setIcon(qta.icon('fa5s.plug', color='#e0e0e0'))
        self._btn_refresh.setEnabled(False)
        self._btn_save_flash.setEnabled(False)
        self._act_connect.setEnabled(True)
        self._act_disconnect.setEnabled(False)
        self._act_identify.setEnabled(False)
        self._status_dot.setToolTip("Not connected")
        self._conn_widget.set_disconnected()
        self.statusBar().showMessage("Disconnected")

    # ------------------------------------------------------------------
    # Identify
    # ------------------------------------------------------------------

    def _on_identify(self) -> None:
        if self._identify_worker and self._identify_worker.isRunning():
            return
        self._act_identify.setEnabled(False)
        self.statusBar().showMessage("Identifying device…")

        worker = _IdentifyWorker(self._transport)
        worker.finished.connect(self._on_identify_done)
        worker.failed.connect(self._on_identify_failed)
        self._identify_worker = worker
        worker.start()

    def _on_identify_done(self, result: dict) -> None:
        log.info("Device identified: FW v%d.%d device_id=0x%02X inputs=%d",
                 result.get('fw_maj'), result.get('fw_min'),
                 result.get('device_id'), result.get('num_inputs'))
        self._identify_worker = None
        port = self._transport._port_name or ""
        self._conn_widget.set_identified(
            port, result["fw_maj"], result["fw_min"], result["num_inputs"]
        )
        self.statusBar().showMessage(
            f"eDrum v{result['fw_maj']}.{result['fw_min']}  —  "
            f"device {result['device_id']:#04x}  —  {result['num_inputs']} inputs"
        )
        self._act_identify.setEnabled(True)

    def _on_identify_failed(self, error: str) -> None:
        log.warning("Identify failed: %s", error)
        self._identify_worker = None
        self.statusBar().showMessage(f"Identify failed: {error}")
        QMessageBox.warning(self, "Identify failed",
                            f"Device did not respond:\n{error}")
        self._act_identify.setEnabled(True)

    # ------------------------------------------------------------------
    # Misc
    # ------------------------------------------------------------------

    def show_status(self, msg: str, timeout_ms: int = 3000) -> None:
        self.statusBar().showMessage(msg, timeout_ms)

    def _on_about(self) -> None:
        QMessageBox.about(
            self,
            "About eDrum Config",
            "<b>eDrum Config</b><br>"
            "Configuration tool for the eDrum head unit.<br><br>"
            "Protocol: SysEx over BLE MIDI  (manufacturer ID 00 7D)",
        )

    def closeEvent(self, event: QCloseEvent) -> None:
        if self._identify_worker and self._identify_worker.isRunning():
            self._identify_worker.quit()
            self._identify_worker.wait(3000)
        if self._transport.is_connected():
            self._transport.disconnect()
        # Close emulator window if open — prevents process hanging on exit
        if self._emulator_window is not None:
            self._emulator_window.hide()
            self._emulator_window.deleteLater()
            self._emulator_window = None
        event.accept()
