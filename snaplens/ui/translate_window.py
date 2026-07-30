"""AI 翻译结果窗口。

布局：
  顶部工具栏：语言选择 | 重新翻译 | 复制翻译结果 | 复制 OCR 原文
  左侧：可缩放/拖动的截图预览（ZoomableImageView）
  右侧：标签页切换 —— 翻译文本 | OCR 原文 | AI 思考
"""
import os
import tempfile

from PySide6.QtCore import Qt, Signal
from PySide6.QtGui import QGuiApplication, QPixmap
from PySide6.QtWidgets import (QComboBox, QHBoxLayout, QLabel,
                               QPlainTextEdit, QPushButton, QSplitter,
                               QTabWidget, QVBoxLayout, QWidget)

from ..core.settings import Settings
from ..core.temp_cleanup import cleanup_temp_dir
from .translate_service import TranslateService
from .zoomable_image import ZoomableImageView

# 预设翻译目标语言选项
_PRESET_LANGUAGES = [
    "简体中文",
    "繁体中文（台湾）",
    "繁体中文（香港）",
    "英语",
    "日语",
    "韩语",
    "法语",
    "德语",
    "西班牙语",
    "葡萄牙语",
    "俄语",
    "阿拉伯语",
    "泰语",
    "越南语",
]


class TranslateWindow(QWidget):
    """翻译结果查看窗口。

    左侧：可缩放/拖动的截图预览
    右侧标签页：
      - 翻译文本（只读 + 一键复制）
      - OCR 原文（方便对比检查 OCR 准确性）
      - AI 思考（如果模型启用了思考模式）
    """

    closed = Signal()

    def __init__(self, pixmap: QPixmap, settings: Settings,
                 auto_translate: bool = True,
                 notify_manager=None, parent=None):
        super().__init__(parent)
        self._settings = settings
        self._pixmap = pixmap
        self._current_lang = settings.ai_target_lang
        self._auto_translate = auto_translate
        self._service: TranslateService | None = None
        self._notify_manager = notify_manager
        self._tmp_path: str | None = None   # 临时图片路径，翻译开始时创建

        self.setWindowTitle("AI 翻译")
        self.resize(1100, 640)
        self.setMinimumSize(800, 480)
        self.setWindowFlags(
            Qt.WindowType.Window
            | Qt.WindowType.WindowStaysOnTopHint
        )
        self.setAttribute(Qt.WidgetAttribute.WA_DeleteOnClose)

        self._build_ui()
        if self._auto_translate:
            self._start_translation()
        else:
            # 确认模式：等待用户选择语言后手动开始
            self._translated_edit.setPlaceholderText(
                "请选择目标语言后点击「开始翻译」"
            )
            self._retranslate_btn.setText("开始翻译")
            self._retranslate_btn.setEnabled(True)
            self._status_label.setText("就绪 - 请选择翻译目标语言")

    # ---------------------------------------------------------------- UI 构建
    def _build_ui(self):
        root = QVBoxLayout(self)
        root.setContentsMargins(12, 12, 12, 12)
        root.setSpacing(10)

        # ---- 顶部工具栏 ----
        toolbar = QHBoxLayout()
        toolbar.setSpacing(8)

        toolbar.addWidget(QLabel("翻译目标语言："))

        self._lang_combo = QComboBox()
        self._lang_combo.setEditable(True)
        self._lang_combo.addItems(_PRESET_LANGUAGES)
        idx = self._lang_combo.findText(self._current_lang)
        if idx >= 0:
            self._lang_combo.setCurrentIndex(idx)
        else:
            self._lang_combo.setCurrentText(self._current_lang)
        self._lang_combo.currentTextChanged.connect(self._on_lang_changed)
        self._lang_combo.setMinimumWidth(180)
        toolbar.addWidget(self._lang_combo)

        self._retranslate_btn = QPushButton("重新翻译")
        self._retranslate_btn.setEnabled(False)
        self._retranslate_btn.clicked.connect(self._retranslate)
        toolbar.addWidget(self._retranslate_btn)

        toolbar.addStretch()

        self._copy_btn = QPushButton("复制翻译结果")
        self._copy_btn.setEnabled(False)
        self._copy_btn.clicked.connect(self._copy_result)
        toolbar.addWidget(self._copy_btn)

        self._copy_ocr_btn = QPushButton("复制 OCR 原文")
        self._copy_ocr_btn.setEnabled(False)
        self._copy_ocr_btn.clicked.connect(self._copy_ocr)
        toolbar.addWidget(self._copy_ocr_btn)

        root.addLayout(toolbar)

        # ---- 主体：左右分栏 ----
        splitter = QSplitter(Qt.Orientation.Horizontal)

        # 左侧：可缩放图片（传入原始全分辨率，QGraphicsView 自行处理显示缩放）
        self._image_view = ZoomableImageView()
        self._image_view.set_pixmap(self._pixmap)
        splitter.addWidget(self._image_view)

        # 右侧：标签页
        right_panel = QWidget()
        right_layout = QVBoxLayout(right_panel)
        right_layout.setContentsMargins(0, 0, 0, 0)
        right_layout.setSpacing(0)

        self._tabs = QTabWidget()

        # Tab 1: 翻译文本
        self._translated_edit = self._make_text_panel("正在翻译中，请稍候...")
        self._tabs.addTab(self._translated_edit, "翻 译")

        # Tab 2: OCR 原文
        self._ocr_edit = self._make_text_panel("等待 OCR 提取结果...")
        self._tabs.addTab(self._ocr_edit, "OCR 原文")

        # Tab 3: AI 思考
        self._thinking_edit = self._make_text_panel("（未启用思考模式）")
        self._tabs.addTab(self._thinking_edit, "AI 思考")

        # OCR 和思考标签页默认隐藏，收到数据后再显示
        # （始终保留标签页，但初始为空内容）

        right_layout.addWidget(self._tabs)
        splitter.addWidget(right_panel)

        splitter.setSizes([500, 560])
        root.addWidget(splitter)

        # ---- 状态栏 ----
        self._status_label = QLabel("正在翻译...")
        root.addWidget(self._status_label)

    def _make_text_panel(self, placeholder: str) -> QPlainTextEdit:
        """创建统一的只读文本面板。"""
        edit = QPlainTextEdit()
        edit.setReadOnly(True)
        edit.setPlaceholderText(placeholder)
        return edit

    # ---------------------------------------------------------------- 翻译逻辑
    def _start_translation(self):
        """启动后台翻译线程。"""
        self._set_loading(True)

        # 确保临时目录存在
        tmp_dir = self._settings.temp_dir
        os.makedirs(tmp_dir, exist_ok=True)

        tmp = tempfile.NamedTemporaryFile(suffix=".png", delete=False, dir=tmp_dir)
        tmp.close()
        self._tmp_path = tmp.name
        self._pixmap.save(self._tmp_path, "PNG")

        self._launch_service(self._current_lang)

    def _launch_service(self, target_lang: str):
        """启动 TranslateService 执行翻译。"""
        self._current_lang = target_lang
        self._set_loading(True)   # 锁定界面，防止并发触发

        if self._service is not None:
            # 断开旧连接，让旧线程自然结束再自动清理
            # 注意：旧 service 可能已被 deleteLater 清理（C++ 对象已删除但
            # Python 包装器尚存），此时访问信号会抛 RuntimeError
            try:
                self._service.translated.disconnect()
                self._service.ocr_text.disconnect()
                self._service.thinking.disconnect()
                self._service.error.disconnect()
                self._service.finished.connect(self._service.deleteLater)
            except RuntimeError:
                pass  # 已被 deleteLater 清理，无需额外操作

        self._service = TranslateService(
            image_path=self._tmp_path,
            target_lang=target_lang,
            settings=self._settings,
        )
        self._service.translated.connect(self._on_translated)
        self._service.ocr_text.connect(self._on_ocr_text)
        self._service.thinking.connect(self._on_thinking)
        self._service.error.connect(self._on_error)
        self._service.finished.connect(self._service.deleteLater)  # 完成后自动清理
        self._service.start()

    def _on_translated(self, text: str):
        self._set_loading(False)
        self._translated_edit.setPlainText(text)
        self._copy_btn.setEnabled(bool(text))
        self._retranslate_btn.setEnabled(True)
        self._status_label.setText(
            f"翻译完成 - 目标语言：{self._current_lang}"
        )
        # 翻译成功托盘通知
        if self._notify_manager is not None:
            self._notify_manager.notify(
                "translate_success", "AI 翻译",
                f"翻译完成（{self._current_lang}）",
            )

    def _on_ocr_text(self, text: str):
        self._ocr_edit.setPlainText(text)
        self._copy_ocr_btn.setEnabled(bool(text))

    def _on_thinking(self, text: str):
        self._thinking_edit.setPlainText(text)

    def _on_error(self, msg: str):
        self._set_loading(False)
        self._translated_edit.setPlainText(f"[翻译失败]\n{msg}")
        self._copy_btn.setEnabled(False)
        self._retranslate_btn.setEnabled(True)
        self._status_label.setText("翻译失败")
        # 翻译失败走通知系统（由 notify_translate_fail 设置控制）
        if self._notify_manager is not None:
            self._notify_manager.notify(
                "translate_fail", "AI 翻译失败", msg,
            )

    def _retranslate(self):
        new_lang = self._lang_combo.currentText().strip()
        if not new_lang:
            return
        # 首次点击"开始翻译"时 _tmp_path 尚未创建
        if self._tmp_path is None:
            self._current_lang = new_lang
            self._start_translation()
            return
        self._launch_service(new_lang)

    def _on_lang_changed(self, _text: str):
        new_lang = self._lang_combo.currentText().strip()
        if new_lang and new_lang != self._current_lang:
            self._retranslate_btn.setEnabled(True)
            self._retranslate_btn.setText(f"翻译为「{new_lang}」")
            # 自动模式下切换语言后立即重译；确认模式下等待手动点击
            if self._auto_translate:
                self._retranslate()

    def _copy_result(self):
        """复制当前活动的标签页内容。"""
        current = self._tabs.currentWidget()
        if isinstance(current, QPlainTextEdit):
            text = current.toPlainText()
            if text and not text.startswith("[翻译失败]"):
                QGuiApplication.clipboard().setText(text)
                self._status_label.setText("已复制到剪贴板")

    def _copy_ocr(self):
        """复制 OCR 原文。"""
        text = self._ocr_edit.toPlainText()
        if text:
            QGuiApplication.clipboard().setText(text)
            self._status_label.setText("已复制 OCR 原文")

    def _set_loading(self, loading: bool):
        self._lang_combo.setEnabled(not loading)  # 翻译中锁定语言切换，防止并发重译
        if loading:
            self._translated_edit.setPlainText("")
            self._translated_edit.setPlaceholderText("正在翻译中，请稍候...")
            self._ocr_edit.setPlainText("")
            self._ocr_edit.setPlaceholderText("等待 OCR 提取结果...")
            self._thinking_edit.setPlainText("")
            self._thinking_edit.setPlaceholderText("（未启用思考模式）")
            self._retranslate_btn.setEnabled(False)
            self._copy_btn.setEnabled(False)
            self._copy_ocr_btn.setEnabled(False)
            self._status_label.setText("正在翻译...")

    # ---------------------------------------------------------------- 清理
    def closeEvent(self, event):
        # 翻译进行中关闭窗口：请求中断 API 调用，断开 UI 信号，线程移交给 QApplication
        if self._service is not None:
            try:
                if self._service.isRunning():
                    self._service.requestInterruption()  # 通知流式 API 循环终止
                    self._service.translated.disconnect()
                    self._service.ocr_text.disconnect()
                    self._service.thinking.disconnect()
                    self._service.error.disconnect()
                    # 线程依附到 QApplication，防止窗口销毁时被连带 GC
                    from PySide6.QtWidgets import QApplication
                    self._service.setParent(QApplication.instance())
                    # finished → deleteLater 已在 _launch_service 中连接
            except RuntimeError:
                # C++ 对象已被 deleteLater 清理，Python 包装器尚存但无效
                pass

        # 仅在用户启用清理策略时才清理临时文件
        if self._settings.cleanup_on_window_close:
            try:
                if hasattr(self, "_tmp_path") and os.path.exists(self._tmp_path):
                    os.unlink(self._tmp_path)
            except OSError:
                pass
            cleanup_temp_dir(self._settings.temp_dir)
        self.closed.emit()
        super().closeEvent(event)
