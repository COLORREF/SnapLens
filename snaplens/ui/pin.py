"""钉图窗口：把截图以无边框置顶窗口的形式“钉”在桌面上。

交互：
- 左键拖动移动位置；
- 滚轮缩放（保持比例）；
- 右键菜单：复制图片 / 另存为 / 关闭；
- Esc 或右上角 “×” 关闭。
"""
import os
from datetime import datetime

from PySide6.QtCore import QPoint, QSizeF, Qt
from PySide6.QtGui import QGuiApplication, QPainter, QPixmap
from PySide6.QtWidgets import QFileDialog, QMenu, QToolButton, QWidget


class PinWindow(QWidget):
    def __init__(self, pixmap: QPixmap, save_dir: str, parent=None):
        super().__init__(
            parent,
            Qt.WindowType.FramelessWindowHint
            | Qt.WindowType.WindowStaysOnTopHint
            | Qt.WindowType.Window,
        )
        self.setAttribute(Qt.WidgetAttribute.WA_DeleteOnClose)
        self._pixmap = pixmap
        self._save_dir = save_dir
        # 以图像自身 DPR 换算出的逻辑尺寸为基准大小
        dpr = pixmap.devicePixelRatio()
        self._base_size = QSizeF(pixmap.width() / dpr, pixmap.height() / dpr)
        self._scale = 1.0
        self.resize(self._base_size.toSize())

        # 右上角常驻关闭按钮
        self._close_button = QToolButton(self)
        self._close_button.setText("×")
        self._close_button.setFixedSize(22, 22)
        self._close_button.setCursor(Qt.CursorShape.ArrowCursor)
        self._close_button.clicked.connect(self.close)

        self._drag_offset: QPoint | None = None

        # 初始位置：主屏中央
        screen = QGuiApplication.primaryScreen()
        if screen is not None:
            geo = screen.availableGeometry()
            self.move(geo.center() - self.rect().center())

    # ------------------------------------------------------------ 绘制与缩放
    def paintEvent(self, _event):
        painter = QPainter(self)
        painter.setRenderHint(QPainter.RenderHint.SmoothPixmapTransform)
        painter.drawPixmap(self.rect(), self._pixmap,
                           self._pixmap.rect())
        painter.end()

    def resizeEvent(self, _event):
        self._close_button.move(self.width() - 26, 4)

    def wheelEvent(self, event):
        factor = 1.2 if event.angleDelta().y() > 0 else 1 / 1.2
        new_scale = min(10.0, max(0.05, self._scale * factor))
        if new_scale == self._scale:
            return
        # 缩放时保持窗口中心位置不动
        center = self.geometry().center()
        self._scale = new_scale
        self.resize((self._base_size * self._scale).toSize())
        self.move(center - self.rect().center())

    # ------------------------------------------------------------ 拖动移动
    def mousePressEvent(self, event):
        if event.button() == Qt.MouseButton.LeftButton:
            self._drag_offset = (event.globalPosition().toPoint()
                                 - self.frameGeometry().topLeft())

    def mouseMoveEvent(self, event):
        if self._drag_offset is not None:
            self.move(event.globalPosition().toPoint() - self._drag_offset)

    def mouseReleaseEvent(self, event):
        if event.button() == Qt.MouseButton.LeftButton:
            self._drag_offset = None

    # ------------------------------------------------------------ 菜单与关闭
    def contextMenuEvent(self, event):
        menu = QMenu(self)
        copy_action = menu.addAction("复制图片")
        save_action = menu.addAction("另存为…")
        menu.addSeparator()
        close_action = menu.addAction("关闭")
        chosen = menu.exec(event.globalPos())
        if chosen is copy_action:
            QGuiApplication.clipboard().setPixmap(self._pixmap)
        elif chosen is save_action:
            self._save_as()
        elif chosen is close_action:
            self.close()

    def keyPressEvent(self, event):
        if event.key() == Qt.Key.Key_Escape:
            self.close()

    def _save_as(self):
        name = datetime.now().strftime("SnapLens_%Y%m%d_%H%M%S") + ".png"
        default_path = os.path.join(self._save_dir, name)
        path, _selected = QFileDialog.getSaveFileName(
            self, "另存为", default_path,
            "PNG 图片 (*.png);;JPEG 图片 (*.jpg)",
        )
        if path:
            self._pixmap.save(path)
