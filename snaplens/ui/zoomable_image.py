"""可缩放图片查看器。

对标 Windows 原生看图体验：滚轮缩放（以光标为中心），按住拖动平移。
"""
from PySide6.QtCore import Qt, QRectF
from PySide6.QtGui import QPainter, QPixmap
from PySide6.QtWidgets import QGraphicsPixmapItem, QGraphicsScene, QGraphicsView

# 缩放范围
_ZOOM_MIN = 0.1
_ZOOM_MAX = 10.0
_ZOOM_WHEEL_STEP = 1.08  # 滚轮缩放因子（细腻控制）


class ZoomableImageView(QGraphicsView):
    """可缩放、可拖动平移的图片查看器。

    滚轮：以光标位置为中心缩放
    左键拖动：平移视图
    """

    def __init__(self, parent=None):
        super().__init__(parent)
        self._scene = QGraphicsScene(self)
        self.setScene(self._scene)
        self._pixmap_item: QGraphicsPixmapItem | None = None
        self._zoom_level = 1.0

        self.setRenderHints(
            QPainter.RenderHint.Antialiasing
            | QPainter.RenderHint.SmoothPixmapTransform
        )
        self.setHorizontalScrollBarPolicy(Qt.ScrollBarPolicy.ScrollBarAsNeeded)
        self.setVerticalScrollBarPolicy(Qt.ScrollBarPolicy.ScrollBarAsNeeded)
        self.setDragMode(QGraphicsView.DragMode.ScrollHandDrag)
        self.setTransformationAnchor(
            QGraphicsView.ViewportAnchor.AnchorUnderMouse
        )
        self.setResizeAnchor(QGraphicsView.ViewportAnchor.AnchorUnderMouse)

    def set_pixmap(self, pixmap: QPixmap):
        """设置要显示的图片。"""
        self._scene.clear()
        self._pixmap_item = QGraphicsPixmapItem(pixmap)
        self._scene.addItem(self._pixmap_item)
        self._scene.setSceneRect(QRectF(pixmap.rect()))
        self._zoom_level = 1.0
        self.resetTransform()
        self.fitInView(
            self._scene.sceneRect(), Qt.AspectRatioMode.KeepAspectRatio
        )

    def wheelEvent(self, event):
        """滚轮缩放（以光标位置为中心）。"""
        if self._pixmap_item is None:
            return
        delta = event.angleDelta().y()
        if delta > 0 and self._zoom_level < _ZOOM_MAX:
            factor = _ZOOM_WHEEL_STEP
            self._zoom_level *= factor
        elif delta < 0 and self._zoom_level > _ZOOM_MIN:
            factor = 1.0 / _ZOOM_WHEEL_STEP
            self._zoom_level /= _ZOOM_WHEEL_STEP
        else:
            return
        self.scale(factor, factor)

    def fit_to_window(self):
        """缩放到适应窗口。"""
        if self._pixmap_item is not None:
            self._zoom_level = 1.0
            self.resetTransform()
            self.fitInView(
                self._scene.sceneRect(), Qt.AspectRatioMode.KeepAspectRatio
            )
