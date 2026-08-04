"""应用控制器：串联主窗口、托盘、全局热键、截图会话与设置。"""
import os
from datetime import datetime

from PySide6.QtCore import QObject, QCoreApplication
from PySide6.QtGui import QGuiApplication, QKeySequence, QPixmap

from .core.settings import Settings
from .log import log_error, log_info, set_enabled_levels
from .notify import NotifyManager
from .platform import create_hotkey_provider
from .ui.main_window import MainWindow
from .ui.pin import PinWindow
from .ui.settings_dialog import SettingsDialog
from .ui.setup_wizard import SetupWizard
from .ui.snip import SnipSession
from .ui.translate_window import TranslateWindow
from .ui.ocr_window import OcrWindow
from .ui.tray import TrayIcon


class AppController(QObject):
    """应用编排器：首次运行弹出引导向导，按模式显示/隐藏翻译窗口，驻留托盘。

    支持两种模式：
    - translate（翻译模式）：启动时显示文本翻译窗口
    - screenshot（截图模式）：后台静默运行，仅托盘图标
    两种模式均可通过快捷键截图，均可使用翻译功能。
    """

    def __init__(self, app, parent=None):
        super().__init__(parent)
        self._app = app
        self._session = None        # 进行中的截图会话（同时只允许一个）
        self._pins = []             # 存活的钉图窗口（防止被 GC）
        self._translate_win = None  # 翻译窗口引用
        self._ocr_win = None        # OCR 窗口引用
        self.main_window = None     # 翻译主窗口
        self.tray = None            # 托盘（引导完成后才创建）
        self.hotkey = None          # 热键（引导完成后才注册）
        self.cancelled = False      # 首次运行向导是否被取消

        # 通知管理器：提前创建（向导中的确认弹窗依赖它）
        self.notify_manager = NotifyManager(None)  # settings 尚未加载

        # 检测首次运行
        if not Settings.config_file_exists():
            self._run_setup_wizard()
        else:
            self.settings = Settings.load()
            self.notify_manager.set_settings(self.settings)
            self._init_after_settings()

    def _run_setup_wizard(self):
        """首次运行：弹出引导向导，用户完成后保存设置并继续初始化。"""
        wizard = SetupWizard(notify_manager=self.notify_manager)
        if wizard.exec() == SetupWizard.DialogCode.Accepted:
            self.settings = Settings(wizard.collect_settings())
            self.settings.save()
            self.notify_manager.set_settings(self.settings)
        else:
            # 用户取消或关闭向导，标记取消；托盘尚未创建无需清理
            self.cancelled = True
            return

        self._init_after_settings()

    def _init_after_settings(self):
        """配置加载完成后的统一初始化：托盘、主窗口、热键。"""
        # 托盘（设置就绪后才创建，避免引导阶段点击菜单崩溃）
        self.tray = TrayIcon(self)
        self.tray.snipRequested.connect(self.start_snip)
        self.tray.settingsRequested.connect(self.open_settings)
        self.tray.quitRequested.connect(self.quit)
        self.tray.modeSwitchRequested.connect(self._on_mode_switch)
        self.tray.translateRequested.connect(self._show_translate_window)
        self.tray.show()

        # 绑定通知管理器的托盘通道
        self.notify_manager.set_tray(self.tray)

        # 应用 OCR 路径设置（自定义 SDK bin / tessdata 目录）
        try:
            from .ocr import apply_settings
            apply_settings(
                sdk_bin_dir=self.settings.ocr_sdk_bin_dir,
                tessdata_dir=self.settings.ocr_tessdata_dir,
            )
        except Exception:
            pass  # OCR 模块可能尚未编译，不阻塞启动

        # 翻译主窗口（两种模式都创建，区别在于是否显示）
        self.main_window = MainWindow(
            self.settings, self.notify_manager,
            app_mode=self.settings.app_mode,
        )
        self.main_window._settings_btn.clicked.connect(self.open_settings)

        # 按模式决定是否显示主窗口
        if self.settings.app_mode == "translate":
            self.main_window.show()
        # screenshot 模式：主窗口已创建但隐藏，翻译功能随时可用

        # 同步托盘菜单中的模式提示
        self.tray.set_mode_text(self.settings.app_mode)

        # 绑定通知管理器的默认父窗口
        self.notify_manager.set_default_parent(self.main_window)

        self.hotkey = create_hotkey_provider(self)
        self.hotkey.triggered.connect(self.start_snip)
        self._apply_hotkey()
        self._apply_log_levels()

    # ------------------------------------------------------------ 截图流程
    def start_snip(self):
        if self._session is not None:
            return  # 已有截图会话进行中
        session = SnipSession(
            on_save=self.save_pixmap,
            on_pin=self.pin_pixmap,
            on_copy=self.copy_pixmap,
            on_translate=self.translate_pixmap,
            on_ocr=self.ocr_pixmap,
            settings=self.settings,
            parent=self,
        )
        session.finished.connect(self._on_session_finished)
        self._session = session
        if not session.start():
            self._session = None
            self.notify_manager.notify(
                "capture_fail", "SnapLens", "屏幕抓取失败，请重试。",
            )

    def _on_session_finished(self):
        if self._session is not None:
            self._session.deleteLater()
            self._session = None

    # ------------------------------------------------------------ 结果动作
    def save_pixmap(self, pixmap: QPixmap):
        """保存到设置中的默认目录，使用设置的图片格式。"""
        try:
            fmt = self.settings.save_format.lower()
            ext = f".{fmt}"
            os.makedirs(self.settings.save_dir, exist_ok=True)
            base = os.path.join(
                self.settings.save_dir,
                datetime.now().strftime("SnapLens_%Y%m%d_%H%M%S"),
            )
            path = base + ext
            index = 1
            while os.path.exists(path):
                index += 1
                path = f"{base}_{index}{ext}"
            if pixmap.save(path, fmt.upper()):
                self.notify_manager.notify(
                    "save_success", "SnapLens 截图", f"已保存：{path}",
                )
                return
        except OSError as e:
            log_error(f"保存截图失败: {e}")
        self.notify_manager.notify(
            "save_fail", "SnapLens 截图",
            "保存失败，请检查保存目录是否可写。",
        )

    def copy_pixmap(self, pixmap: QPixmap):
        QGuiApplication.clipboard().setPixmap(pixmap)
        self.notify_manager.notify(
            "copy", "SnapLens 截图", "已复制到剪贴板。",
        )

    def pin_pixmap(self, pixmap: QPixmap):
        pin = PinWindow(pixmap, self.settings.save_dir)
        pin.destroyed.connect(
            lambda _obj=None, w=pin: self._remove_pin(w)
        )
        self._pins.append(pin)
        pin.show()

    def _remove_pin(self, window):
        try:
            self._pins.remove(window)
        except ValueError:
            pass

    def translate_pixmap(self, pixmap: QPixmap):
        """打开 AI 翻译窗口。"""
        self._translate_win = TranslateWindow(
            pixmap, self.settings,
            auto_translate=not self.settings.ai_confirm_before_translate,
            notify_manager=self.notify_manager,
        )
        self._translate_win.closed.connect(self._on_translate_closed)
        self._translate_win.show()

    def _on_translate_closed(self):
        self._translate_win = None

    def ocr_pixmap(self, pixmap: QPixmap):
        """打开 OCR 识别窗口。"""
        self._ocr_win = OcrWindow(pixmap, self.settings,
                                 notify_manager=self.notify_manager)
        self._ocr_win.closed.connect(self._on_ocr_closed)
        self._ocr_win.show()

    def _on_ocr_closed(self):
        self._ocr_win = None

    # ------------------------------------------------------------ 设置
    def open_settings(self):
        dialog = SettingsDialog(self.settings, self.notify_manager)
        if not dialog.exec():
            return
        old_mode = self.settings.app_mode
        self.settings.update_from_dict(dialog.as_dict())
        self.settings.save()
        self._apply_hotkey()
        self._apply_log_levels()
        # 同步 OCR 路径设置
        try:
            from .ocr import apply_settings
            apply_settings(
                sdk_bin_dir=self.settings.ocr_sdk_bin_dir,
                tessdata_dir=self.settings.ocr_tessdata_dir,
            )
        except Exception:
            pass
        # 如果用户在设置中更改了应用模式，立即应用
        if self.settings.app_mode != old_mode:
            self.switch_mode(self.settings.app_mode)

    def _apply_hotkey(self):
        ok = self.hotkey.start(self.settings.hotkey)
        native = QKeySequence(self.settings.hotkey).toString(
            QKeySequence.SequenceFormat.NativeText
        )
        self.tray.set_hotkey_text(native)
        if not ok:
            self.notify_manager.notify(
                "hotkey_fail", "SnapLens 截图",
                f"快捷键 {native} 注册失败，可能被其它程序占用，"
                f"请在设置中更换。",
            )

    def _apply_log_levels(self):
        set_enabled_levels(
            debug=self.settings.log_debug_enabled,
            info=self.settings.log_info_enabled,
            warning=self.settings.log_warning_enabled,
            error=self.settings.log_error_enabled,
        )

    # ------------------------------------------------------------ 模式切换
    def _on_mode_switch(self):
        """托盘菜单触发：在翻译模式和截图模式之间切换。"""
        if self.settings.app_mode == "translate":
            self.switch_mode("screenshot")
        else:
            self.switch_mode("translate")

    def _show_translate_window(self):
        """托盘菜单触发：显示文本翻译窗口（不切换模式）。

        在截图模式下，用户可以随时通过托盘菜单打开文本翻译窗口
        进行翻译，无需切换到翻译模式。
        """
        self._bring_translate_window()

    def _bring_translate_window(self):
        """将翻译窗口提到前台，自动处理最小化状态。"""
        if self.main_window.isMinimized():
            self.main_window.showNormal()
        else:
            self.main_window.show()
        self.main_window.raise_()
        self.main_window.activateWindow()

    def switch_mode(self, mode: str):
        """切换应用模式并持久化。

        mode: "translate" | "screenshot"
        """
        if mode not in ("translate", "screenshot"):
            return
        if self.settings.app_mode == mode:
            return  # 已是目标模式

        self.settings.app_mode = mode
        self.settings.save()

        # 同步 MainWindow 的内部模式（影响关闭行为）
        self.main_window._app_mode = mode

        if mode == "translate":
            self._bring_translate_window()
        else:
            self.main_window.hide()

        # 同步托盘菜单文字
        self.tray.set_mode_text(mode)

        log_info(f"应用模式切换为: {mode}")

    # ------------------------------------------------------------ 退出
    def quit(self):
        if not self.notify_manager.confirm("退出确认", "确定要退出 SnapLens 吗？"):
            return
        self.hotkey.stop()
        self.tray.hide()
        QCoreApplication.quit()
