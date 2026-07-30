"""翻译主窗口：原文 + 译文/AI思考，支持场景和目标语言切换。"""
from PySide6.QtCore import Qt
from PySide6.QtWidgets import (
    QApplication, QCheckBox, QComboBox, QDialog,
    QHBoxLayout, QLabel, QMainWindow, QPlainTextEdit, QPushButton,
    QSplitter, QTabWidget, QVBoxLayout, QWidget,
)

from ..core.text_translator import TextTranslateService

TRANSLATION_SCENARIOS = [
    "通用场景",
    "计算机 / IT",
    "医学",
    "金融经济",
    "法律",
    "学术论文",
    "文学",
]

TARGET_LANGUAGES = [
    "简体中文", "繁体中文",
    "英语", "日语", "韩语",
    "法语", "德语", "西班牙语",
    "葡萄牙语", "俄语", "阿拉伯语",
    "泰语", "越南语", "意大利语",
    "荷兰语", "波兰语", "土耳其语",
]


class _CloseDialog(QDialog):
    """关闭窗口时的选择对话框。"""

    def __init__(self, parent=None):
        super().__init__(parent)
        self.setWindowTitle("SnapLens")
        self.setMinimumWidth(320)

        layout = QVBoxLayout(self)
        layout.addWidget(QLabel("关闭主窗口后："))

        self._btn_tray = QPushButton("后台运行（最小化到系统托盘）")
        self._btn_tray.clicked.connect(lambda: self._done(True))
        layout.addWidget(self._btn_tray)

        self._btn_exit = QPushButton("退出程序")
        self._btn_exit.clicked.connect(lambda: self._done(False))
        layout.addWidget(self._btn_exit)

        self._remember = QCheckBox("记住我的选择，下次不再询问")
        layout.addWidget(self._remember)

        self._result = None

    def _done(self, to_tray: bool):
        self._result = to_tray
        self.accept()

    @staticmethod
    def ask(parent) -> tuple[bool | None, bool]:
        dlg = _CloseDialog(parent)
        dlg.exec()
        if dlg._result is None:
            return (None, False)
        return (dlg._result, dlg._remember.isChecked())


class _SplitTabPanel(QWidget):
    """可合并/分离的双面板：两个 QTabWidget 放在 QSplitter 中。

    双击标签页切换合并/分离，拖拽调整比例但不可折叠到零。
    合并时 B 的编辑框并入 A 的标签页，B 的面板隐藏。
    """

    def __init__(self, label_a: str, label_b: str,
                 editor_a: QPlainTextEdit, editor_b: QPlainTextEdit,
                 default_merged: bool = False,
                 parent=None):
        super().__init__(parent)
        self.label_a = label_a
        self.label_b = label_b
        self.editor_a = editor_a
        self.editor_b = editor_b
        self._docked = default_merged   # B 是否在 A 的标签页中
        self._reverse = False           # A 是否在 B 的标签页中
        self._saved_ratio = 0.7         # 分离时 A 占比（0.0~1.0）

        layout = QVBoxLayout(self)
        layout.setContentsMargins(0, 0, 0, 0)
        self.splitter = QSplitter(Qt.Orientation.Vertical)
        layout.addWidget(self.splitter)

        self.tab_a = QTabWidget()
        self.tab_a.addTab(editor_a, label_a)
        self.splitter.addWidget(self.tab_a)

        self.tab_b = QTabWidget()
        self.tab_b.addTab(editor_b, label_b)
        self.splitter.addWidget(self.tab_b)

        self.splitter.setCollapsible(0, False)
        self.splitter.setCollapsible(1, False)
        self.splitter.splitterMoved.connect(self._on_moved)

        self.tab_a.tabBar().tabBarDoubleClicked.connect(
            lambda _idx: self._on_double_clicked(to_a=True)
        )
        self.tab_b.tabBar().tabBarDoubleClicked.connect(
            lambda _idx: self._on_double_clicked(to_a=False)
        )

        if default_merged:
            self._merge_b_into_a()
            self.splitter.setSizes([1, 0])
        else:
            total = sum(self.splitter.sizes()) or 570
            size_a = int(total * self._saved_ratio)
            self.splitter.setSizes([size_a, total - size_a])

    def _on_moved(self, _pos, _index):
        sizes = self.splitter.sizes()
        if len(sizes) >= 2 and sizes[0] > 0 and sizes[1] > 0:
            total = sizes[0] + sizes[1]
            self._saved_ratio = sizes[0] / total if total > 0 else 0.7

    def _on_double_clicked(self, to_a: bool):
        if self._docked or self._reverse:
            self._split()
        elif to_a:
            self._merge_b_into_a()
        else:
            self._merge_a_into_b()

    def _merge_b_into_a(self):
        self.tab_a.addTab(self.editor_b, self.label_b)
        self._docked = True
        self.tab_b.hide()

    def _merge_a_into_b(self):
        self.tab_b.insertTab(0, self.editor_a, self.label_a)
        self._reverse = True
        self.tab_a.hide()

    def _split(self):
        if self._reverse:
            idx = self.tab_b.indexOf(self.editor_a)
            if idx >= 0:
                self.tab_b.removeTab(idx)
            self.editor_a.setParent(self.tab_a)
            self.tab_a.addTab(self.editor_a, self.label_a)
            self._reverse = False
            self.tab_a.show()
        if self._docked:
            idx = self.tab_a.indexOf(self.editor_b)
            if idx >= 0:
                self.tab_a.removeTab(idx)
            self.editor_b.setParent(self.tab_b)
            self.tab_b.addTab(self.editor_b, self.label_b)
            self._docked = False
            self.tab_b.show()
        # 按比例计算实际像素
        total = sum(self.splitter.sizes()) or 570
        size_a = int(total * self._saved_ratio)
        self.splitter.setSizes([size_a, total - size_a])

    @property
    def is_merged(self) -> bool:
        return self._docked or self._reverse

    @property
    def ratio(self) -> float:
        return self._saved_ratio

    def apply_state(self, merged: bool, ratio: float):
        """外部恢复布局状态（启动时调用）。"""
        self._saved_ratio = max(0.1, min(0.9, ratio))
        if merged and not self.is_merged:
            self._merge_b_into_a()
            self.splitter.setSizes([1, 0])
        elif not merged and self.is_merged:
            self._split()
        else:
            # 已是目标状态，直接应用比例到视觉
            total = sum(self.splitter.sizes()) or 570
            size_a = int(total * self._saved_ratio)
            self.splitter.setSizes([size_a, total - size_a])


class MainWindow(QMainWindow):
    """SnapLens 翻译主窗口。"""

    def __init__(self, settings, notify_manager=None, app_mode="translate", parent=None):
        super().__init__(parent)
        self._settings = settings
        self._notify_manager = notify_manager
        self._app_mode = app_mode  # 当前应用模式，影响关闭行为

        self.setWindowTitle("SnapLens 翻译")
        self.resize(900, 600)
        self.setMinimumSize(640, 400)

        central = QWidget()
        self.setCentralWidget(central)
        root = QVBoxLayout(central)
        root.setContentsMargins(12, 12, 12, 12)

        # ---- 顶部栏 ----
        top_bar = QHBoxLayout()

        top_bar.addWidget(QLabel("场景："))
        self.scenario_combo = QComboBox()
        self.scenario_combo.addItems(TRANSLATION_SCENARIOS)
        self.scenario_combo.currentTextChanged.connect(self._refresh_prompt_preview)
        top_bar.addWidget(self.scenario_combo)

        top_bar.addWidget(QLabel("目标语言："))
        self.target_lang_combo = QComboBox()
        self.target_lang_combo.addItems(TARGET_LANGUAGES)
        eng_idx = self.target_lang_combo.findText("英语")
        if eng_idx >= 0:
            self.target_lang_combo.setCurrentIndex(eng_idx)
        self.target_lang_combo.setMinimumWidth(100)
        self.target_lang_combo.currentTextChanged.connect(self._refresh_prompt_preview)
        top_bar.addWidget(self.target_lang_combo)

        self._settings_btn = QPushButton("设置")
        self._settings_btn.setFixedWidth(60)
        top_bar.addWidget(self._settings_btn)

        top_bar.addStretch()

        self._layout_btn = QPushButton("上下布局")
        self._layout_btn.setFixedWidth(80)
        self._layout_btn.clicked.connect(self._toggle_layout)
        top_bar.addWidget(self._layout_btn)

        self._reset_layout_btn = QPushButton("还原布局")
        self._reset_layout_btn.setFixedWidth(80)
        self._reset_layout_btn.clicked.connect(self._reset_layout)
        top_bar.addWidget(self._reset_layout_btn)

        root.addLayout(top_bar)

        # ---- 翻译区域 ----
        self._splitter = QSplitter(Qt.Orientation.Horizontal)

        # 左侧：原文 + 提示词（默认合并）
        self.source_edit = QPlainTextEdit()
        self.source_edit.setPlaceholderText("在此输入或粘贴要翻译的文本...")
        self.source_edit.textChanged.connect(self._refresh_prompt_preview)
        self._prompt_edit = QPlainTextEdit()
        self._prompt_edit.setPlaceholderText("提示词模板...")

        self._left_panel = _SplitTabPanel(
            "原文", "提示词", self.source_edit, self._prompt_edit,
            default_merged=True,
        )
        self._splitter.addWidget(self._left_panel)

        # 右侧：译文 + AI 思考（默认分离）
        self.target_edit = QPlainTextEdit()
        self.target_edit.setReadOnly(True)
        self.target_edit.setPlaceholderText("翻译结果将显示在此处...")
        self._thinking_edit = QPlainTextEdit()
        self._thinking_edit.setReadOnly(True)
        self._thinking_edit.setPlaceholderText("AI思考过程将在此处显示...")

        self._right_panel = _SplitTabPanel(
            "译文", "AI 思考", self.target_edit, self._thinking_edit,
            default_merged=False,
        )
        self._splitter.addWidget(self._right_panel)

        # 从设置恢复面板合并/分离状态和分隔比例
        self._left_panel.apply_state(
            self._settings.left_panel_merged,
            self._settings.left_panel_ratio,
        )
        self._right_panel.apply_state(
            self._settings.right_panel_merged,
            self._settings.right_panel_ratio,
        )

        # 主分隔比例
        total = sum(self._splitter.sizes()) or 860
        r = self._settings.main_split_ratio
        self._splitter.setSizes([int(total * r), int(total * (1 - r))])
        root.addWidget(self._splitter)

        # 恢复布局（两侧分栏方向与整体相反）
        if self._settings.layout_orientation:
            self._splitter.setOrientation(Qt.Orientation.Horizontal)
            self._left_panel.splitter.setOrientation(Qt.Orientation.Vertical)
            self._right_panel.splitter.setOrientation(Qt.Orientation.Vertical)
            self._layout_btn.setText("上下布局")
        else:
            self._splitter.setOrientation(Qt.Orientation.Vertical)
            self._left_panel.splitter.setOrientation(Qt.Orientation.Horizontal)
            self._right_panel.splitter.setOrientation(Qt.Orientation.Horizontal)
            self._layout_btn.setText("左右布局")

        # ---- 底部操作栏 ----
        bottom_bar = QHBoxLayout()
        bottom_bar.addStretch()

        self.translate_btn = QPushButton("翻译")
        self.translate_btn.setMinimumWidth(100)
        self.translate_btn.clicked.connect(self._translate)
        bottom_bar.addWidget(self.translate_btn)

        root.addLayout(bottom_bar)

    # ---- 关闭行为 ----

    def closeEvent(self, event):
        self._save_layout_state()

        # 截图模式：关闭窗口 = 直接隐藏，不弹窗、不退出
        if self._app_mode == "screenshot":
            self.hide()
            event.ignore()
            return

        # 翻译模式：按 close_to_tray 设置决定行为
        to_tray = self._settings.close_to_tray
        if to_tray is None:
            result, remember = _CloseDialog.ask(self)
            if result is None:
                event.ignore()
                return
            to_tray = result
            if remember:
                self._settings.close_to_tray = to_tray
                self._settings.save()
        if to_tray:
            self.hide()
            event.ignore()
        else:
            QApplication.quit()

    def _save_layout_state(self):
        """保存面板合并/分离状态和分隔比例。"""
        s = self._settings
        s.left_panel_merged = self._left_panel.is_merged
        s.right_panel_merged = self._right_panel.is_merged
        s.left_panel_ratio = self._left_panel.ratio
        s.right_panel_ratio = self._right_panel.ratio
        # 主分隔比例
        sizes = self._splitter.sizes()
        total = sizes[0] + sizes[1]
        s.main_split_ratio = sizes[0] / total if total > 0 else 0.5
        s.save()

    def _reset_layout(self):
        """一键还原到默认布局（左侧合并、右侧分离、比例默认）。"""
        self._left_panel.apply_state(merged=True, ratio=0.7)
        self._right_panel.apply_state(merged=False, ratio=0.7)
        total = sum(self._splitter.sizes()) or 860
        self._splitter.setSizes([int(total * 0.5), int(total * 0.5)])
        self._settings.main_split_ratio = 0.5
        self._settings.left_panel_merged = True
        self._settings.right_panel_merged = False
        self._settings.left_panel_ratio = 0.7
        self._settings.right_panel_ratio = 0.7
        self._settings.save()

    # ---- 翻译 ----

    def _build_full_prompt(self) -> str:
        """根据当前模板和输入构建完整提示词。"""
        return self._settings.text_translation_prompt.format(
            target_lang=self.target_lang_combo.currentText(),
            scenario=self.scenario_combo.currentText(),
            source_text=self.source_edit.toPlainText().strip(),
        )

    def _refresh_prompt_preview(self):
        """实时更新提示词标签页内容。"""
        self._prompt_edit.setPlainText(self._build_full_prompt())

    def _translate(self):
        text = self.source_edit.toPlainText().strip()
        if not text:
            return
        s = self._settings
        if not s.ai_api_key:
            self.target_edit.setPlainText("[错误] API Key 未设置，请在设置中配置。")
            return

        self.translate_btn.setEnabled(False)
        self.translate_btn.setText("翻译中...")
        self.target_edit.setPlainText("")
        self._thinking_edit.setPlainText("")

        # 使用提示词标签页中的内容（可能已被用户临时修改）
        prompt = self._prompt_edit.toPlainText().strip()

        self._service = TextTranslateService(
            source_text=text,
            target_lang=self.target_lang_combo.currentText(),
            scenario=self.scenario_combo.currentText(),
            prompt_template=s.text_translation_prompt,
            full_prompt=prompt,
            api_key=s.ai_api_key,
            api_base=s.ai_api_base,
            model=s.ai_model,
            timeout=s.ai_timeout,
            temperature=s.ai_temperature,
            max_tokens=s.ai_max_tokens,
            top_p=s.ai_top_p,
            frequency_penalty=s.ai_frequency_penalty,
            presence_penalty=s.ai_presence_penalty,
            seed=s.ai_seed,
            stream_thinking=s.ai_stream_thinking,
        )
        self._service.translated.connect(self._on_translated)
        self._service.thinking.connect(self._on_thinking)
        self._service.error.connect(self._on_translate_error)
        self._service.finished.connect(self._service.deleteLater)
        self._service.start()

    def _on_translated(self, text: str):
        self.target_edit.setPlainText(text)
        self.translate_btn.setEnabled(True)
        self.translate_btn.setText("翻译")
        # 翻译成功托盘通知（与截图翻译路径保持一致）
        if self._notify_manager is not None:
            self._notify_manager.notify(
                "translate_success", "文本翻译", "翻译完成",
            )

    def _on_thinking(self, text: str):
        self._thinking_edit.setPlainText(text)

    def _on_translate_error(self, msg: str):
        self.target_edit.setPlainText(f"[错误] {msg}")
        self.translate_btn.setEnabled(True)
        self.translate_btn.setText("翻译")
        # 翻译失败走通知系统（由 notify_translate_fail 设置控制）
        if self._notify_manager is not None:
            self._notify_manager.notify(
                "translate_fail", "翻译失败", msg,
            )

    # ---- 布局 ----

    def _toggle_layout(self):
        """切换整体布局，右侧分栏取反方向以充分利用空间。"""
        if self._splitter.orientation() == Qt.Orientation.Horizontal:
            self._splitter.setOrientation(Qt.Orientation.Vertical)
            self._left_panel.splitter.setOrientation(Qt.Orientation.Horizontal)
            self._right_panel.splitter.setOrientation(Qt.Orientation.Horizontal)
            self._layout_btn.setText("左右布局")
            self._settings.layout_orientation = False
        else:
            self._splitter.setOrientation(Qt.Orientation.Horizontal)
            self._left_panel.splitter.setOrientation(Qt.Orientation.Vertical)
            self._right_panel.splitter.setOrientation(Qt.Orientation.Vertical)
            self._layout_btn.setText("上下布局")
            self._settings.layout_orientation = True
        self._settings.save()
