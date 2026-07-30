"""通用颜色选择器组件。

提供 ColorPickerButton — 一个可点击的颜色预览按钮，
点击后弹出系统 QColorDialog 进行任意颜色选取。

用于替代设置对话框中固定预设颜色的下拉框。
"""
from PySide6.QtCore import Qt, Signal
from PySide6.QtGui import QColor
from PySide6.QtWidgets import QColorDialog, QPushButton


class ColorPickerButton(QPushButton):
    """颜色选择按钮。

    显示当前颜色的方块预览 + 十六进制色值文本，
    点击弹出 QColorDialog 选取任意颜色。

    用法：
        picker = ColorPickerButton("#DC1414")
        picker.color_changed.connect(on_color_changed)
        hex_value = picker.color()  # → "#DC1414"
    """

    color_changed = Signal(str)  # 选中新颜色时发出 hex 字符串

    def __init__(self, hex_color: str = "#FFFFFF", parent=None):
        super().__init__(parent)
        self._hex = hex_color
        self.clicked.connect(self._pick)
        self._update_style()

    # ---------------------------------------------------------------- 公开接口
    def color(self) -> str:
        """返回当前颜色的十六进制字符串，如 "#DC1414"。"""
        return self._hex

    def set_color(self, hex_color: str) -> None:
        """程序化设置颜色并刷新显示。"""
        self._hex = hex_color
        self._update_style()

    # ---------------------------------------------------------------- 内部
    def _pick(self):
        current = QColor(self._hex)
        if not current.isValid():
            current = QColor("#FFFFFF")
        color = QColorDialog.getColor(current, self, "选择颜色")
        if color.isValid():
            self._hex = color.name()
            self._update_style()
            self.color_changed.emit(self._hex)

    def _update_style(self):
        color = QColor(self._hex)
        if not color.isValid():
            color = QColor("#FFFFFF")
            self._hex = "#FFFFFF"
        # 根据亮度选择文字颜色（浅色背景用深色文字，深色背景用浅色文字）
        luminance = (0.299 * color.red() + 0.587 * color.green()
                     + 0.114 * color.blue()) / 255.0
        text_color = "#1e1e1e" if luminance > 0.5 else "#f0f0f0"
        self.setText(self._hex.upper())
        self.setStyleSheet(
            f"QPushButton {{"
            f"  background-color: {self._hex};"
            f"  color: {text_color};"
            f"}}"
        )
        self.setCursor(Qt.CursorShape.PointingHandCursor)
