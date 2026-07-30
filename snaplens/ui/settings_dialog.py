"""设置对话框：分页组织 —— 截图、放大镜、坐标与颜色、存储、AI翻译、常规。"""
import os

from PySide6.QtCore import Qt
from PySide6.QtGui import QKeySequence
from PySide6.QtWidgets import (QCheckBox, QComboBox, QDialog, QDialogButtonBox,
                               QFileDialog, QFormLayout, QFrame, QHBoxLayout,
                               QKeySequenceEdit, QLabel, QLineEdit,
                               QProgressBar, QPushButton,
                               QScrollArea, QSlider, QTabWidget, QTextEdit, QVBoxLayout,
                               QWidget)

from ..core.temp_cleanup import cleanup_temp_dir
from ..core.settings import Settings
from ..core.api_client import list_models
from ..ai import PROVIDER_CONFIGS, list_providers
from .color_picker import ColorPickerButton


def _make_slider_row(value: int, min_val: int, max_val: int, suffix: str = "%"):
    """创建 滑块 + 数值标签 的控件对。"""
    widget = QWidget()
    row = QHBoxLayout(widget)
    row.setContentsMargins(0, 0, 0, 0)
    slider = QSlider(Qt.Orientation.Horizontal)
    slider.setRange(min_val, max_val)
    slider.setValue(value)
    label = QLabel(f"{value}{suffix}")
    label.setFixedWidth(55)
    slider.valueChanged.connect(lambda v: label.setText(f"{v}{suffix}"))
    row.addWidget(slider)
    row.addWidget(label)
    return widget, slider


def _make_decimal_slider_row(value: float, min_val: float, max_val: float,
                              decimals: int = 2):
    """创建小数滑块（内部用整数），返回 (widget, slider, label)。"""
    mult = 10 ** decimals
    widget = QWidget()
    row = QHBoxLayout(widget)
    row.setContentsMargins(0, 0, 0, 0)
    slider = QSlider(Qt.Orientation.Horizontal)
    slider.setRange(int(min_val * mult), int(max_val * mult))
    slider.setValue(int(value * mult))
    label = QLabel(f"{value:.{decimals}f}")
    label.setFixedWidth(55)
    slider.valueChanged.connect(
        lambda v, l=label, d=decimals, m=mult: l.setText(f"{v / m:.{d}f}")
    )
    row.addWidget(slider)
    row.addWidget(label)
    return widget, slider


def _wrap_scroll(widget: QWidget) -> QScrollArea:
    """将内容包裹在滚动区域中，每一页调用。"""
    scroll = QScrollArea()
    scroll.setWidgetResizable(True)
    scroll.setWidget(widget)
    return scroll


def _add_sep(form: QFormLayout):
    """在表单中添加一条水平分隔线。"""
    line = QFrame()
    line.setFrameShape(QFrame.Shape.HLine)
    line.setFrameShadow(QFrame.Shadow.Sunken)
    form.addRow(line)


# ---- OCR 语言包管理 ----

# 支持下载的语言包：{代码: 显示名}
_KNOWN_LANGS = {
    "chi_sim": "中文简体", "chi_tra": "中文繁体",
    "eng":     "英文",     "jpn": "日语",
    "kor":     "韩语",     "fra": "法语",
    "deu":     "德语",     "spa": "西班牙语",
    "rus":     "俄语",     "ara": "阿拉伯语",
    "tha":     "泰语",     "vie": "越南语",
    "por":     "葡萄牙语", "ita": "意大利语",
    "nld":     "荷兰语",   "pol": "波兰语",
    "tur":     "土耳其语", "hin": "印地语",
    "ind":     "印尼语",   "msa": "马来语",
    "swe":     "瑞典语",   "dan": "丹麦语",
    "nor":     "挪威语",   "fin": "芬兰语",
    "hun":     "匈牙利语", "ces": "捷克语",
    "ron":     "罗马尼亚语", "ukr": "乌克兰语",
    "heb":     "希伯来语", "ell": "希腊语",
}
# jsdelivr CDN 下载地址模板
_LANG_DOWNLOAD_URL = (
    "https://cdn.jsdelivr.net/gh/tesseract-ocr/tessdata_fast@main/{code}.traineddata"
)
from ..core.ocr import find_tessdata_dir

# 查找 tessdata 目录 — 已迁移至 snaplens.core.ocr.find_tessdata_dir()
_find_tessdata_dir = find_tessdata_dir


def _scan_installed_langs(tessdata_dir: str) -> set[str]:
    """扫描 tessdata 目录下已安装的语言包。"""
    installed = set()
    if not tessdata_dir or not os.path.isdir(tessdata_dir):
        return installed
    for f in os.listdir(tessdata_dir):
        if f.endswith(".traineddata"):
            code = f[:-len(".traineddata")]
            if code != "osd":  # osd 是方向检测，不是 OCR 语言
                installed.add(code)
    return installed


class _OcrLangManager(QWidget):
    """OCR 语言包管理控件：勾选启用的语言 + 下载/删除操作。

    下载使用 QNetworkAccessManager，非阻塞，显示进度条。
    通知通过 NotifyManager 统一分发。
    """

    def __init__(self, current_langs: str, notify_manager,
                 settings_dialog_parent=None, parent=None):
        super().__init__(parent)
        self._notify_manager = notify_manager
        self._settings_dialog_parent = settings_dialog_parent
        self._tessdata_dir = _find_tessdata_dir()
        self._installed = _scan_installed_langs(self._tessdata_dir) if self._tessdata_dir else set()
        self._selected = set(current_langs.split("+")) if current_langs else set()
        self._checks: dict[str, QCheckBox] = {}
        self._downloads: dict[str, tuple] = {}  # code -> (reply, bar, row_layout)
        self._network_mgr = None  # lazy init

        self._main_layout = QVBoxLayout(self)
        self._main_layout.setContentsMargins(0, 0, 0, 0)
        self._main_layout.setSpacing(4)

        if not self._tessdata_dir:
            self._main_layout.addWidget(QLabel("未检测到 tessdata 目录，请先部署 Tesseract。"))
            return

        self._rebuild_ui()

    def _network(self):
        """延迟初始化 QNetworkAccessManager。"""
        from PySide6.QtNetwork import QNetworkAccessManager
        if self._network_mgr is None:
            self._network_mgr = QNetworkAccessManager(self)
        return self._network_mgr

    def _rebuild_ui(self):
        """清空并重建所有行。"""
        # 清空已有的 layout items
        while self._main_layout.count():
            item = self._main_layout.takeAt(0)
            if item.widget():
                item.widget().deleteLater()
            elif item.layout():
                self._clear_layout(item.layout())

        self._checks.clear()

        installed_known = {c: n for c, n in _KNOWN_LANGS.items() if c in self._installed}
        available = {c: n for c, n in _KNOWN_LANGS.items() if c not in self._installed}
        unknown_installed = self._installed - set(_KNOWN_LANGS.keys())

        # ---- 已安装 ----
        if installed_known or unknown_installed:
            self._main_layout.addWidget(QLabel("已安装（勾选即启用）："))
            for code, name in sorted(installed_known.items(), key=lambda x: x[1]):
                self._add_installed_row(code, name)
            for code in sorted(unknown_installed):
                self._add_installed_row(code, f"{code}（未知语言）")

        # ---- 进行中的下载 ----
        if self._downloads:
            self._main_layout.addSpacing(8)
            self._main_layout.addWidget(QLabel("下载中："))

        # ---- 可下载 ----
        if available:
            self._main_layout.addSpacing(8)
            self._main_layout.addWidget(QLabel("可下载："))
            for code, name in sorted(available.items(), key=lambda x: x[1]):
                row = QHBoxLayout()
                row.addWidget(QLabel(f"  {name} ({code})"))
                dl_btn = QPushButton("下载")
                dl_btn.setFixedWidth(50)
                dl_btn.clicked.connect(
                    lambda _checked=False, c=code, n=name: self._confirm_download(c, n)
                )
                row.addWidget(dl_btn)
                row.addStretch()
                self._main_layout.addLayout(row)

        self._main_layout.addStretch()

    def _clear_layout(self, layout):
        """递归清空 layout。"""
        while layout.count():
            item = layout.takeAt(0)
            if item.widget():
                item.widget().deleteLater()
            elif item.layout():
                self._clear_layout(item.layout())

    def _add_installed_row(self, code: str, name: str):
        """添加已安装语言行。"""
        row = QHBoxLayout()
        cb = QCheckBox(f"{name} ({code})")
        cb.setChecked(code in self._selected)
        row.addWidget(cb)
        del_btn = QPushButton("删除")
        del_btn.setFixedWidth(50)
        del_btn.clicked.connect(
            lambda _checked=False, c=code, n=name: self._delete_lang(c, n)
        )
        row.addWidget(del_btn)
        row.addStretch()
        self._main_layout.addLayout(row)
        self._checks[code] = cb

    # ---------------------------------------------------------------- 下载
    def _confirm_download(self, code: str, name: str):
        """弹出确认对话框，确认后启动下载。"""
        url = _LANG_DOWNLOAD_URL.format(code=code)
        if not self._notify_manager.confirm(
            "确认下载",
            f"即将下载语言包「{name}」({code})\n\n"
            f"下载地址：{url}\n\n"
            f"⚠ 安全提示：\n"
            f"· 该文件来自第三方 CDN (jsdelivr)，非 Tesseract 官方直接分发\n"
            f"· 源仓库为 tesseract-ocr/tessdata_fast (GitHub 开源项目)\n"
            f"· 下载后文件将保存到：\n  {self._tessdata_dir}\\{code}.traineddata\n\n"
            f"是否继续？",
            parent=self._settings_dialog_parent,
        ):
            return
        self._start_download(code, name, url)

    def _start_download(self, code: str, name: str, url: str):
        """预添加下载行，启动 QNetworkAccessManager 下载。"""
        from PySide6.QtNetwork import QNetworkRequest
        from PySide6.QtCore import QUrl

        # 创建下载行：禁用复选框 + 进度条
        row = QHBoxLayout()
        cb = QCheckBox(f"{name} ({code})")
        cb.setEnabled(False)
        row.addWidget(cb)

        bar = QProgressBar()
        bar.setRange(0, 100)
        bar.setValue(0)
        bar.setFixedWidth(120)
        bar.setFixedHeight(16)
        bar.setFormat("%p%")
        row.addWidget(bar)
        row.addStretch()

        # 插入到"可下载"区域前面
        insert_idx = self._main_layout.count() - 1  # before stretch
        for i in range(self._main_layout.count()):
            item = self._main_layout.itemAt(i)
            if item.widget() and isinstance(item.widget(), QLabel) \
                    and item.widget().text() == "可下载：":
                insert_idx = i
                break

        self._main_layout.insertLayout(insert_idx, row)
        self._checks[code] = cb

        # 发起网络请求
        req = QNetworkRequest(QUrl(url))
        req.setRawHeader(b"User-Agent", b"Mozilla/5.0")
        net_reply = self._network().get(req)
        net_reply.downloadProgress.connect(
            lambda received, total, b=bar: self._on_progress(b, received, total)
        )
        net_reply.finished.connect(
            lambda c=code, n=name, r=net_reply, b=bar, l=row:
            self._on_download_finished(c, n, r, b, l)
        )
        self._downloads[code] = (net_reply, bar, row)

    def _on_progress(self, bar: QProgressBar, received: int, total: int):
        """更新进度条。"""
        if total > 0:
            bar.setRange(0, total)
            bar.setValue(received)

    def _on_download_finished(self, code: str, name: str,
                               reply, bar: QProgressBar, row: QHBoxLayout):
        """下载完成或失败的处理。"""
        from PySide6.QtNetwork import QNetworkReply
        error = reply.error()
        file_path = os.path.join(self._tessdata_dir, f"{code}.traineddata")

        if error == QNetworkReply.NetworkError.NoError:
            data = reply.readAll().data()
            try:
                with open(file_path, "wb") as f:
                    f.write(data)
                self._installed.add(code)
                self._selected.add(code)
                self._replace_download_row(code, name, row, bar)
                self._notify_manager.notify(
                    "lang_download_success", "语言包下载",
                    f"「{name}」下载完成，已自动启用。",
                )
            except OSError as e:
                self._downloads.pop(code, None)
                reply.deleteLater()
                self._rebuild_ui()
                self._notify_manager.notify(
                    "lang_download_fail", "下载失败",
                    f"「{name}」写入文件失败：{e}",
                )
                return
        else:
            self._downloads.pop(code, None)
            reply.deleteLater()
            self._rebuild_ui()
            reason = reply.errorString()
            self._notify_manager.notify(
                "lang_download_fail", "下载失败",
                f"「{name}」下载失败：{reason}",
            )
            return

        reply.deleteLater()
        self._downloads.pop(code, None)

    def _replace_download_row(self, code: str, name: str,
                                row: QHBoxLayout, bar: QProgressBar):
        """下载完成后，将进度条行替换为正常的已安装行。"""
        # 移除进度条
        bar.deleteLater()
        # 移除旧 checkbox
        old_cb = self._checks[code]
        old_cb.deleteLater()

        # 创建新行
        # 清除 row 中的所有项
        while row.count():
            item = row.takeAt(0)
            if item.widget():
                item.widget().deleteLater()

        new_cb = QCheckBox(f"{name} ({code})")
        new_cb.setChecked(True)  # 默认勾选
        row.addWidget(new_cb)
        self._checks[code] = new_cb

        del_btn = QPushButton("删除")
        del_btn.setFixedWidth(50)
        del_btn.clicked.connect(
            lambda _checked=False, c=code, n=name: self._delete_lang(c, n)
        )
        row.addWidget(del_btn)
        row.addStretch()

    # ---------------------------------------------------------------- 删除
    def _delete_lang(self, code: str, name: str):
        """删除语言包（需用户确认）。"""
        if not self._notify_manager.confirm(
            "确认删除",
            f"确定要删除语言包「{name}」({code}) 吗？\n\n"
            f"文件将被永久删除：\n{self._tessdata_dir}\\{code}.traineddata",
            parent=self._settings_dialog_parent,
        ):
            return
        file_path = os.path.join(self._tessdata_dir, f"{code}.traineddata")
        try:
            os.remove(file_path)
            self._installed.discard(code)
            self._selected.discard(code)
            self._rebuild_ui()
            self._notify_manager.info(
                "已删除", f"语言包「{name}」已删除。",
                parent=self._settings_dialog_parent,
            )
        except OSError as e:
            self._notify_manager.warn(
                "删除失败", f"无法删除文件：{e}",
                parent=self._settings_dialog_parent,
            )

    def get_selected_langs(self) -> str:
        """返回当前选中的语言包列表（+ 分隔）。"""
        if not self._tessdata_dir:
            return "eng"
        selected = [c for c, cb in self._checks.items()
                     if cb.isEnabled() and cb.isChecked()]
        return "+".join(selected) if selected else "eng"


class SettingsDialog(QDialog):
    """设置对话框，包含七个标签页。

    弹窗和托盘通知均通过 NotifyManager 统一分发，
    不再使用独立的 tray_notify 信号。
    """

    def __init__(self, settings, notify_manager, parent=None):
        super().__init__(parent)
        self._settings = settings
        self._notify_manager = notify_manager

        # 按服务商记忆的 API Key（会话级，切换后可恢复）
        self._provider_keys: dict[str, str] = {}
        if settings.ai_api_key:
            self._provider_keys[settings.ai_provider] = settings.ai_api_key
        self._current_provider: str | None = settings.ai_provider
        self.setWindowTitle("SnapLens 设置")
        self.setMinimumWidth(480)
        self.setMinimumHeight(500)

        layout = QVBoxLayout(self)
        tabs = QTabWidget()

        tabs.addTab(_wrap_scroll(self._build_screenshot_tab(settings)), "截 图")
        tabs.addTab(_wrap_scroll(self._build_magnifier_tab(settings)), "放大镜")
        tabs.addTab(_wrap_scroll(self._build_labels_tab(settings)), "坐标与颜色")
        tabs.addTab(_wrap_scroll(self._build_storage_tab(settings)), "存 储")
        tabs.addTab(_wrap_scroll(self._build_ocr_tab(settings)), "OCR 识别")
        tabs.addTab(_wrap_scroll(self._build_ai_settings_tab(settings)), "AI 设置")
        tabs.addTab(_wrap_scroll(self._build_ai_translate_tab(settings)), "翻 译")
        tabs.addTab(_wrap_scroll(self._build_general_tab(settings)), "常 规")

        layout.addWidget(tabs)

        # 底部按钮
        buttons = QDialogButtonBox()
        save_btn = buttons.addButton("保存", QDialogButtonBox.ButtonRole.AcceptRole)
        buttons.addButton("取消", QDialogButtonBox.ButtonRole.RejectRole)
        reset_btn = buttons.addButton("恢复全部默认", QDialogButtonBox.ButtonRole.ResetRole)
        buttons.accepted.connect(self.accept)
        buttons.rejected.connect(self.reject)
        reset_btn.clicked.connect(self._on_reset_all)
        layout.addWidget(buttons)

        # ---- 配置项 → widget 映射表（数据驱动重置和序列化） ----
        # 格式: key → (widget, setter_type, tab_name)
        # 新增设置项只需在此加一行，重置和 as_dict() 自动生效
        self._reset_map: dict[str, tuple] = {
            # 截图
            "hotkey":               (self._hotkey_edit,           "keySequence",       "screenshot"),
            "toolbar_opacity":      (self._toolbar_opacity,       "value",             "screenshot"),
            "crosshair_color":      (self._crosshair_color_picker,"color",             "screenshot"),
            "crosshair_alpha":      (self._crosshair_alpha,       "value",             "screenshot"),
            "crosshair_thickness":  (self._crosshair_thickness,   "value",             "screenshot"),
            "crosshair_invert":     (self._crosshair_invert,      "checked",           "screenshot"),
            "crosshair_enabled":    (self._crosshair_enabled,     "checked",           "screenshot"),
            "cursor_enabled":       (self._cursor_enabled,        "checked",           "screenshot"),
            # 放大镜
            "magnifier_enabled":        (self._magnifier_enabled,     "checked",       "magnifier"),
            "magnifier_zoom":           (self._magnifier_zoom,        "value_scale10", "magnifier"),
            "magnifier_size":           (self._magnifier_size,        "value",         "magnifier"),
            "magnifier_wheel_zoom":     (self._magnifier_wheel_zoom,  "checked",       "magnifier"),
            "magnifier_cross_color":    (self._cross_color_picker,    "color",         "magnifier"),
            "magnifier_cross_alpha":    (self._magnifier_cross_alpha, "value",         "magnifier"),
            "magnifier_cross_thickness":(self._magnifier_cross_thickness, "value",     "magnifier"),
            "magnifier_cross_invert":   (self._magnifier_cross_invert,"checked",       "magnifier"),
            "grid_enabled":         (self._grid_enabled,          "checked",           "magnifier"),
            "grid_color":           (self._grid_color_picker,     "color",             "magnifier"),
            "grid_alpha":           (self._grid_alpha,            "value",             "magnifier"),
            "edge_mode":            (self._edge_mode_combo,       "combo_edge_mode",   "magnifier"),
            "edge_color":           (self._edge_color_picker,     "color",             "magnifier"),
            "zoom_label_enabled":   (self._zoom_label_enabled,    "checked",           "magnifier"),
            "zoom_label_color":     (self._zoom_label_color_picker, "color",           "magnifier"),
            "zoom_label_alpha":     (self._zoom_label_alpha,      "value",             "magnifier"),
            # 坐标与颜色
            "coord_label_enabled":      (self._coord_label_enabled,       "checked",    "labels"),
            "coord_label_text_color":   (self._coord_label_text_color_picker, "color",   "labels"),
            "coord_label_bg_color":     (self._coord_label_bg_color_picker, "color",     "labels"),
            "coord_label_bg_alpha":     (self._coord_label_bg_alpha,   "value",          "labels"),
            "color_label_enabled":      (self._color_label_enabled,       "checked",    "labels"),
            "color_label_text_color":   (self._color_label_text_color_picker, "color",   "labels"),
            "color_label_bg_color":     (self._color_label_bg_color_picker, "color",     "labels"),
            "color_label_bg_alpha":     (self._color_label_bg_alpha,   "value",          "labels"),
            "color_format":         (self._color_format_combo,    "combo_color_format","labels"),
            "copy_color_key":       (self._copy_color_key_edit,   "keySequence",       "labels"),
            "copy_hex_prefix":      (self._copy_hex_prefix,       "checked",           "labels"),
            "copy_rgb_prefix":      (self._copy_rgb_prefix,       "checked",           "labels"),
            # 存储
            "save_format":          (self._format_combo,          "combo_findText",    "storage"),
            "config_dir":           (self._config_edit,           "clear_edit",        "storage"),
            "temp_dir":             (self._temp_edit,             "clear_edit",        "storage"),
            "cleanup_on_startup":   (self._cleanup_on_startup,    "checked",           "storage"),
            "cleanup_on_window_close":(self._cleanup_on_window_close, "checked",       "storage"),
            # OCR
            "ai_ocr_langs":         (self._ocr_lang_list,         "ocr_langs",         "ocr"),
            # AI 设置
            "ai_provider":          (self._ai_provider_combo,     "combo_findData",    "ai_settings"),
            "ai_api_key":           (self._ai_api_key_edit,       "text",              "ai_settings"),
            "ai_api_base":          (self._ai_api_base_edit,      "text",              "ai_settings"),
            "ai_model":             (self._ai_model_combo,       "combo_findText",    "ai_settings"),
            "ai_timeout":           (self._ai_timeout,            "value",             "ai_settings"),
            "ai_temperature":       (self._ai_temperature,        "value_scale100",    "ai_settings"),
            "ai_max_tokens":        (self._ai_max_tokens,         "value",             "ai_settings"),
            "ai_top_p":             (self._ai_top_p,              "value_scale100",    "ai_settings"),
            "ai_frequency_penalty": (self._ai_frequency_penalty,  "value_scale100",    "ai_settings"),
            "ai_presence_penalty":  (self._ai_presence_penalty,   "value_scale100",    "ai_settings"),
            "ai_seed":              (self._ai_seed_edit,          "text_int",          "ai_settings"),
            # AI 翻译行为
            "ai_target_lang":       (self._ai_target_lang_combo,  "combo_findText",    "ai_translate"),
            "ai_translation_prompt":(self._ai_prompt_edit,        "plainText",         "ai_translate"),
            "ai_confirm_before_translate":(self._ai_confirm_before, "checked",         "ai_translate"),
            "ai_stream_thinking":   (self._ai_stream_thinking,    "checked",           "ai_translate"),
            "text_translation_prompt":(self._text_translation_prompt_edit, "plainText", "ai_translate"),
            # 常规
            "app_mode":             (self._app_mode_combo,        "combo_findData",    "general"),
            "close_to_tray":        (self._close_to_tray_combo,   "combo_close_to_tray","general"),
            "notify_copy":          (self._notify_copy,           "checked",           "general"),
            "notify_save_success":  (self._notify_save_success,   "checked",           "general"),
            "notify_save_fail":     (self._notify_save_fail,      "checked",           "general"),
            "notify_capture_fail":  (self._notify_capture_fail,   "checked",           "general"),
            "notify_hotkey_fail":   (self._notify_hotkey_fail,    "checked",           "general"),
            "notify_translate_success":(self._notify_translate_success, "checked",      "general"),
            "notify_translate_fail":(self._notify_translate_fail, "checked",           "general"),
            "notify_ocr_fail":      (self._notify_ocr_fail,       "checked",           "general"),
            "notify_lang_download": (self._notify_lang_download,  "checked",           "general"),
            # 开发者选项
            "log_enabled":          (self._log_enabled,           "checked",           "general"),
        }

    # ==================================================== Tab builders

    def _build_screenshot_tab(self, settings) -> QWidget:
        w = QWidget()
        f = QFormLayout(w)
        self._hotkey_edit = QKeySequenceEdit(QKeySequence(settings.hotkey))
        f.addRow("截图快捷键", self._hotkey_edit)

        toolbar_w, self._toolbar_opacity = _make_slider_row(settings.toolbar_opacity, 1, 100)
        f.addRow("工具条不透明度", toolbar_w)

        _add_sep(f)
        # 外部十字准星
        self._crosshair_color_picker = ColorPickerButton(settings.crosshair_color)
        f.addRow("全屏十字线颜色", self._crosshair_color_picker)

        ch_alpha_w, self._crosshair_alpha = _make_slider_row(settings.crosshair_alpha, 10, 100)
        f.addRow("全屏十字线不透明度", ch_alpha_w)

        ch_thick_w, self._crosshair_thickness = _make_slider_row(
            settings.crosshair_thickness, 1, 10, " px"
        )
        f.addRow("全屏十字线粗细", ch_thick_w)

        self._crosshair_invert = QCheckBox("反色十字线")
        self._crosshair_invert.setChecked(settings.crosshair_invert)
        self._crosshair_invert.toggled.connect(self._toggle_crosshair_widgets)
        f.addRow("", self._crosshair_invert)

        self._crosshair_enabled = QCheckBox("显示全屏十字线")
        self._crosshair_enabled.setChecked(settings.crosshair_enabled)
        f.addRow("", self._crosshair_enabled)

        # 反色开启时禁用颜色和不透明度控件
        self._crosshair_color_widgets = [
            self._crosshair_color_picker, ch_alpha_w,
        ]
        self._toggle_crosshair_widgets(settings.crosshair_invert)

        _add_sep(f)
        # 系统鼠标光标
        self._cursor_enabled = QCheckBox("启用系统十字光标")
        self._cursor_enabled.setChecked(settings.cursor_enabled)
        f.addRow("", self._cursor_enabled)

        _add_sep(f)
        reset_btn = QPushButton("恢复本页默认")
        reset_btn.clicked.connect(self._reset_screenshot_tab)
        reset_row = QHBoxLayout()
        reset_row.addStretch()
        reset_row.addWidget(reset_btn)
        f.addRow("", reset_row)
        return w

    def _build_magnifier_tab(self, settings) -> QWidget:
        w = QWidget()
        f = QFormLayout(w)

        self._magnifier_enabled = QCheckBox("启用像素放大镜")
        self._magnifier_enabled.setChecked(settings.magnifier_enabled)
        self._magnifier_enabled.toggled.connect(self._toggle_magnifier_widgets)
        f.addRow("", self._magnifier_enabled)

        # 放大倍率
        zoom_w = QWidget()
        zoom_row = QHBoxLayout(zoom_w)
        zoom_row.setContentsMargins(0, 0, 0, 0)
        self._magnifier_zoom = QSlider(Qt.Orientation.Horizontal)
        self._magnifier_zoom.setRange(40, 200)
        self._magnifier_zoom.setValue(int(settings.magnifier_zoom * 10))
        zoom_label = QLabel(f"{self._magnifier_zoom.value() / 10:.1f}×")
        zoom_label.setFixedWidth(55)
        self._magnifier_zoom.valueChanged.connect(lambda v: zoom_label.setText(f"{v / 10:.1f}×"))
        zoom_row.addWidget(self._magnifier_zoom)
        zoom_row.addWidget(zoom_label)
        f.addRow("放大倍率", zoom_w)

        self._magnifier_wheel_zoom = QCheckBox(
            "允许鼠标滚轮实时调整放大倍率\n（启用后固定倍率失效）"
        )
        self._magnifier_wheel_zoom.setChecked(settings.magnifier_wheel_zoom)
        self._magnifier_wheel_zoom.toggled.connect(self._toggle_wheel_zoom_widgets)
        f.addRow("", self._magnifier_wheel_zoom)

        size_w, self._magnifier_size = _make_slider_row(settings.magnifier_size, 5, 30, " px")
        f.addRow("源区域半径", size_w)

        _add_sep(f)
        # 放大镜内十字线
        self._cross_color_picker = ColorPickerButton(settings.magnifier_cross_color)
        f.addRow("十字线颜色", self._cross_color_picker)

        alpha_w, self._magnifier_cross_alpha = _make_slider_row(
            settings.magnifier_cross_alpha, 30, 100
        )
        f.addRow("十字线不透明度", alpha_w)

        cross_thick_w, self._magnifier_cross_thickness = _make_slider_row(
            settings.magnifier_cross_thickness, 1, 10, " px"
        )
        f.addRow("十字线粗细", cross_thick_w)

        self._magnifier_cross_invert = QCheckBox("反色十字线")
        self._magnifier_cross_invert.setChecked(settings.magnifier_cross_invert)
        self._magnifier_cross_invert.toggled.connect(self._toggle_mag_cross_widgets)
        f.addRow("", self._magnifier_cross_invert)

        # 反色开启时禁用颜色和不透明度控件
        self._mag_cross_color_widgets = [self._cross_color_picker, alpha_w]
        self._toggle_mag_cross_widgets(settings.magnifier_cross_invert)

        _add_sep(f)
        # 像素网格
        self._grid_enabled = QCheckBox("绘制像素网格线")
        self._grid_enabled.setChecked(settings.grid_enabled)
        self._grid_enabled.toggled.connect(self._toggle_grid_widgets)
        f.addRow("", self._grid_enabled)

        self._grid_color_picker = ColorPickerButton(settings.grid_color)
        f.addRow("网格线颜色", self._grid_color_picker)

        grid_alpha_w, self._grid_alpha = _make_slider_row(settings.grid_alpha, 1, 100)
        f.addRow("网格线不透明度", grid_alpha_w)
        self._grid_sub_widgets = [self._grid_color_picker, grid_alpha_w]
        self._toggle_grid_widgets(settings.grid_enabled)

        _add_sep(f)
        # 屏幕边缘策略
        self._edge_mode_combo = QComboBox()
        self._edge_mode_combo.addItems(["裁剪到屏幕边缘", "保持固定大小（填充边缘）"])
        self._edge_mode_combo.setCurrentIndex(0 if settings.edge_mode == "crop" else 1)
        self._edge_mode_combo.currentIndexChanged.connect(
            lambda idx: self._toggle_edge_color_widgets(idx == 1)
        )
        f.addRow("屏幕边缘策略", self._edge_mode_combo)

        self._edge_color_picker = ColorPickerButton(settings.edge_color)
        self._edge_color_label = QLabel("填充颜色")
        f.addRow(self._edge_color_label, self._edge_color_picker)
        self._edge_color_widgets = [self._edge_color_label, self._edge_color_picker]
        self._toggle_edge_color_widgets(settings.edge_mode == "pad")

        _add_sep(f)
        # 倍率标签
        self._zoom_label_enabled = QCheckBox("显示放大倍率标签")
        self._zoom_label_enabled.setChecked(settings.zoom_label_enabled)
        self._zoom_label_enabled.toggled.connect(self._toggle_zoom_label_widgets)
        f.addRow("", self._zoom_label_enabled)

        self._zoom_label_color_picker = ColorPickerButton(settings.zoom_label_color)
        f.addRow("倍率标签颜色", self._zoom_label_color_picker)

        zl_alpha_w, self._zoom_label_alpha = _make_slider_row(
            settings.zoom_label_alpha, 10, 100
        )
        f.addRow("倍率标签不透明度", zl_alpha_w)
        self._zoom_label_sub_widgets = [self._zoom_label_color_picker, zl_alpha_w]
        self._toggle_zoom_label_widgets(settings.zoom_label_enabled)

        # magnifier sub widgets (zoom_w shared across tabs)
        self._magnifier_sub_widgets = [zoom_w, size_w,
                                       self._cross_color_picker, alpha_w,
                                       self._magnifier_wheel_zoom]
        self._wheel_zoom_sub_widgets = [zoom_w]
        self._toggle_wheel_zoom_widgets(settings.magnifier_wheel_zoom)
        self._toggle_magnifier_widgets(settings.magnifier_enabled)

        _add_sep(f)
        reset_btn = QPushButton("恢复本页默认")
        reset_btn.clicked.connect(self._reset_magnifier_tab)
        reset_row = QHBoxLayout()
        reset_row.addStretch()
        reset_row.addWidget(reset_btn)
        f.addRow("", reset_row)
        return w

    def _build_labels_tab(self, settings) -> QWidget:
        w = QWidget()
        f = QFormLayout(w)

        # 坐标标签
        self._coord_label_enabled = QCheckBox("显示屏幕坐标标签")
        self._coord_label_enabled.setChecked(settings.coord_label_enabled)
        self._coord_label_enabled.toggled.connect(self._toggle_coord_label_widgets)
        f.addRow("", self._coord_label_enabled)

        self._coord_label_text_color_picker = ColorPickerButton(settings.coord_label_text_color)
        f.addRow("坐标文字颜色", self._coord_label_text_color_picker)

        self._coord_label_bg_color_picker = ColorPickerButton(settings.coord_label_bg_color)
        f.addRow("坐标背景颜色", self._coord_label_bg_color_picker)

        cl_alpha_w, self._coord_label_bg_alpha = _make_slider_row(
            settings.coord_label_bg_alpha, 10, 100
        )
        f.addRow("坐标背景不透明度", cl_alpha_w)
        self._coord_label_sub_widgets = [
            self._coord_label_text_color_picker,
            self._coord_label_bg_color_picker, cl_alpha_w,
        ]
        self._toggle_coord_label_widgets(settings.coord_label_enabled)

        _add_sep(f)
        # 像素颜色标签
        self._color_label_enabled = QCheckBox("显示像素颜色标签")
        self._color_label_enabled.setChecked(settings.color_label_enabled)
        self._color_label_enabled.toggled.connect(self._toggle_color_label_widgets)
        f.addRow("", self._color_label_enabled)

        self._color_label_text_color_picker = ColorPickerButton(settings.color_label_text_color)
        f.addRow("颜色标签文字颜色", self._color_label_text_color_picker)

        self._color_label_bg_color_picker = ColorPickerButton(settings.color_label_bg_color)
        f.addRow("颜色标签背景颜色", self._color_label_bg_color_picker)

        col_alpha_w, self._color_label_bg_alpha = _make_slider_row(
            settings.color_label_bg_alpha, 10, 100
        )
        f.addRow("颜色标签背景不透明度", col_alpha_w)

        # 颜色显示格式
        self._color_format_combo = QComboBox()
        self._color_format_combo.addItems(["RGB", "十六进制 (Hex)"])
        self._color_format_combo.setCurrentIndex(0 if settings.color_format == "rgb" else 1)
        f.addRow("颜色显示格式", self._color_format_combo)

        _add_sep(f)
        # 复制颜色快捷键
        self._copy_color_key_edit = QKeySequenceEdit(QKeySequence(settings.copy_color_key))
        f.addRow("复制颜色快捷键", self._copy_color_key_edit)

        # 复制选项
        self._copy_hex_prefix = QCheckBox("Hex 复制时带 # 号")
        self._copy_hex_prefix.setChecked(settings.copy_hex_prefix)
        f.addRow("", self._copy_hex_prefix)

        self._copy_rgb_prefix = QCheckBox("RGB 复制时带 rgb() 包裹")
        self._copy_rgb_prefix.setChecked(settings.copy_rgb_prefix)
        f.addRow("", self._copy_rgb_prefix)

        self._color_label_sub_widgets = [
            self._color_label_text_color_picker, self._color_label_bg_color_picker,
            col_alpha_w, self._color_format_combo, self._copy_color_key_edit,
            self._copy_hex_prefix, self._copy_rgb_prefix,
        ]
        self._toggle_color_label_widgets(settings.color_label_enabled)

        _add_sep(f)
        reset_btn = QPushButton("恢复本页默认")
        reset_btn.clicked.connect(self._reset_labels_tab)
        reset_row = QHBoxLayout()
        reset_row.addStretch()
        reset_row.addWidget(reset_btn)
        f.addRow("", reset_row)
        return w

    def _build_storage_tab(self, settings) -> QWidget:
        w = QWidget()
        f = QFormLayout(w)

        dir_widget = QWidget()
        dir_row = QHBoxLayout(dir_widget)
        dir_row.setContentsMargins(0, 0, 0, 0)
        self._dir_edit = QLineEdit(settings.save_dir)
        browse_button = QPushButton("浏览…")
        browse_button.clicked.connect(self._browse_save_dir)
        dir_row.addWidget(self._dir_edit)
        dir_row.addWidget(browse_button)
        f.addRow("图片保存位置", dir_widget)

        self._format_combo = QComboBox()
        self._format_combo.addItems(["png", "jpg", "bmp"])
        idx = self._format_combo.findText(settings.save_format.lower())
        if idx >= 0:
            self._format_combo.setCurrentIndex(idx)
        f.addRow("图片保存格式", self._format_combo)

        config_widget = QWidget()
        config_row = QHBoxLayout(config_widget)
        config_row.setContentsMargins(0, 0, 0, 0)
        config_text = settings.config_dir if settings.config_dir else ""
        self._config_edit = QLineEdit(config_text)
        if not config_text:
            self._config_edit.setPlaceholderText("（程序所在目录）")
        config_browse = QPushButton("浏览…")
        config_browse.clicked.connect(self._browse_config_dir)
        config_reset = QPushButton("默认")
        config_reset.setToolTip("恢复为程序所在目录")
        config_reset.clicked.connect(self._reset_config_dir)
        config_row.addWidget(self._config_edit)
        config_row.addWidget(config_browse)
        config_row.addWidget(config_reset)
        f.addRow("配置保存位置", config_widget)

        # 临时文件目录
        temp_widget = QWidget()
        temp_row = QHBoxLayout(temp_widget)
        temp_row.setContentsMargins(0, 0, 0, 0)
        temp_text = settings.temp_dir if settings.temp_dir else ""
        self._temp_edit = QLineEdit(temp_text)
        if not temp_text:
            self._temp_edit.setPlaceholderText("（程序所在目录）")
        temp_browse = QPushButton("浏览…")
        temp_browse.clicked.connect(self._browse_temp_dir)
        temp_reset = QPushButton("默认")
        temp_reset.setToolTip("恢复为程序所在目录")
        temp_reset.clicked.connect(self._reset_temp_dir)
        temp_row.addWidget(self._temp_edit)
        temp_row.addWidget(temp_browse)
        temp_row.addWidget(temp_reset)
        f.addRow("临时文件目录", temp_widget)

        # ---- 临时文件清理策略 ----
        _add_sep(f)
        warning = QLabel(
            "⚠ 清理操作会删除临时目录中的所有文件，请勿将重要文件放入该目录。"
        )
        warning.setWordWrap(True)
        f.addRow(warning)

        self._cleanup_on_startup = QCheckBox("每次启动时自动清理临时文件")
        self._cleanup_on_startup.setChecked(settings.cleanup_on_startup)
        f.addRow("", self._cleanup_on_startup)

        self._cleanup_on_window_close = QCheckBox(
            "每次翻译 / OCR 窗口关闭后清理临时文件"
        )
        self._cleanup_on_window_close.setChecked(settings.cleanup_on_window_close)
        f.addRow("", self._cleanup_on_window_close)

        # 立即清理按钮
        self._clean_now_btn = QPushButton("立即清理临时文件")
        self._clean_now_btn.clicked.connect(self._on_clean_now)
        f.addRow("", self._clean_now_btn)

        _add_sep(f)
        reset_btn = QPushButton("恢复本页默认")
        reset_btn.clicked.connect(self._reset_storage_tab)
        reset_row = QHBoxLayout()
        reset_row.addStretch()
        reset_row.addWidget(reset_btn)
        f.addRow("", reset_row)
        return w

    def _build_general_tab(self, settings) -> QWidget:
        w = QWidget()
        f = QFormLayout(w)

        # 应用模式
        self._app_mode_combo = QComboBox()
        self._app_mode_combo.addItem("翻译模式 — 启动时显示文本翻译窗口", "translate")
        self._app_mode_combo.addItem("截图模式 — 启动时静默在后台", "screenshot")
        idx = 0 if settings.app_mode == "translate" else 1
        self._app_mode_combo.setCurrentIndex(idx)
        f.addRow("默认启动模式", self._app_mode_combo)

        _add_sep(f)

        # 关闭窗口行为
        self._close_to_tray_combo = QComboBox()
        self._close_to_tray_combo.addItems(["每次询问", "后台运行（最小化到托盘）", "退出程序"])
        if settings.close_to_tray is True:
            self._close_to_tray_combo.setCurrentIndex(1)
        elif settings.close_to_tray is False:
            self._close_to_tray_combo.setCurrentIndex(2)
        else:
            self._close_to_tray_combo.setCurrentIndex(0)
        f.addRow("关闭主窗口时", self._close_to_tray_combo)

        _add_sep(f)

        # 通知开关
        self._notify_copy = QCheckBox("复制到剪贴板时显示通知")
        self._notify_copy.setChecked(settings.notify_copy)
        f.addRow("", self._notify_copy)

        self._notify_save_success = QCheckBox("保存到文件成功时显示通知")
        self._notify_save_success.setChecked(settings.notify_save_success)
        f.addRow("", self._notify_save_success)

        self._notify_save_fail = QCheckBox("保存到文件失败时显示通知")
        self._notify_save_fail.setChecked(settings.notify_save_fail)
        f.addRow("", self._notify_save_fail)

        self._notify_capture_fail = QCheckBox("屏幕抓取失败时显示通知")
        self._notify_capture_fail.setChecked(settings.notify_capture_fail)
        f.addRow("", self._notify_capture_fail)

        self._notify_hotkey_fail = QCheckBox("快捷键注册失败时显示通知")
        self._notify_hotkey_fail.setChecked(settings.notify_hotkey_fail)
        f.addRow("", self._notify_hotkey_fail)

        self._notify_translate_success = QCheckBox("AI 翻译成功时显示通知")
        self._notify_translate_success.setChecked(settings.notify_translate_success)
        f.addRow("", self._notify_translate_success)

        self._notify_translate_fail = QCheckBox("AI 翻译失败时显示通知")
        self._notify_translate_fail.setChecked(settings.notify_translate_fail)
        f.addRow("", self._notify_translate_fail)

        self._notify_ocr_fail = QCheckBox("OCR 识别失败时显示通知")
        self._notify_ocr_fail.setChecked(settings.notify_ocr_fail)
        f.addRow("", self._notify_ocr_fail)

        self._notify_lang_download = QCheckBox("语言包下载成功/失败时显示通知")
        self._notify_lang_download.setChecked(settings.notify_lang_download)
        f.addRow("", self._notify_lang_download)

        _add_sep(f)

        # 开发者选项
        self._log_enabled = QCheckBox("启用控制台日志输出（开发者选项，重启生效）")
        self._log_enabled.setChecked(settings.log_enabled)
        f.addRow("", self._log_enabled)

        _add_sep(f)
        reset_btn = QPushButton("恢复本页默认")
        reset_btn.clicked.connect(self._reset_general_tab)
        reset_row = QHBoxLayout()
        reset_row.addStretch()
        reset_row.addWidget(reset_btn)
        f.addRow("", reset_row)
        return w

    # ==================================================== Toggle helpers

    def _toggle_magnifier_widgets(self, enabled):
        for w in self._magnifier_sub_widgets:
            w.setEnabled(enabled)

    def _toggle_wheel_zoom_widgets(self, enabled):
        for w in self._wheel_zoom_sub_widgets:
            w.setEnabled(not enabled)

    def _toggle_grid_widgets(self, enabled):
        for w in self._grid_sub_widgets:
            w.setEnabled(enabled)

    def _toggle_edge_color_widgets(self, enabled):
        for w in self._edge_color_widgets:
            w.setVisible(enabled)

    def _toggle_zoom_label_widgets(self, enabled):
        for w in self._zoom_label_sub_widgets:
            w.setEnabled(enabled)

    def _toggle_coord_label_widgets(self, enabled):
        for w in self._coord_label_sub_widgets:
            w.setEnabled(enabled)

    def _toggle_color_label_widgets(self, enabled):
        for w in self._color_label_sub_widgets:
            w.setEnabled(enabled)

    def _toggle_mag_cross_widgets(self, invert: bool):
        """反色准星开启时禁用放大镜标签页的颜色和不透明度控件。"""
        for w in self._mag_cross_color_widgets:
            w.setEnabled(not invert)

    def _toggle_crosshair_widgets(self, invert: bool):
        """反色十字线开启时禁用全屏十字线颜色和不透明度控件。"""
        for w in self._crosshair_color_widgets:
            w.setEnabled(not invert)

    # ==================================================== Actions

    def _browse_save_dir(self):
        d = QFileDialog.getExistingDirectory(self, "选择图片保存目录", self._dir_edit.text())
        if d:
            self._dir_edit.setText(d)

    def _browse_config_dir(self):
        d = QFileDialog.getExistingDirectory(self, "选择配置保存目录", self._config_edit.text())
        if d:
            self._config_edit.setText(d)

    def _reset_config_dir(self):
        self._config_edit.clear()
        self._config_edit.setPlaceholderText("（程序所在目录）")

    def _browse_temp_dir(self):
        d = QFileDialog.getExistingDirectory(self, "选择临时文件目录", self._temp_edit.text())
        if d:
            self._temp_edit.setText(d)

    def _reset_temp_dir(self):
        self._temp_edit.clear()
        self._temp_edit.setPlaceholderText("（程序所在目录）")

    def _on_clean_now(self):
        """立即清理临时目录中的所有文件。"""
        temp_dir = self._temp_edit.text().strip()
        if not temp_dir:
            from ..core.settings import _default_temp_dir
            temp_dir = _default_temp_dir()
        removed = cleanup_temp_dir(temp_dir)
        if removed > 0:
            self._notify_manager.info(
                "清理完成",
                f"已删除 {removed} 个临时文件。\n目录：{temp_dir}",
                parent=self,
            )
        else:
            self._notify_manager.info(
                "清理完成",
                f"临时目录中没有文件需要清理。\n目录：{temp_dir}",
                parent=self,
            )

    # ---- OCR 识别 ----

    def _build_ocr_tab(self, settings) -> QWidget:
        w = QWidget()
        f = QFormLayout(w)

        self._ocr_lang_list = _OcrLangManager(
            settings.ai_ocr_langs,
            notify_manager=self._notify_manager,
            settings_dialog_parent=self,
        )
        f.addRow("语言包管理", self._ocr_lang_list)

        _add_sep(f)
        reset_btn = QPushButton("恢复本页默认")
        reset_btn.clicked.connect(self._reset_ocr_tab)
        reset_row = QHBoxLayout()
        reset_row.addStretch()
        reset_row.addWidget(reset_btn)
        f.addRow("", reset_row)
        return w

    # ---- 翻译 ----

    def _build_ai_settings_tab(self, settings) -> QWidget:
        w = QWidget()
        f = QFormLayout(w)

        # ==================== AI 参数 ====================

        api_header = QLabel("AI 连接与参数")
        f.addRow(api_header)

        # 服务商（下拉选择，切换时联动 API 地址和模型）
        self._ai_provider_combo = QComboBox()
        for pid in list_providers():
            cfg = PROVIDER_CONFIGS[pid]
            self._ai_provider_combo.addItem(cfg["label"], pid)
        idx = self._ai_provider_combo.findData(settings.ai_provider)
        if idx >= 0:
            self._ai_provider_combo.setCurrentIndex(idx)
        self._ai_provider_combo.currentIndexChanged.connect(self._on_provider_changed)
        f.addRow("服务商", self._ai_provider_combo)

        # API Key（明文显示）
        self._ai_api_key_edit = QLineEdit(settings.ai_api_key)
        self._ai_api_key_edit.setPlaceholderText("sk-...")
        f.addRow("API Key", self._ai_api_key_edit)

        # API 地址
        self._ai_api_base_edit = QLineEdit(settings.ai_api_base)
        self._ai_api_base_edit.setPlaceholderText("https://api.deepseek.com/v1")
        f.addRow("API 地址", self._ai_api_base_edit)

        # 模型名称（可编辑下拉框 + 自动获取按钮）
        model_row = QHBoxLayout()
        self._ai_model_combo = QComboBox()
        self._ai_model_combo.setEditable(True)
        self._ai_model_combo.setMinimumWidth(200)
        self._ai_model_combo.setCurrentText(settings.ai_model)
        self._ai_model_combo.setPlaceholderText("输入模型名称或点击右侧按钮自动获取")
        model_row.addWidget(self._ai_model_combo)
        self._fetch_models_btn = QPushButton("获取模型列表")
        self._fetch_models_btn.setFixedWidth(100)
        self._fetch_models_btn.setFocusPolicy(Qt.FocusPolicy.NoFocus)
        self._fetch_models_btn.clicked.connect(self._on_fetch_models)
        model_row.addWidget(self._fetch_models_btn)
        f.addRow("模型", model_row)

        # 模型获取错误信息标签（初始隐藏）
        self._ai_error_label = QLabel("")
        self._ai_error_label.setWordWrap(True)
        self._ai_error_label.setVisible(False)
        f.addRow("", self._ai_error_label)

        # 超时时间
        timeout_w, self._ai_timeout = _make_slider_row(
            settings.ai_timeout, 5, 120, "秒"
        )
        f.addRow("请求超时", timeout_w)

        # 温度
        temp_w, self._ai_temperature = _make_decimal_slider_row(
            settings.ai_temperature, 0.0, 2.0
        )
        temp_row = QVBoxLayout()
        temp_row.addWidget(temp_w)
        temp_hint = QLabel("控制输出随机性。接近 0 翻译更稳定一致，接近 2 更富变化。翻译建议 ≤0.3")
        temp_row.addWidget(temp_hint)
        f.addRow("温度 (Temperature)", temp_row)

        # 最大输出 Token
        tk_w, self._ai_max_tokens = _make_slider_row(
            settings.ai_max_tokens, 256, 8192, ""
        )
        tk_row = QVBoxLayout()
        tk_row.addWidget(tk_w)
        tk_hint = QLabel("单次翻译最大输出长度。1 个中文字 ≈ 2 token。超出将被截断")
        tk_row.addWidget(tk_hint)
        f.addRow("最大输出 Token", tk_row)

        # Top P
        top_p_w, self._ai_top_p = _make_decimal_slider_row(
            settings.ai_top_p, 0.0, 1.0
        )
        top_p_row = QVBoxLayout()
        top_p_row.addWidget(top_p_w)
        top_p_hint = QLabel("核采样阈值。1.0 考虑所有可能词汇，越低输出越聚焦。翻译建议 1.0")
        top_p_row.addWidget(top_p_hint)
        f.addRow("Top P（核采样）", top_p_row)

        # 频率惩罚
        fp_w, self._ai_frequency_penalty = _make_decimal_slider_row(
            settings.ai_frequency_penalty, -2.0, 2.0
        )
        fp_row = QVBoxLayout()
        fp_row.addWidget(fp_w)
        fp_hint = QLabel("降低模型重复用词。正值惩罚已出现的词，翻译建议 0.0（不惩罚）")
        fp_row.addWidget(fp_hint)
        f.addRow("频率惩罚 (Frequency Penalty)", fp_row)

        # 存在惩罚
        pp_w, self._ai_presence_penalty = _make_decimal_slider_row(
            settings.ai_presence_penalty, -2.0, 2.0
        )
        pp_row = QVBoxLayout()
        pp_row.addWidget(pp_w)
        pp_hint = QLabel("鼓励引入新话题。正值惩罚已讨论过的内容，翻译建议 0.0（不惩罚）")
        pp_row.addWidget(pp_hint)
        f.addRow("存在惩罚 (Presence Penalty)", pp_row)

        # 随机种子
        seed_w = QWidget()
        seed_row = QHBoxLayout(seed_w)
        seed_row.setContentsMargins(0, 0, 0, 0)
        self._ai_seed_edit = QLineEdit(str(settings.ai_seed))
        self._ai_seed_edit.setPlaceholderText("0（默认随机）")
        self._ai_seed_edit.setMaximumWidth(120)
        seed_row.addWidget(self._ai_seed_edit)
        seed_row.addStretch()
        seed_hint_row = QVBoxLayout()
        seed_hint_row.addWidget(seed_w)
        seed_hint = QLabel("相同种子 + 相同参数 = 相同结果。设为 0 表示每次随机。调试/对比翻译时有用")
        seed_hint_row.addWidget(seed_hint)
        f.addRow("随机种子 (Seed)", seed_hint_row)

        _add_sep(f)
        reset_btn = QPushButton("恢复本页默认")
        reset_btn.clicked.connect(self._reset_ai_settings_tab)
        reset_row = QHBoxLayout()
        reset_row.addStretch()
        reset_row.addWidget(reset_btn)
        f.addRow("", reset_row)
        return w

    def _build_ai_translate_tab(self, settings) -> QWidget:
        w = QWidget()
        f = QFormLayout(w)

        # ==================== 翻译设置 ====================

        # 默认翻译目标语言
        self._ai_target_lang_combo = QComboBox()
        self._ai_target_lang_combo.setEditable(True)
        self._ai_target_lang_combo.addItems([
            "简体中文", "繁体中文（台湾）", "繁体中文（香港）",
            "英语", "日语", "韩语", "法语", "德语",
            "西班牙语", "葡萄牙语", "俄语", "阿拉伯语",
            "泰语", "越南语",
        ])
        idx = self._ai_target_lang_combo.findText(settings.ai_target_lang)
        if idx >= 0:
            self._ai_target_lang_combo.setCurrentIndex(idx)
        else:
            self._ai_target_lang_combo.setCurrentText(settings.ai_target_lang)
        f.addRow("默认目标语言", self._ai_target_lang_combo)

        # 截图翻译确认
        self._ai_confirm_before = QCheckBox("翻译前确认目标语言（弹出窗口后不自动翻译，需手动点击）")
        self._ai_confirm_before.setChecked(settings.ai_confirm_before_translate)
        f.addRow("", self._ai_confirm_before)

        # 流式思考
        self._ai_stream_thinking = QCheckBox("流式推送 AI 思考过程（实时显示，推荐开启）")
        self._ai_stream_thinking.setChecked(settings.ai_stream_thinking)
        f.addRow("", self._ai_stream_thinking)

        _add_sep(f)

        trans_header = QLabel("提示词设置")
        f.addRow(trans_header)

        # 截图翻译提示词
        default_snip_prompt = Settings.defaults_dict().get("ai_translation_prompt", "")
        self._ai_prompt_edit = QTextEdit()
        self._ai_prompt_edit.setPlainText(settings.ai_translation_prompt)
        self._ai_prompt_edit.setPlaceholderText(default_snip_prompt)
        self._ai_prompt_edit.setFixedHeight(120)
        snip_prompt_label = QLabel("占位符 {target_lang} 会在翻译时替换为目标语言")
        snip_prompt_row = QVBoxLayout()
        snip_prompt_row.addWidget(self._ai_prompt_edit)
        snip_prompt_row.addWidget(snip_prompt_label)
        f.addRow("截图翻译提示词", snip_prompt_row)

        # 文本翻译提示词
        self._text_translation_prompt_edit = QTextEdit()
        self._text_translation_prompt_edit.setPlainText(settings.text_translation_prompt)
        self._text_translation_prompt_edit.setFixedHeight(120)
        text_prompt_label = QLabel(
            "占位符 {source_text} 替换为原文，{target_lang} 替换为目标语言，{scenario} 替换为翻译场景"
        )
        text_prompt_row = QVBoxLayout()
        text_prompt_row.addWidget(self._text_translation_prompt_edit)
        text_prompt_row.addWidget(text_prompt_label)
        f.addRow("文本翻译提示词", text_prompt_row)

        _add_sep(f)
        reset_btn = QPushButton("恢复本页默认")
        reset_btn.clicked.connect(self._reset_ai_translate_tab)
        reset_row = QHBoxLayout()
        reset_row.addStretch()
        reset_row.addWidget(reset_btn)
        f.addRow("", reset_row)
        return w

    # ---- 消息和通知 ----

    def accept(self):
        if self._hotkey_edit.keySequence().isEmpty():
            self._notify_manager.warn(
                "设置", "请先录入截图快捷键。", parent=self,
            )
            return
        if not self._dir_edit.text().strip():
            self._notify_manager.warn(
                "设置", "保存目录不能为空。", parent=self,
            )
            return
        super().accept()

    # ==== 统一导出（供 app.py 批量同步） ===========================

    def as_dict(self) -> dict:
        """将当前对话框中所有设置项导出为 dict。

        自动从 SETTING_DEFS 生成：遍历所有已注册 key，
        调用同名 getter 方法取值。新增设置项只需添加 getter + _reset_map 条目。
        """
        from ..core.settings import SETTING_DEFS
        result: dict = {}
        for d in SETTING_DEFS:
            getter = getattr(self, d.key, None)
            if getter is not None:
                result[d.key] = getter()
        # SETTING_DEFS 外字段（动态默认值，无 getter）
        result["save_dir"] = self.save_dir()
        return result

    # ==================================================== Getters

    def app_mode(self) -> str:
        return self._app_mode_combo.currentData()

    def hotkey(self) -> str:
        return self._hotkey_edit.keySequence().toString(QKeySequence.SequenceFormat.PortableText)

    def save_dir(self) -> str:
        return self._dir_edit.text().strip()

    def save_format(self) -> str:
        return self._format_combo.currentText()

    def toolbar_opacity(self) -> int:
        return self._toolbar_opacity.value()

    def config_dir(self) -> str | None:
        t = self._config_edit.text().strip()
        return t if t else None

    def temp_dir(self) -> str | None:
        t = self._temp_edit.text().strip()
        return t if t else None

    def close_to_tray(self) -> bool | None:
        """关闭窗口行为：True=后台, False=退出, None=询问。"""
        idx = self._close_to_tray_combo.currentIndex()
        return {0: None, 1: True, 2: False}[idx]

    # 放大镜
    def magnifier_enabled(self) -> bool:
        return self._magnifier_enabled.isChecked()

    def magnifier_zoom(self) -> float:
        return self._magnifier_zoom.value() / 10.0

    def magnifier_size(self) -> int:
        return self._magnifier_size.value()

    def magnifier_cross_color(self) -> str:
        return self._cross_color_picker.color()

    def magnifier_cross_alpha(self) -> int:
        return self._magnifier_cross_alpha.value()

    def magnifier_cross_thickness(self) -> int:
        return self._magnifier_cross_thickness.value()

    def magnifier_cross_invert(self) -> bool:
        return self._magnifier_cross_invert.isChecked()

    def magnifier_wheel_zoom(self) -> bool:
        return self._magnifier_wheel_zoom.isChecked()

    # 外部十字准星
    def crosshair_color(self) -> str:
        return self._crosshair_color_picker.color()

    def crosshair_alpha(self) -> int:
        return self._crosshair_alpha.value()

    def crosshair_thickness(self) -> int:
        return self._crosshair_thickness.value()

    def crosshair_invert(self) -> bool:
        return self._crosshair_invert.isChecked()

    def crosshair_enabled(self) -> bool:
        return self._crosshair_enabled.isChecked()

    # 系统光标
    def cursor_enabled(self) -> bool:
        return self._cursor_enabled.isChecked()

    # 像素网格
    def grid_enabled(self) -> bool:
        return self._grid_enabled.isChecked()

    def grid_color(self) -> str:
        return self._grid_color_picker.color()

    def grid_alpha(self) -> int:
        return self._grid_alpha.value()

    # 屏幕边缘
    def edge_mode(self) -> str:
        return "crop" if self._edge_mode_combo.currentIndex() == 0 else "pad"

    def edge_color(self) -> str:
        return self._edge_color_picker.color()

    # 倍率标签
    def zoom_label_enabled(self) -> bool:
        return self._zoom_label_enabled.isChecked()

    def zoom_label_color(self) -> str:
        return self._zoom_label_color_picker.color()

    def zoom_label_alpha(self) -> int:
        return self._zoom_label_alpha.value()

    # 坐标标签
    def coord_label_enabled(self) -> bool:
        return self._coord_label_enabled.isChecked()

    def coord_label_text_color(self) -> str:
        return self._coord_label_text_color_picker.color()

    def coord_label_bg_color(self) -> str:
        return self._coord_label_bg_color_picker.color()

    def coord_label_bg_alpha(self) -> int:
        return self._coord_label_bg_alpha.value()

    # 颜色标签
    def color_label_enabled(self) -> bool:
        return self._color_label_enabled.isChecked()

    def color_label_text_color(self) -> str:
        return self._color_label_text_color_picker.color()

    def color_label_bg_color(self) -> str:
        return self._color_label_bg_color_picker.color()

    def color_label_bg_alpha(self) -> int:
        return self._color_label_bg_alpha.value()

    def color_format(self) -> str:
        return "rgb" if self._color_format_combo.currentIndex() == 0 else "hex"

    def copy_color_key(self) -> str:
        return self._copy_color_key_edit.keySequence().toString(
            QKeySequence.SequenceFormat.PortableText
        )

    def copy_hex_prefix(self) -> bool:
        return self._copy_hex_prefix.isChecked()

    def copy_rgb_prefix(self) -> bool:
        return self._copy_rgb_prefix.isChecked()

    def notify_copy(self) -> bool:
        return self._notify_copy.isChecked()

    def notify_save_success(self) -> bool:
        return self._notify_save_success.isChecked()

    def notify_save_fail(self) -> bool:
        return self._notify_save_fail.isChecked()

    def notify_capture_fail(self) -> bool:
        return self._notify_capture_fail.isChecked()

    def notify_hotkey_fail(self) -> bool:
        return self._notify_hotkey_fail.isChecked()

    def notify_translate_success(self) -> bool:
        return self._notify_translate_success.isChecked()

    def notify_translate_fail(self) -> bool:
        return self._notify_translate_fail.isChecked()

    def notify_ocr_fail(self) -> bool:
        return self._notify_ocr_fail.isChecked()

    def notify_lang_download(self) -> bool:
        return self._notify_lang_download.isChecked()

    def log_enabled(self) -> bool:
        return self._log_enabled.isChecked()

    # ---- AI 翻译 ----
    def ai_provider(self) -> str:
        return self._ai_provider_combo.currentData()

    def _on_provider_changed(self, _index: int):
        """服务商切换时：保存旧厂商 Key → 更新 API 地址 → 恢复新厂商 Key。"""
        new_pid = self._ai_provider_combo.currentData()
        if not new_pid or new_pid not in PROVIDER_CONFIGS:
            return

        # 保存当前厂商的 Key（切换前）
        if self._current_provider and self._current_provider != new_pid:
            self._provider_keys[self._current_provider] = self._ai_api_key_edit.text().strip()

        # 更新 API 地址
        self._ai_api_base_edit.setText(PROVIDER_CONFIGS[new_pid]["base_url"])

        # 恢复新厂商的 Key（首次切换到该厂商时为空）
        if new_pid != self._current_provider:
            saved_key = self._provider_keys.get(new_pid, "")
            self._ai_api_key_edit.setText(saved_key)

        self._current_provider = new_pid

    def _on_fetch_models(self):
        """从当前服务商获取可用模型列表，填充到下拉框。"""
        api_key = self._ai_api_key_edit.text().strip()
        api_base = self._ai_api_base_edit.text().strip()
        self._ai_error_label.setVisible(False)

        if not api_key:
            self._ai_error_label.setText("请先填写 API Key 后再获取模型列表。")
            self._ai_error_label.setVisible(True)
            self._ai_api_key_edit.setFocus()
            return
        if not api_base:
            self._ai_error_label.setText("请先填写 API 地址后再获取模型列表。")
            self._ai_error_label.setVisible(True)
            self._ai_api_base_edit.setFocus()
            return

        self._fetch_models_btn.setEnabled(False)
        self._fetch_models_btn.setText("获取中…")
        self._ai_model_combo.clear()

        try:
            models = list_models(api_key, api_base)
            if models:
                self._ai_model_combo.addItems(models)
                self._ai_model_combo.setCurrentIndex(0)
            else:
                self._ai_model_combo.addItem("（服务商未返回模型列表）")
                self._ai_model_combo.setCurrentIndex(0)
        except ConnectionError as e:
            self._ai_model_combo.addItem("（网络连接失败）")
            self._ai_model_combo.setCurrentIndex(0)
            self._ai_error_label.setText(
                f"网络连接失败，请检查网络。\n地址：{api_base}\n错误：{e}"
            )
            self._ai_error_label.setVisible(True)
        except TimeoutError as e:
            self._ai_model_combo.addItem("（请求超时）")
            self._ai_model_combo.setCurrentIndex(0)
            self._ai_error_label.setText(
                f"请求超时，请稍后重试。\n地址：{api_base}\n错误：{e}"
            )
            self._ai_error_label.setVisible(True)
        except RuntimeError as e:
            msg = str(e)
            provider = self._ai_provider_combo.currentText()
            if "鉴权" in msg:
                hint = "API Key 鉴权失败，请确认 Key 是否正确"
            elif "额度" in msg:
                hint = "API 额度不足或受到频率限制"
            else:
                hint = "该服务商可能不支持自动获取模型列表，请手动输入模型名称"
            self._ai_model_combo.addItem("（获取失败）")
            self._ai_model_combo.setCurrentIndex(0)
            self._ai_error_label.setText(
                f"{hint}\n服务商：{provider}\n地址：{api_base}\n错误：{msg}"
            )
            self._ai_error_label.setVisible(True)
        finally:
            self._fetch_models_btn.setEnabled(True)
            self._fetch_models_btn.setText("获取模型列表")

    def ai_api_key(self) -> str:
        return self._ai_api_key_edit.text().strip()

    def ai_api_base(self) -> str:
        return self._ai_api_base_edit.text().strip()

    def ai_model(self) -> str:
        return self._ai_model_combo.currentText().strip()

    def ai_target_lang(self) -> str:
        return self._ai_target_lang_combo.currentText().strip()

    def ai_timeout(self) -> int:
        return self._ai_timeout.value()

    def ai_ocr_langs(self) -> str:
        return self._ocr_lang_list.get_selected_langs()

    def ai_translation_prompt(self) -> str:
        return self._ai_prompt_edit.toPlainText().strip()

    def text_translation_prompt(self) -> str:
        return self._text_translation_prompt_edit.toPlainText().strip()

    def ai_confirm_before_translate(self) -> bool:
        return self._ai_confirm_before.isChecked()

    def ai_stream_thinking(self) -> bool:
        return self._ai_stream_thinking.isChecked()

    def ai_temperature(self) -> float:
        return self._ai_temperature.value() / 100.0

    def ai_max_tokens(self) -> int:
        return self._ai_max_tokens.value()

    def ai_top_p(self) -> float:
        return self._ai_top_p.value() / 100.0

    def ai_frequency_penalty(self) -> float:
        return self._ai_frequency_penalty.value() / 100.0

    def ai_presence_penalty(self) -> float:
        return self._ai_presence_penalty.value() / 100.0

    def ai_seed(self) -> int:
        try:
            return int(self._ai_seed_edit.text().strip())
        except ValueError:
            return 0

    # ---- 临时文件清理 ----
    def cleanup_on_startup(self) -> bool:
        return self._cleanup_on_startup.isChecked()

    def cleanup_on_window_close(self) -> bool:
        return self._cleanup_on_window_close.isChecked()

    # ==================================================== 恢复默认（数据驱动）

    def _confirm_reset(self, scope: str = "全部") -> bool:
        """弹出确认对话框。"""
        return self._notify_manager.confirm(
            "恢复默认设置",
            f"确定要将{scope}设置恢复为默认值吗？\n\n此操作不可撤销。",
            parent=self,
        )

    def _reset_keys(self, *keys):
        """按 key 列表批量恢复设置到默认值（数据驱动）。"""
        defaults = Settings.defaults_dict()
        for key in keys:
            widget, setter_type, _ = self._reset_map[key]
            v = defaults[key]
            if setter_type == "value":
                widget.setValue(v)
            elif setter_type == "checked":
                widget.setChecked(v)
            elif setter_type == "color":
                widget.set_color(v)
            elif setter_type == "keySequence":
                widget.setKeySequence(QKeySequence(v))
            elif setter_type == "text":
                widget.setText(v)
            elif setter_type == "plainText":
                widget.setPlainText(v)
            elif setter_type == "text_int":
                widget.setText(str(v))
            elif setter_type == "value_scale10":
                widget.setValue(int(v * 10))
            elif setter_type == "value_scale100":
                widget.setValue(int(v * 100))
            elif setter_type == "combo_edge_mode":
                widget.setCurrentIndex(0 if v == "crop" else 1)
            elif setter_type == "combo_color_format":
                widget.setCurrentIndex(0 if v == "rgb" else 1)
            elif setter_type == "combo_close_to_tray":
                widget.setCurrentIndex({None: 0, True: 1, False: 2}[v])
            elif setter_type == "combo_findText":
                idx = widget.findText(v)
                if idx >= 0:
                    widget.setCurrentIndex(idx)
                else:
                    widget.setCurrentText(v)
            elif setter_type == "combo_findData":
                idx = widget.findData(v)
                if idx >= 0:
                    widget.setCurrentIndex(idx)
            elif setter_type == "ocr_langs":
                widget._selected = set((v or "chi_sim+eng+jpn+kor").split("+"))
                widget._rebuild_ui()
            elif setter_type == "clear_edit":
                widget.clear()
                widget.setPlaceholderText("（程序所在目录）")

    def _on_reset_all(self):
        """恢复全部设置。"""
        if not self._confirm_reset("全部"):
            return
        self._reset_keys(*self._reset_map.keys())

    def _reset_screenshot_tab(self):
        if not self._confirm_reset("截图页面"):
            return
        self._reset_keys(*[k for k, (_, _, t) in self._reset_map.items() if t == "screenshot"])

    def _reset_magnifier_tab(self):
        if not self._confirm_reset("放大镜页面"):
            return
        self._reset_keys(*[k for k, (_, _, t) in self._reset_map.items() if t == "magnifier"])

    def _reset_labels_tab(self):
        if not self._confirm_reset("坐标与颜色页面"):
            return
        self._reset_keys(*[k for k, (_, _, t) in self._reset_map.items() if t == "labels"])

    def _reset_storage_tab(self):
        if not self._confirm_reset("存储页面"):
            return
        self._reset_keys(*[k for k, (_, _, t) in self._reset_map.items() if t == "storage"])

    def _reset_ocr_tab(self):
        if not self._confirm_reset("OCR 识别页面"):
            return
        self._reset_keys(*[k for k, (_, _, t) in self._reset_map.items() if t == "ocr"])

    def _reset_ai_settings_tab(self):
        if not self._confirm_reset("AI 设置页面"):
            return
        self._reset_keys(*[k for k, (_, _, t) in self._reset_map.items() if t == "ai_settings"])

    def _reset_ai_translate_tab(self):
        if not self._confirm_reset("翻译页面"):
            return
        self._reset_keys(*[k for k, (_, _, t) in self._reset_map.items() if t == "ai_translate"])

    def _reset_general_tab(self):
        if not self._confirm_reset("常规页面"):
            return
        self._reset_keys(*[k for k, (_, _, t) in self._reset_map.items() if t == "general"])
