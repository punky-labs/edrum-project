from __future__ import annotations

from PyQt6.QtGui import QColor, QPalette
from PyQt6.QtWidgets import QApplication

# Background colours
COLOR_BG_DARK        = "#1a1a1a"
COLOR_BG_PANEL       = "#242424"
COLOR_BG_CARD        = "#2e2e2e"
COLOR_BG_CARD_SEL    = "#1a3a4a"
COLOR_BG_INPUT       = "#1e1e1e"

# Text colours
COLOR_TEXT_PRIMARY   = "#e0e0e0"
COLOR_TEXT_SECONDARY = "#888888"
COLOR_TEXT_DISABLED  = "#444444"

# Accent colours
COLOR_ACCENT         = "#00aacc"
COLOR_RIM            = "#cc6600"
COLOR_CONNECTED      = "#2ecc71"
COLOR_WARNING        = "#e74c3c"
COLOR_BORDER         = "#3a3a3a"

# Hit log
COLOR_HIT_HEAD       = "#00aacc"
COLOR_HIT_RIM        = "#cc6600"
COLOR_HIT_OTHER      = "#555555"   # grey — crosstalk / other pad hits

# Fonts
FONT_LABEL_SIZE      = 9
FONT_VALUE_SIZE      = 11
FONT_TITLE_SIZE      = 10

# Dimensions
CARD_MIN_WIDTH       = 120
CARD_MIN_HEIGHT      = 80
HIT_LOG_BARS         = 30
SLIDER_HEIGHT        = 160   # logical pixels for vertical trigger sliders


def apply_dark_theme(app: QApplication) -> None:
    """Apply a dark QPalette to the QApplication."""
    palette = QPalette()
    palette.setColor(QPalette.ColorRole.Window,          QColor(COLOR_BG_DARK))
    palette.setColor(QPalette.ColorRole.WindowText,      QColor(COLOR_TEXT_PRIMARY))
    palette.setColor(QPalette.ColorRole.Base,            QColor(COLOR_BG_INPUT))
    palette.setColor(QPalette.ColorRole.AlternateBase,   QColor(COLOR_BG_PANEL))
    palette.setColor(QPalette.ColorRole.Text,            QColor(COLOR_TEXT_PRIMARY))
    palette.setColor(QPalette.ColorRole.Button,          QColor(COLOR_BG_PANEL))
    palette.setColor(QPalette.ColorRole.ButtonText,      QColor(COLOR_TEXT_PRIMARY))
    palette.setColor(QPalette.ColorRole.Highlight,       QColor(COLOR_ACCENT))
    palette.setColor(QPalette.ColorRole.HighlightedText, QColor("#ffffff"))
    palette.setColor(QPalette.ColorRole.PlaceholderText, QColor(COLOR_TEXT_SECONDARY))
    palette.setColor(QPalette.ColorRole.ToolTipBase,     QColor(COLOR_BG_PANEL))
    palette.setColor(QPalette.ColorRole.ToolTipText,     QColor(COLOR_TEXT_PRIMARY))
    app.setPalette(palette)
