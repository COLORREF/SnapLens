"""首次运行引导向导：分步设置快捷键、AI 翻译、OCR 语言、保存目录。"""
import os

from PySide6.QtCore import Qt
from PySide6.QtGui import QKeySequence
from PySide6.QtWidgets import (
    QButtonGroup, QCheckBox, QComboBox, QFileDialog, QFormLayout, QGroupBox,
    QHBoxLayout, QKeySequenceEdit, QLabel, QLineEdit,
    QPushButton, QRadioButton, QVBoxLayout, QWizard, QWizardPage, QWidget,
)

from ..core.settings import Settings
from ..core.api_client import list_models
from ..core.ocr import find_tessdata_dir


def _default_save_dir() -> str:
    """默认截图保存目录。"""
    from PySide6.QtCore import QStandardPaths
    pics = QStandardPaths.writableLocation(
        QStandardPaths.StandardLocation.PicturesLocation
    )
    return os.path.join(pics or os.path.expanduser("~"), "SnapLens")


class _WelcomePage(QWizardPage):
    """欢迎页面。"""

    def __init__(self, parent=None):
        super().__init__(parent)
        self.setTitle("欢迎使用 SnapLens")
        self.setSubTitle("首次运行需要完成基本设置，只需一分钟。")

        layout = QVBoxLayout(self)
        intro = QLabel(
            "SnapLens 是一个轻量级的截图工具，支持：\n\n"
            "  - 截图 / 钉图 / 取色\n"
            "  - OCR 文字识别\n"
            "  - AI 翻译（需配置 API Key）\n\n"
            "点击\"下一步\"选择使用模式。"
        )
        intro.setWordWrap(True)
        layout.addWidget(intro)
        layout.addStretch()


class _ModePage(QWizardPage):
    """应用模式选择：翻译模式 vs 截图模式。"""

    def __init__(self, parent=None):
        super().__init__(parent)
        self.setTitle("选择使用模式")
        self.setSubTitle(
            "选择默认的使用方式，后续可在设置或托盘菜单中切换。"
        )

        layout = QVBoxLayout(self)

        # 用 QButtonGroup 保证两个 RadioButton 互斥
        self._mode_group = QButtonGroup(self)

        # 翻译模式
        group = QGroupBox()
        g_layout = QVBoxLayout(group)

        self._translate_radio = QRadioButton("翻译模式")
        self._translate_radio.setChecked(True)
        self._mode_group.addButton(self._translate_radio)
        translate_desc = QLabel(
            "启动后直接显示文本翻译窗口，方便随时进行翻译。\n"
            "截图功能通过快捷键触发，不影响翻译使用。\n"
            "适用于以翻译为主要使用场景的用户。"
        )
        translate_desc.setWordWrap(True)
        translate_desc.setStyleSheet("color: gray; margin-left: 20px;")
        g_layout.addWidget(self._translate_radio)
        g_layout.addWidget(translate_desc)

        layout.addWidget(group)

        # 截图模式
        group2 = QGroupBox()
        g_layout2 = QVBoxLayout(group2)

        self._screenshot_radio = QRadioButton("截图模式")
        self._mode_group.addButton(self._screenshot_radio)
        screenshot_desc = QLabel(
            "启动后静默在后台运行，仅显示托盘图标。\n"
            "截图通过快捷键触发，翻译功能可通过托盘菜单或截图工具条打开。\n"
            "适用于以截图为主要使用场景、追求简洁体验的用户。"
        )
        screenshot_desc.setWordWrap(True)
        screenshot_desc.setStyleSheet("color: gray; margin-left: 20px;")
        g_layout2.addWidget(self._screenshot_radio)
        g_layout2.addWidget(screenshot_desc)

        layout.addWidget(group2)
        layout.addStretch()

    def app_mode(self) -> str:
        return "translate" if self._translate_radio.isChecked() else "screenshot"


class _HotkeyPage(QWizardPage):
    """截图快捷键设置。"""

    def __init__(self, parent=None):
        super().__init__(parent)
        self.setTitle("截图快捷键")
        self.setSubTitle("设置触发截图的全局快捷键。")

        layout = QFormLayout(self)
        self._edit = QKeySequenceEdit(QKeySequence("Ctrl+Shift+Z"))
        layout.addRow("快捷键：", self._edit)

        hint = QLabel("默认 Ctrl+Shift+Z，可按需修改。")
        layout.addRow(hint)

    def hotkey(self) -> str:
        return self._edit.keySequence().toString(
            QKeySequence.SequenceFormat.PortableText
        )


class _AiTranslationPage(QWizardPage):
    """AI 翻译设置。"""

    def __init__(self, parent=None):
        super().__init__(parent)
        self.setTitle("AI 翻译设置")
        self.setSubTitle(
            "配置 AI 翻译服务。选择服务商并填入对应的 API Key 即可使用。"
        )

        layout = QFormLayout(self)

        # 按服务商记忆的 API Key（会话级，不持久化）
        self._provider_keys: dict[str, str] = {}
        self._current_provider: str | None = None

        # 服务商（切换时联动 API 地址和模型）
        from ..ai import PROVIDER_CONFIGS, list_providers
        self._provider_combo = QComboBox()
        for pid in list_providers():
            cfg = PROVIDER_CONFIGS[pid]
            self._provider_combo.addItem(cfg["label"], pid)
        self._provider_combo.currentIndexChanged.connect(self._on_provider_changed)
        layout.addRow("服务商：", self._provider_combo)

        # 初始化 _current_provider：combo 已默认选中第一项，但信号尚未触发过
        self._current_provider = self._provider_combo.currentData()

        # API Key
        self._api_key_edit = QLineEdit("")
        self._api_key_edit.setPlaceholderText("请输入 API Key（sk-...）")
        layout.addRow("API Key：", self._api_key_edit)

        # API 地址
        self._api_base_edit = QLineEdit("https://api.deepseek.com/v1")
        layout.addRow("API 地址：", self._api_base_edit)

        # 模型（可编辑下拉框 + 自动获取按钮）
        model_row = QHBoxLayout()
        self._model_combo = QComboBox()
        self._model_combo.setEditable(True)
        self._model_combo.setPlaceholderText("输入模型名称或点击右侧按钮自动获取")
        model_row.addWidget(self._model_combo)
        self._fetch_models_btn = QPushButton("获取模型列表")
        self._fetch_models_btn.setFixedWidth(100)
        self._fetch_models_btn.setFocusPolicy(Qt.FocusPolicy.NoFocus)
        self._fetch_models_btn.clicked.connect(self._on_fetch_models)
        model_row.addWidget(self._fetch_models_btn)
        layout.addRow("模型：", model_row)

        # 目标语言
        self._target_lang_combo = QComboBox()
        self._target_lang_combo.setEditable(True)
        self._target_lang_combo.addItems([
            "简体中文", "繁体中文（台湾）", "繁体中文（香港）",
            "英语", "日语", "韩���", "法语", "德语",
            "西班牙语", "葡萄牙语", "俄语", "阿拉伯语",
            "泰语", "越南语",
        ])
        layout.addRow("默认目标语言：", self._target_lang_combo)

        # 错误信息标签（初始隐藏，获取模型失败时在此显示详细错误）
        self._error_label = QLabel("")
        self._error_label.setWordWrap(True)
        self._error_label.setVisible(False)
        layout.addRow(self._error_label)

    def _on_provider_changed(self, _index: int):
        """服务商切换时：保存旧厂商 Key → 更新 API 地址 → 恢复新厂商 Key。"""
        from ..ai import PROVIDER_CONFIGS
        new_pid = self._provider_combo.currentData()
        if not new_pid or new_pid not in PROVIDER_CONFIGS:
            return

        # 保存当前厂商的 Key（切换前）
        if self._current_provider and self._current_provider != new_pid:
            self._provider_keys[self._current_provider] = self._api_key_edit.text().strip()

        # 更新 API 地址
        self._api_base_edit.setText(PROVIDER_CONFIGS[new_pid]["base_url"])

        # 恢复新厂商的 Key（首次切换到该厂商时为空）
        if new_pid != self._current_provider:
            saved_key = self._provider_keys.get(new_pid, "")
            self._api_key_edit.setText(saved_key)

        self._current_provider = new_pid

    def _on_fetch_models(self):
        """从当前服务商获取可用模型列表，填充到下拉框。"""
        api_key = self._api_key_edit.text().strip()
        api_base = self._api_base_edit.text().strip()
        self._error_label.setVisible(False)

        if not api_key:
            self._error_label.setText("请先填写 API Key 后再获取模型列表。")
            self._error_label.setVisible(True)
            self._api_key_edit.setFocus()
            return
        if not api_base:
            self._error_label.setText("请先填写 API 地址后再获取模型列表。")
            self._error_label.setVisible(True)
            self._api_base_edit.setFocus()
            return

        self._fetch_models_btn.setEnabled(False)
        self._fetch_models_btn.setText("获取中…")
        self._model_combo.clear()

        try:
            models = list_models(api_key, api_base)
            if models:
                self._model_combo.addItems(models)
                self._model_combo.setCurrentIndex(0)
            else:
                self._model_combo.addItem("（服务商未返回模型列表）")
                self._model_combo.setCurrentIndex(0)
        except ConnectionError as e:
            self._model_combo.addItem("（网络连接失败）")
            self._model_combo.setCurrentIndex(0)
            self._error_label.setText(
                f"网络连接失败，请检查网络。\n地址：{api_base}\n错误：{e}"
            )
            self._error_label.setVisible(True)
        except TimeoutError as e:
            self._model_combo.addItem("（请求超时）")
            self._model_combo.setCurrentIndex(0)
            self._error_label.setText(
                f"请求超时，请稍后重试。\n地址：{api_base}\n错误：{e}"
            )
            self._error_label.setVisible(True)
        except RuntimeError as e:
            msg = str(e)
            if "鉴权" in msg:
                hint = "API Key 鉴权失败，请确认 Key 是否正确"
            elif "额度" in msg:
                hint = "API 额度不足或受到频率限制"
            else:
                hint = "该服务商可能不支持自动获取模型列表，请手动输入模型名称"
            self._model_combo.addItem("（获取失败）")
            self._model_combo.setCurrentIndex(0)
            self._error_label.setText(
                f"{hint}\n服务商：{self._provider_combo.currentText()}\n地址：{api_base}\n错误：{msg}"
            )
            self._error_label.setVisible(True)
        finally:
            self._fetch_models_btn.setEnabled(True)
            self._fetch_models_btn.setText("获取模型列表")

    def collect(self) -> dict:
        api_key = self._api_key_edit.text().strip()
        return {
            "ai_provider": self._provider_combo.currentData(),
            "ai_api_key": api_key,
            "ai_model": self._model_combo.currentText().strip(),
            "ai_target_lang": self._target_lang_combo.currentText(),
            "ai_api_base": self._api_base_edit.text().strip() or "https://api.deepseek.com/v1",
        }


class _OcrPage(QWizardPage):
    """OCR 语言包选择。"""

    # 已知语言 {code: display_name}
    _LANG_MAP = {
        "chi_sim": "中文简体", "chi_tra": "中文繁体",
        "eng": "英文", "jpn": "日语", "kor": "韩语",
        "fra": "法语", "deu": "德语", "spa": "西班牙语",
        "rus": "俄语",
    }

    def __init__(self, parent=None):
        super().__init__(parent)
        self.setTitle("OCR 文字识别")
        self.setSubTitle("选择 OCR 识别支持的语言。")

        layout = QVBoxLayout(self)

        # 检测已有语言包
        tessdata = find_tessdata_dir()
        os.makedirs(tessdata, exist_ok=True)
        installed = self._scan_installed(tessdata)
        layout.addWidget(QLabel("勾选需要识别的语言："))
        self._checks: dict[str, QCheckBox] = {}
        for code, name in self._LANG_MAP.items():
            cb = QCheckBox(f"{name}  ({code})")
            cb.setChecked(code in {"chi_sim", "eng"} and code in installed)
            cb.setEnabled(code in installed)
            layout.addWidget(cb)
            self._checks[code] = cb

        # 显示未安装的语言
        missing = [n for c, n in self._LANG_MAP.items() if c not in installed]
        if missing:
            missing_label = QLabel(
                f'未安装的语言包（可在"设置 > OCR 识别"中下载）：'
                f" {', '.join(missing)}"
            )
            missing_label.setWordWrap(True)
            layout.addWidget(missing_label)

        if not installed:
            hint = QLabel(
                "语言包下载后自动保存到 sdk/tesseract/tessdata/。\n"
                "OCR 引擎的 SDK DLL 目录和 tessdata 目录可在后续\n"
                "\"设置 > OCR 识别\"中手动指定。"
            )
            hint.setWordWrap(True)
            layout.addWidget(hint)

        layout.addStretch()

    @staticmethod
    def _scan_installed(tessdata_dir: str) -> set[str]:
        installed = set()
        if not os.path.isdir(tessdata_dir):
            return installed
        for f in os.listdir(tessdata_dir):
            if f.endswith(".traineddata"):
                code = f[:-len(".traineddata")]
                if code != "osd":
                    installed.add(code)
        return installed

    def get_selected_langs(self) -> str:
        """返回 + 分隔的语言列表，至少包含 "eng"。"""
        if not hasattr(self, "_checks"):
            return "eng"
        selected = [c for c, cb in self._checks.items() if cb.isChecked()]
        return "+".join(selected) if selected else "eng"


class _SavePage(QWizardPage):
    """保存位置和格式。"""

    def __init__(self, parent=None):
        super().__init__(parent)
        self.setTitle("截图保存")
        self.setSubTitle("设置截图文件的默认保存位置和格式。")

        layout = QFormLayout(self)

        # 保存目录
        dir_w = QWidget()
        dir_row = QHBoxLayout(dir_w)
        dir_row.setContentsMargins(0, 0, 0, 0)
        self._dir_edit = QLineEdit(_default_save_dir())
        browse = QPushButton("浏览…")
        browse.clicked.connect(self._browse)
        dir_row.addWidget(self._dir_edit)
        dir_row.addWidget(browse)
        layout.addRow("保存目录：", dir_w)

        # 保存格式
        self._format_combo = QComboBox()
        self._format_combo.addItems(["png", "jpg", "bmp"])
        layout.addRow("保存格式：", self._format_combo)

    def _browse(self):
        d = QFileDialog.getExistingDirectory(self, "选择截图保存目录", self._dir_edit.text())
        if d:
            self._dir_edit.setText(d)

    def save_dir(self) -> str:
        return self._dir_edit.text().strip() or _default_save_dir()

    def save_format(self) -> str:
        return self._format_combo.currentText()


class _FinishPage(QWizardPage):
    """完成页面 — 汇总设置。"""

    def __init__(self, parent=None):
        super().__init__(parent)
        self.setTitle("设置完成")
        self.setSubTitle('以下是您的配置汇总，点击"完成"保存并启动。')

        layout = QVBoxLayout(self)
        self._summary = QLabel()
        self._summary.setWordWrap(True)
        layout.addWidget(self._summary)
        layout.addStretch()

    def set_summary(self, text: str):
        self._summary.setText(text)


class SetupWizard(QWizard):
    """首次运行的设置引导向导。

    用法：
        wizard = SetupWizard()
        if wizard.exec() == QWizard.DialogCode.Accepted:
            settings_data = wizard.collect_settings()
    """

    def __init__(self, notify_manager=None, parent=None):
        super().__init__(parent)
        self._notify_manager = notify_manager
        self.setWindowTitle("SnapLens 首次设置")
        self.setMinimumSize(520, 420)
        self.setWizardStyle(QWizard.WizardStyle.ClassicStyle)

        # 创建各页
        self._page_welcome = _WelcomePage()
        self._page_mode = _ModePage()
        self._page_hotkey = _HotkeyPage()
        self._page_ai = _AiTranslationPage()
        self._page_ocr = _OcrPage()
        self._page_save = _SavePage()
        self._page_finish = _FinishPage()

        self.addPage(self._page_welcome)
        self.addPage(self._page_mode)
        self.addPage(self._page_hotkey)
        self.addPage(self._page_ai)
        self.addPage(self._page_ocr)
        self.addPage(self._page_save)
        self.addPage(self._page_finish)

        # 从欢迎页到快捷键页时不需要"上一步"
        # 完成页隐藏"取消"按钮
        self.currentIdChanged.connect(self._on_page_changed)

    def reject(self):
        """取消或关闭窗口时确认（走统一通知系统）。"""
        if self._notify_manager is not None:
            confirmed = self._notify_manager.confirm(
                "确认取消",
                "取消引导设置将无法进入程序，确定要退出吗？",
                self,
            )
        else:
            # 防御：没有通知管理器时回退到原始 QMessageBox
            from PySide6.QtWidgets import QMessageBox
            reply = QMessageBox.question(
                self, "确认取消",
                "取消引导设置将无法进入程序，确定要退出吗？",
                QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No,
                QMessageBox.StandardButton.No,
            )
            confirmed = (reply == QMessageBox.StandardButton.Yes)
        if confirmed:
            super().reject()

    def _on_page_changed(self, page_id: int):
        """页面切换时更新完成页汇总。"""
        if self.page(page_id) is self._page_finish:
            summary = (
                f"<b>使用模式：</b>"
                f"{'翻译模式' if self._page_mode.app_mode() == 'translate' else '截图模式'}<br>"
                f"<b>截图快捷键：</b>{self._page_hotkey.hotkey()}<br>"
                f"<b>AI 模型：</b>{self._page_ai._model_combo.currentText()}<br>"
                f"<b>目标语言：</b>{self._page_ai._target_lang_combo.currentText()}<br>"
                f"<b>OCR 语言：</b>{self._page_ocr.get_selected_langs()}<br>"
                f"<b>保存目录：</b>{self._page_save.save_dir()}<br>"
                f"<b>保存格式：</b>{self._page_save.save_format()}<br>"
            )
            self._page_finish.set_summary(summary)

    def collect_settings(self) -> dict:
        """收集各页设置，返回完整 settings dict。"""
        defaults = Settings.defaults_dict()

        # 覆盖用户设置的字段
        defaults["app_mode"] = self._page_mode.app_mode()
        defaults["hotkey"] = self._page_hotkey.hotkey()
        defaults["save_dir"] = self._page_save.save_dir()
        defaults["save_format"] = self._page_save.save_format()
        defaults["ai_ocr_langs"] = self._page_ocr.get_selected_langs()

        ai = self._page_ai.collect()
        for k, v in ai.items():
            if v:  # 非空才覆盖（空字符串保留默认值）
                defaults[k] = v

        return defaults
