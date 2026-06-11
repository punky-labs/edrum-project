from __future__ import annotations

import os

from PyQt6.QtGui import QColor, QPalette
from PyQt6.QtWidgets import QApplication

# ---------------------------------------------------------------------------
# Colour tokens
# Keep these in sync with the TOKEN MAP in boal_base.qss
# ---------------------------------------------------------------------------

# Backgrounds
COLOR_BG_DARK        = "#141414"   # bg-base
COLOR_BG_PANEL       = "#1e1e1e"   # bg-surface
COLOR_BG_CARD        = "#252525"   # bg-card
COLOR_BG_CARD_SEL    = "#0d2a36"   # bg-card-sel
COLOR_BG_INPUT       = "#1a1a1a"   # bg-input

# Text
COLOR_TEXT_PRIMARY   = "#d8d4ce"   # text-primary  (warm off-white)
COLOR_TEXT_SECONDARY = "#6b6b6b"   # text-secondary
COLOR_TEXT_DISABLED  = "#3a3a3a"   # text-disabled

# Accents
COLOR_ACCENT         = "#00aabb"   # accent (teal)
COLOR_RIM            = "#cc6600"   # accent-rim (orange)
COLOR_CONNECTED      = "#2ecc71"   # connected (green)
COLOR_WARNING        = "#e74c3c"   # warning (red)
COLOR_BORDER         = "#2a2a2a"   # border

# Hit log
COLOR_HIT_HEAD       = "#00aabb"   # matches accent
COLOR_HIT_RIM        = "#cc6600"   # matches accent-rim
COLOR_HIT_OTHER      = "#3a3a3a"   # grey — crosstalk / other pad hits

# Fonts
FONT_LABEL_SIZE      = 9
FONT_VALUE_SIZE      = 11
FONT_TITLE_SIZE      = 10

# Dimensions
CARD_MIN_WIDTH       = 120
CARD_MIN_HEIGHT      = 80
HIT_LOG_BARS         = 30
SLIDER_HEIGHT        = 160   # logical pixels for vertical trigger sliders


# ---------------------------------------------------------------------------
# Stylesheet loader
# ---------------------------------------------------------------------------

_STYLES_DIR = os.path.join(os.path.dirname(__file__), "..", "assets", "styles")


def _load_qss(*filenames: str) -> str:
    """Read and concatenate one or more .qss files from the styles directory."""
    parts = []
    for name in filenames:
        path = os.path.normpath(os.path.join(_STYLES_DIR, name))
        try:
            with open(path, encoding="utf-8") as f:
                parts.append(f.read())
        except OSError as exc:
            import logging
            logging.getLogger("edrum.theme").warning("Could not load stylesheet %s: %s", path, exc)
    return "\n".join(parts)


def apply_dark_theme(app: QApplication) -> None:
    """Apply the BOAL base palette + eDrum stylesheet to the QApplication."""

    # QPalette — fallback colours for widgets not covered by QSS
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

    # QSS stylesheet — base + product overrides
    stylesheet = _load_qss("boal_base.qss", "edrum.qss")
    app.setStyleSheet(stylesheet)
