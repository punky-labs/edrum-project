from PyQt6.QtWidgets import QWidget, QHBoxLayout, QLabel


class ConnectionWidget(QWidget):
    """Status bar widget: coloured dot + descriptive text for connection state."""

    _RED   = "color: #e74c3c; font-size: 13px;"
    _GREEN = "color: #2ecc71; font-size: 13px;"

    def __init__(self, parent=None):
        super().__init__(parent)
        layout = QHBoxLayout(self)
        layout.setContentsMargins(4, 0, 4, 0)
        layout.setSpacing(4)

        self._dot  = QLabel("●")
        self._text = QLabel("Not connected")
        self._dot.setStyleSheet(self._RED)

        layout.addWidget(self._dot)
        layout.addWidget(self._text)

    def set_disconnected(self):
        self._dot.setStyleSheet(self._RED)
        self._text.setText("Not connected")

    def set_connected(self, port_name: str):
        self._dot.setStyleSheet(self._GREEN)
        self._text.setText(f"Connected: {port_name}")

    def set_identified(self, port_name: str, fw_maj: int, fw_min: int, num_inputs: int):
        self._dot.setStyleSheet(self._GREEN)
        self._text.setText(
            f"{port_name}  —  FW v{fw_maj}.{fw_min}  —  {num_inputs} inputs"
        )
