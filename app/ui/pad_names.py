from __future__ import annotations

import json
import os

_JSON_PATH = os.path.join(os.path.dirname(__file__), "..", "pad_names.json")

PAD_NAMES: list[str] = [
    "Unassigned",
    "Kick",
    "Snare",
    "Hi-Hat",
    "Tom 1",
    "Tom 2",
    "Tom 3",
    "Tom 4",
    "Ride (Head/Rim)",
    "Ride (Bell)",
    "Crash 1",
    "Crash 2",
    "Crash 3",
    "China",
    "Splash",
    "Cowbell",
    "Tambourine",
]

_NUM_INPUTS = 9


def default_names() -> dict[int, str]:
    return {i: "Unassigned" for i in range(_NUM_INPUTS)}


def load_pad_names() -> dict[int, str]:
    result = default_names()
    try:
        with open(_JSON_PATH, "r", encoding="utf-8") as f:
            raw = json.load(f)
        for k, v in raw.items():
            try:
                idx = int(k)
                if 0 <= idx < _NUM_INPUTS and isinstance(v, str):
                    result[idx] = v
            except (ValueError, TypeError):
                pass
    except (FileNotFoundError, json.JSONDecodeError):
        pass
    return result


def save_pad_names(names: dict[int, str]) -> None:
    try:
        data = {str(k): v for k, v in names.items()}
        with open(_JSON_PATH, "w", encoding="utf-8") as f:
            json.dump(data, f, indent=2)
    except OSError:
        pass
