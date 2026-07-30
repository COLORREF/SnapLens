"""系统托盘图标与右键菜单。

注意：托盘通知功能已迁移至 snaplens.notify 模块，
TrayIcon 仅负责图标的显示、菜单交互和文本同步。
"""
from PySide6.QtCore import Qt, Signal
from PySide6.QtGui import QColor, QIcon, QPainter, QPixmap
from PySide6.QtWidgets import QMenu, QSystemTrayIcon


def _make_icon() -> QIcon:
    """程序化生成托盘图标：蓝色圆角底 + 白色"截"字，无需外部资源文件。"""
    pixmap = QPixmap(64, 64)
    pixmap.fill(Qt.GlobalColor.transparent)
    painter = QPainter(pixmap)
    painter.setRenderHint(QPainter.RenderHint.Antialiasing)
    painter.setBrush(QColor(37, 99, 235))
    painter.setPen(Qt.PenStyle.NoPen)
    painter.drawRoundedRect(2, 2, 60, 60, 14, 14)
    painter.setPen(QColor("white"))
    font = painter.font()
    font.setPixelSize(34)
    font.setBold(True)
    painter.setFont(font)
    painter.drawText(pixmap.rect(), Qt.AlignmentFlag.AlignCenter, "截")
    painter.end()
    return QIcon(pixmap)


class TrayIcon(QSystemTrayIcon):
    """系统托盘图标：菜单 + 快捷键/模式同步。

    通知功能由 NotifyManager 统一接管，通过 set_tray() 绑定。
    """

    snipRequested = Signal()
    settingsRequested = Signal()
    quitRequested = Signal()
    modeSwitchRequested = Signal()   # 用户通过托盘菜单切换应用模式
    translateRequested = Signal()    # 用户通过托盘菜单打开文本翻译窗口

    def __init__(self, parent=None):
        super().__init__(_make_icon(), parent)
        self.setToolTip("SnapLens 截图工具")

        menu = QMenu()
        self._snip_action = menu.addAction("截图")
        self._snip_action.triggered.connect(self.snipRequested)

        menu.addSeparator()

        self._translate_action = menu.addAction("文本翻译")
        self._translate_action.triggered.connect(self.translateRequested)

        self._mode_action = menu.addAction("")
        self._mode_action.triggered.connect(self.modeSwitchRequested)

        menu.addSeparator()
        settings_action = menu.addAction("设置…")
        settings_action.triggered.connect(self.settingsRequested)
        quit_action = menu.addAction("退出")
        quit_action.triggered.connect(self.quitRequested)
        self.setContextMenu(menu)
        self._menu = menu  # 保持引用，防止被回收

    def set_hotkey_text(self, text: str):
        """在菜单项上同步显示当前快捷键。"""
        self._snip_action.setText(f"截图 ({text})")

    def set_mode_text(self, mode: str):
        """更新模式切换菜单项的文字。

        mode: "translate" | "screenshot"
        """
        if mode == "translate":
            self._mode_action.setText("切换至截图模式")
        else:
            self._mode_action.setText("切换至翻译模式")
