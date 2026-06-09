"""
eDrum asset loader — loads and caches pad icons as QPixmaps.
Icons live in app/assets/pads/ relative to this file.
"""
from __future__ import annotations

import os
from typing import Optional

from PyQt6.QtGui import QPixmap
from PyQt6.QtCore import Qt

# Directory containing pad icon PNGs
_ASSETS_DIR = os.path.join(os.path.dirname(__file__), "..", "assets", "pads")
_ASSETS_DIR = os.path.normpath(_ASSETS_DIR)

# Mapping from PAD_NAMES entries to icon filenames
PAD_ICON_MAP: dict[str, str] = {
    "Unassigned":      "unassigned.png",
    "Kick":            "kick.png",
    "Snare":           "snare.png",
    "Hi-Hat":          "hihat.png",
    "Tom 1":           "racktom.png",
    "Tom 2":           "racktom.png",
    "Tom 3":           "floortom.png",
    "Tom 4":           "floortom.png",
    "Ride (Head/Rim)": "ride.png",
    "Ride (Bell)":     "ride.png",
    "Crash 1":         "crash.png",
    "Crash 2":         "crash.png",
    "Crash 3":         "crash.png",
    "China":           "crash.png",
    "Splash":          "crash.png",
    "Cowbell":         "unassigned.png",
    "Tambourine":      "unassigned.png",
}

# Cache: (filename, size) -> QPixmap
_cache: dict[tuple[str, int], QPixmap] = {}


def load_pad_icon(pad_name: str, size: int = 64) -> Optional[QPixmap]:
    """
    Load and cache a pad icon for the given pad name.

    Returns a QPixmap scaled to size x size, or None if the
    file cannot be found or loaded.

    Args:
        pad_name: The pad name as it appears in PAD_NAMES
                  (e.g. "Snare", "Kick", "Hi-Hat")
        size:     Logical pixel size for the icon (default 64)
    """
    filename = PAD_ICON_MAP.get(pad_name, "unassigned.png")
    cache_key = (filename, size)

    if cache_key in _cache:
        return _cache[cache_key]

    path = os.path.join(_ASSETS_DIR, filename)
    if not os.path.exists(path):
        fallback = os.path.join(_ASSETS_DIR, "unassigned.png")
        if not os.path.exists(fallback):
            return None
        path = fallback

    pixmap = QPixmap(path)
    if pixmap.isNull():
        return None

    # Scale to requested size maintaining aspect ratio with smooth transformation
    scaled = pixmap.scaled(
        size, size,
        Qt.AspectRatioMode.KeepAspectRatio,
        Qt.TransformationMode.SmoothTransformation,
    )

    _cache[cache_key] = scaled
    return scaled


def clear_cache() -> None:
    """Clear the pixmap cache (e.g. if assets change at runtime)."""
    _cache.clear()
