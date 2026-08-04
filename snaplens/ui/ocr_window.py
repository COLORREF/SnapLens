"""OCR 识别结果窗口。

布局：
  顶部工具栏：复制 OCR 原文
  左侧：可缩放/拖动的截图预览
  右侧：OCR 识别的文字内容
"""
from PySide6.QtCore import Qt, Signal
from PySide6.QtGui import QGuiApplication, QPixmap
from PySide6.QtWidgets import (QHBoxLayout, QLabel,
                               QPlainTextEdit, QPushButton, QSplitter,
                               QVBoxLayout, QWidget)

from ..core.settings import Settings
from .ocr_service import OcrService
from .zoomable_image import ZoomableImageView


class OcrWindow(QWidget):
    """OCR 识别结果窗口。

    左侧：可缩放的截图预览
    右侧：OCR 提取的文字（只读 + 一键复制）
    """

    closed = Signal()

    def __init__(self, pixmap: QPixmap, settings: Settings,
                 notify_manager=None, parent=None):
        super().__init__(parent)
        self._settings = settings
        self._pixmap = pixmap
        self._service: OcrService | None = None
        self._notify_manager = notify_manager

        self.setWindowTitle("OCR 识别")
        self.resize(900, 560)
        self.setMinimumSize(640, 400)
        self.setWindowFlags(
            Qt.WindowType.Window
            | Qt.WindowType.WindowStaysOnTopHint
        )
        self.setAttribute(Qt.WidgetAttribute.WA_DeleteOnClose)

        self._build_ui()
        self._start_ocr()

    def _build_ui(self):
        root = QVBoxLayout(self)
        root.setContentsMargins(12, 12, 12, 12)
        root.setSpacing(10)

        # ---- 顶部工具栏 ----
        toolbar = QHBoxLayout()
        toolbar.setSpacing(8)
        toolbar.addStretch()

        self._copy_btn = QPushButton("复制 OCR 原文")
        self._copy_btn.setEnabled(False)
        self._copy_btn.clicked.connect(self._copy_result)
        toolbar.addWidget(self._copy_btn)

        root.addLayout(toolbar)

        # ---- 主体：左右分栏 ----
        splitter = QSplitter(Qt.Orientation.Horizontal)

        # 左侧：可缩放图片
        self._image_view = ZoomableImageView()
        self._image_view.set_pixmap(self._pixmap)
        splitter.addWidget(self._image_view)

        # 右侧：OCR 文本
        right_panel = QWidget()
        right_layout = QVBoxLayout(right_panel)
        right_layout.setContentsMargins(0, 0, 0, 0)

        self._text_edit = QPlainTextEdit()
        self._text_edit.setReadOnly(True)
        self._text_edit.setPlaceholderText("正在识别中，请稍候...")
        right_layout.addWidget(self._text_edit)
        splitter.addWidget(right_panel)

        splitter.setSizes([400, 460])
        root.addWidget(splitter)

        # ---- 状态栏 ----
        self._status_label = QLabel("正在识别...")
        root.addWidget(self._status_label)

    def _start_ocr(self):
        self._service = OcrService(
            pixmap=self._pixmap,
            ocr_langs=self._settings.ai_ocr_langs,
        )
        self._service.finished.connect(self._on_result)
        self._service.error.connect(self._on_error)
        self._service.start()

    def _on_result(self, text: str):
        self._text_edit.setPlainText(text)
        self._copy_btn.setEnabled(bool(text))
        self._status_label.setText("OCR 识别完成")

    def _on_error(self, msg: str):
        self._text_edit.setPlainText(f"[识别失败]\n{msg}")
        self._status_label.setText("识别失败")
        # OCR 失败走通知系统（由 notify_ocr_fail 设置控制）
        if self._notify_manager is not None:
            self._notify_manager.notify(
                "ocr_fail", "OCR 识别失败", msg,
            )

    def _copy_result(self):
        text = self._text_edit.toPlainText()
        if text and not text.startswith("[识别失败]"):
            QGuiApplication.clipboard().setText(text)
            self._status_label.setText("已复制到剪贴板")

    def closeEvent(self, event):
        # OCR 进行中关闭窗口：断开 UI 信号，线程移交给 QApplication 防止连带 GC 崩溃
        if self._service is not None and self._service.isRunning():
            try:
                self._service.finished.disconnect()
                self._service.error.disconnect()
            except RuntimeError:
                pass  # 已被清理
            from PySide6.QtWidgets import QApplication
            self._service.setParent(QApplication.instance())

        self.closed.emit()
        super().closeEvent(event)
