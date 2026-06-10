"""
Preset library manager for eDrum config app.

Loads/saves app/presets.json. Provides helpers for the UI.

Preset schema per entry:
  pad_type, threshold, head_sensitivity, scan_time, mask_time,
  rim_threshold, rim_sensitivity
  (all int)
"""
from __future__ import annotations

import json
import os

PRESETS_PATH = os.path.normpath(
    os.path.join(os.path.dirname(__file__), "..", "presets.json")
)

CATEGORIES: list[str] = ["Drum", "Cymbal", "Other", "My Presets"]


def load_presets() -> dict:
    try:
        with open(PRESETS_PATH, encoding="utf-8") as f:
            return json.load(f)
    except (FileNotFoundError, json.JSONDecodeError):
        return {cat: {} for cat in CATEGORIES}


def save_presets(data: dict) -> None:
    with open(PRESETS_PATH, "w", encoding="utf-8") as f:
        json.dump(data, f, indent=2)


def get_category_models(data: dict, category: str) -> list[str]:
    return sorted(data.get(category, {}).keys())


def get_preset(data: dict, category: str, model: str) -> dict | None:
    return data.get(category, {}).get(model)


def save_user_preset(name: str, values: dict) -> None:
    data = load_presets()
    data.setdefault("My Presets", {})[name] = values
    save_presets(data)


def delete_user_preset(name: str) -> None:
    data = load_presets()
    data.get("My Presets", {}).pop(name, None)
    save_presets(data)


def is_user_preset(category: str) -> bool:
    return category == "My Presets"
