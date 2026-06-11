"""
eDrum asset loader — loads and caches pad icons as QPixmaps.
Icons live in app/assets/pads/ relative to this file.
SVG icons are preferred over PNG; falls back to PNG if SVG not found.
SVG icons are recoloured at render time using the supplied colour.
"""
from __future__ import annotations

import os
from typing import Optional

from PyQt6.QtCore import Qt
from PyQt6.QtGui import QColor, QPainter, QPixmap
from PyQt6.QtSvg import QSvgRenderer

try:
    from .theme import COLOR_TEXT_SECONDARY
except ImportError:
    from ui.theme import COLOR_TEXT_SECONDARY  # type: ignore[no-redef]

# Directory containing pad icons
_ASSETS_DIR = os.path.normpath(
    os.path.join(os.path.dirname(__file__), "..", "assets", "pads")
)

# Mapping from PAD_NAMES entries to icon base names (no extension)
# Loader tries .svg first, then .png
PAD_ICON_MAP: dict[str, str] = {
    "Unassigned":      "unassigned",
    "Kick":            "kick",
    "Snare":           "snare",
    "Hi-Hat":          "hihat",
    "Tom 1":           "racktom",
    "Tom 2":           "racktom",
    "Tom 3":           "floortom",
    "Tom 4":           "floortom",
    "Ride (Head/Rim)": "ride",
    "Ride (Bell)":     "ride",
    "Crash 1":         "crash",
    "Crash 2":         "crash",
    "Crash 3":         "crash",
    "China":           "crash",
    "Splash":          "crash",
    "Cowbell":         "unassigned",
    "Tambourine":      "unassigned",
}

# Cache: (base_name, size, color_hex) -> QPixmap
_cache: dict[tuple[str, int, str], QPixmap] = {}


def _resolve_path(base_name: str) -> Optional[str]:
    """Return the first existing path for base_name, trying SVG then PNG."""
    for ext in (".svg", ".png"):
        path = os.path.join(_ASSETS_DIR, base_name + ext)
        if os.path.exists(path):
            return path
    return None


def _render_svg(path: str, size: int, color: str) -> Optional[QPixmap]:
    """
    Render an SVG at `size` x `size` pixels and recolour it.

    Works by rendering the SVG onto a transparent pixmap, then using
    CompositionMode_SourceIn to flood-fill the icon's alpha channel
    with the requested colour. This gives clean single-colour icons
    from any SVG whose paths use fill (not stroke).
    """
    renderer = QSvgRenderer(path)
    if not renderer.isValid():
        return None

    # Render SVG at target size onto a transparent pixmap
    pixmap = QPixmap(size, size)
    pixmap.fill(Qt.GlobalColor.transparent)
    painter = QPainter(pixmap)
    painter.setRenderHint(QPainter.RenderHint.Antialiasing, True)
    renderer.render(painter)
    painter.end()

    # Recolour: flood-fill with target colour, masked by the alpha channel
    painter = QPainter(pixmap)
    painter.setRenderHint(QPainter.RenderHint.Antialiasing, True)
    painter.setCompositionMode(QPainter.CompositionMode.CompositionMode_SourceIn)
    painter.fillRect(pixmap.rect(), QColor(color))
    painter.end()

    return pixmap


def _render_png(path: str, size: int) -> Optional[QPixmap]:
    """Load and scale a PNG icon."""
    pixmap = QPixmap(path)
    if pixmap.isNull():
        return None
    return pixmap.scaled(
        size, size,
        Qt.AspectRatioMode.KeepAspectRatio,
        Qt.TransformationMode.SmoothTransformation,
    )


def load_pad_icon(
    pad_name: str,
    size: int = 64,
    color: str = COLOR_TEXT_SECONDARY,
) -> Optional[QPixmap]:
    """
    Load and cache a pad icon for the given pad name.

    SVG icons are recoloured with `color` at render time.
    PNG icons are returned as-is (no recolouring).

    Args:
        pad_name: The pad name as it appears in PAD_NAMES
        size:     Logical pixel size for the icon
        color:    Hex colour string for SVG recolouring (e.g. '#00aabb')
    """
    base_name = PAD_ICON_MAP.get(pad_name, "unassigned")
    cache_key = (base_name, size, color)

    if cache_key in _cache:
        return _cache[cache_key]

    path = _resolve_path(base_name)
    if path is None:
        path = _resolve_path("unassigned")
    if path is None:
        return None

    if path.endswith(".svg"):
        pixmap = _render_svg(path, size, color)
    else:
        pixmap = _render_png(path, size)

    if pixmap is not None:
        _cache[cache_key] = pixmap

    return pixmap


def clear_cache() -> None:
    """Clear the pixmap cache (e.g. after a colour theme change)."""
    _cache.clear()
