from __future__ import annotations

import logging
from typing import Optional

log = logging.getLogger("edrum.presets_tab")

from PyQt6.QtCore import Qt
from PyQt6.QtWidgets import (
    QComboBox,
    QGroupBox,
    QHBoxLayout,
    QInputDialog,
    QLabel,
    QPushButton,
    QSpinBox,
    QSplitter,
    QTreeWidget,
    QTreeWidgetItem,
    QVBoxLayout,
    QWidget,
)

try:
    from .theme import (
        COLOR_BG_DARK, COLOR_BG_PANEL, COLOR_BG_INPUT,
        COLOR_TEXT_PRIMARY, COLOR_TEXT_SECONDARY,
        COLOR_BORDER, COLOR_ACCENT,
        FONT_LABEL_SIZE, FONT_TITLE_SIZE,
    )
    from .presets import (
        CATEGORIES, load_presets, save_presets, get_category_models,
        get_preset, delete_user_preset, is_user_preset,
    )
except ImportError:
    from ui.theme import (  # type: ignore[no-redef]
        COLOR_BG_DARK, COLOR_BG_PANEL, COLOR_BG_INPUT,
        COLOR_TEXT_PRIMARY, COLOR_TEXT_SECONDARY,
        COLOR_BORDER, COLOR_ACCENT,
        FONT_LABEL_SIZE, FONT_TITLE_SIZE,
    )
    from ui.presets import (  # type: ignore[no-redef]
        CATEGORIES, load_presets, save_presets, get_category_models,
        get_preset, delete_user_preset, is_user_preset,
    )

try:
    from ..protocol.sysex import PAD_TYPE_NAMES, PAD_TYPE_PIEZO
except ImportError:
    from protocol.sysex import PAD_TYPE_NAMES, PAD_TYPE_PIEZO  # type: ignore[no-redef]


class PresetsTab(QWidget):
    def __init__(self, parent: Optional[QWidget] = None) -> None:
        super().__init__(parent)
        self._data: dict = load_presets()
        self._current_category: Optional[str] = None
        self._current_model: Optional[str] = None
        self._build_ui()
        self._populate_tree()

    # ------------------------------------------------------------------
    # UI construction
    # ------------------------------------------------------------------

    def _build_ui(self) -> None:
        layout = QVBoxLayout(self)
        layout.setContentsMargins(0, 0, 0, 0)
        layout.setSpacing(0)

        splitter = QSplitter(Qt.Orientation.Horizontal)
        splitter.setHandleWidth(2)
        layout.addWidget(splitter)

        splitter.addWidget(self._build_left_panel())
        splitter.addWidget(self._build_right_panel())
        splitter.setStretchFactor(0, 0)
        splitter.setStretchFactor(1, 1)

    def _build_left_panel(self) -> QWidget:
        w = QWidget()
        w.setStyleSheet(f"background: {COLOR_BG_PANEL};")
        vl = QVBoxLayout(w)
        vl.setContentsMargins(8, 8, 8, 8)
        vl.setSpacing(6)

        title = QLabel("PRESET LIBRARY")
        title.setStyleSheet(
            f"color: {COLOR_TEXT_SECONDARY}; font-size: {FONT_TITLE_SIZE}px;"
            " font-weight: bold; letter-spacing: 2px;"
        )
        vl.addWidget(title)

        self._tree = QTreeWidget()
        self._tree.setHeaderHidden(True)
        self._tree.setMinimumWidth(200)
        self._tree.setStyleSheet(
            f"QTreeWidget {{ background: {COLOR_BG_DARK}; color: {COLOR_TEXT_PRIMARY};"
            f" border: 1px solid {COLOR_BORDER}; outline: none; }}"
            f"QTreeWidget::item {{ padding: 2px 4px; }}"
            f"QTreeWidget::item:selected {{ background: {COLOR_ACCENT}; color: #ffffff; }}"
        )
        self._tree.currentItemChanged.connect(self._on_tree_selection)
        vl.addWidget(self._tree)
        return w

    def _build_right_panel(self) -> QWidget:
        w = QWidget()
        w.setStyleSheet(f"background: {COLOR_BG_DARK};")
        vl = QVBoxLayout(w)
        vl.setContentsMargins(12, 12, 12, 12)
        vl.setSpacing(8)

        form_group = QGroupBox("PRESET VALUES")
        form_group.setStyleSheet(self._group_style())
        fl = QVBoxLayout(form_group)
        fl.setSpacing(6)
        fl.setContentsMargins(8, 16, 8, 8)

        spin_style = (
            f"QSpinBox {{ background: {COLOR_BG_INPUT}; color: {COLOR_TEXT_PRIMARY};"
            f" border: 1px solid {COLOR_BORDER}; border-radius: 3px; padding: 2px 4px; }}"
            f"QSpinBox::up-button, QSpinBox::down-button {{"
            f" width: 16px; background: {COLOR_BG_PANEL}; border: none; }}"
        )

        def _row(label: str, widget: QWidget) -> QHBoxLayout:
            row = QHBoxLayout()
            lbl = QLabel(label)
            lbl.setFixedWidth(150)
            lbl.setStyleSheet(
                f"color: {COLOR_TEXT_SECONDARY}; font-size: {FONT_LABEL_SIZE}px;"
            )
            row.addWidget(lbl)
            row.addWidget(widget)
            row.addStretch()
            return row

        self._edit_type = QComboBox()
        for k, v in PAD_TYPE_NAMES.items():
            self._edit_type.addItem(v, k)
        fl.addLayout(_row("Pad Type:", self._edit_type))

        spin_fields = [
            ("_edit_thresh",     "Threshold:",          0, 1023),
            ("_edit_sens",       "Head Sensitivity:",   0, 1023),
            ("_edit_scan",       "Scan Time (ms):",     0, 100),
            ("_edit_mask",       "Mask Time (ms):",     0, 500),
            ("_edit_rim_thresh", "Rim Threshold:",      0, 1023),
            ("_edit_rim_sens",   "Rim Sensitivity:",    0, 1023),
        ]
        for attr, label, vmin, vmax in spin_fields:
            spin = QSpinBox()
            spin.setRange(vmin, vmax)
            spin.setFixedWidth(90)
            spin.setStyleSheet(spin_style)
            setattr(self, attr, spin)
            fl.addLayout(_row(label, spin))

        vl.addWidget(form_group)
        vl.addStretch()

        btn_row = QHBoxLayout()
        self._save_btn   = QPushButton("Save Changes")
        self._delete_btn = QPushButton("Delete Preset")
        self._new_btn    = QPushButton("New Preset")
        for btn in (self._save_btn, self._delete_btn, self._new_btn):
            btn_row.addWidget(btn)
        btn_row.addStretch()
        vl.addLayout(btn_row)

        self._save_btn.clicked.connect(self._on_save)
        self._delete_btn.clicked.connect(self._on_delete)
        self._new_btn.clicked.connect(self._on_new)

        self._set_form_enabled(False)
        self._delete_btn.setEnabled(False)
        return w

    @staticmethod
    def _group_style() -> str:
        return (
            f"QGroupBox {{"
            f"  background-color: {COLOR_BG_PANEL};"
            f"  border: 1px solid {COLOR_BORDER};"
            f"  border-radius: 6px;"
            f"  margin-top: 12px;"
            f"  font-size: {FONT_TITLE_SIZE}px;"
            f"  color: {COLOR_TEXT_SECONDARY};"
            f"}}"
            f"QGroupBox::title {{"
            f"  subcontrol-origin: margin;"
            f"  left: 8px;"
            f"  padding: 0 4px;"
            f"}}"
        )

    # ------------------------------------------------------------------
    # Tree
    # ------------------------------------------------------------------

    def _populate_tree(self) -> None:
        self._tree.clear()
        for cat in CATEGORIES:
            cat_item = QTreeWidgetItem([cat])
            cat_item.setData(0, Qt.ItemDataRole.UserRole, ("category", cat))
            self._tree.addTopLevelItem(cat_item)
            for model in get_category_models(self._data, cat):
                model_item = QTreeWidgetItem([model])
                model_item.setData(0, Qt.ItemDataRole.UserRole, ("model", cat, model))
                cat_item.addChild(model_item)
            cat_item.setExpanded(True)

    def _on_tree_selection(
        self,
        current: Optional[QTreeWidgetItem],
        previous: Optional[QTreeWidgetItem],
    ) -> None:
        if current is None:
            self._current_category = None
            self._current_model = None
            self._set_form_enabled(False)
            self._delete_btn.setEnabled(False)
            return

        data = current.data(0, Qt.ItemDataRole.UserRole)
        if data is None:
            return

        if data[0] == "model":
            _, category, model = data
            self._current_category = category
            self._current_model = model
            preset = get_preset(self._data, category, model)
            if preset:
                self._load_form(preset)
            self._set_form_enabled(True)
            self._delete_btn.setEnabled(is_user_preset(category))
        else:
            self._current_category = data[1]
            self._current_model = None
            self._set_form_enabled(False)
            self._delete_btn.setEnabled(False)

    # ------------------------------------------------------------------
    # Form helpers
    # ------------------------------------------------------------------

    def _load_form(self, preset: dict) -> None:
        idx = self._edit_type.findData(preset.get("pad_type", 0))
        if idx >= 0:
            self._edit_type.setCurrentIndex(idx)
        self._edit_thresh.setValue(preset.get("threshold", 0))
        self._edit_sens.setValue(preset.get("head_sensitivity", 0))
        self._edit_scan.setValue(preset.get("scan_time", 0))
        self._edit_mask.setValue(preset.get("mask_time", 0))
        self._edit_rim_thresh.setValue(preset.get("rim_threshold", 0))
        self._edit_rim_sens.setValue(preset.get("rim_sensitivity", 0))

    def _read_form(self) -> dict:
        return {
            "pad_type":         self._edit_type.currentData(),
            "threshold":        self._edit_thresh.value(),
            "head_sensitivity": self._edit_sens.value(),
            "scan_time":        self._edit_scan.value(),
            "mask_time":        self._edit_mask.value(),
            "rim_threshold":    self._edit_rim_thresh.value(),
            "rim_sensitivity":  self._edit_rim_sens.value(),
        }

    def _set_form_enabled(self, enabled: bool) -> None:
        for widget in (
            self._edit_type,
            self._edit_thresh, self._edit_sens,
            self._edit_scan,   self._edit_mask,
            self._edit_rim_thresh, self._edit_rim_sens,
        ):
            widget.setEnabled(enabled)
        self._save_btn.setEnabled(enabled)

    # ------------------------------------------------------------------
    # Button handlers
    # ------------------------------------------------------------------

    def _on_save(self) -> None:
        if self._current_category is None or self._current_model is None:
            return
        self._data.setdefault(self._current_category, {})[self._current_model] = self._read_form()
        save_presets(self._data)

    def _on_delete(self) -> None:
        if not is_user_preset(self._current_category or "") or self._current_model is None:
            return
        delete_user_preset(self._current_model)
        self._data = load_presets()
        self._current_model = None
        self._populate_tree()
        self._set_form_enabled(False)
        self._delete_btn.setEnabled(False)

    def _on_new(self) -> None:
        category = self._current_category or "My Presets"
        name, ok = QInputDialog.getText(self, "New Preset", "Enter preset name:")
        if not ok or not name.strip():
            return
        name = name.strip()
        defaults: dict = {
            "pad_type":         PAD_TYPE_PIEZO,
            "threshold":        100,
            "head_sensitivity": 800,
            "scan_time":        10,
            "mask_time":        30,
            "rim_threshold":    0,
            "rim_sensitivity":  0,
        }
        self._data.setdefault(category, {})[name] = defaults
        save_presets(self._data)
        self._populate_tree()

        # Select the new item in the tree
        for i in range(self._tree.topLevelItemCount()):
            cat_item = self._tree.topLevelItem(i)
            cat_data = cat_item.data(0, Qt.ItemDataRole.UserRole)
            if cat_data and cat_data[1] == category:
                for j in range(cat_item.childCount()):
                    child = cat_item.child(j)
                    if child.text(0) == name:
                        self._tree.setCurrentItem(child)
                        return
