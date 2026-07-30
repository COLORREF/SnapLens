"""通知通道实现：托盘、弹窗、日志三种通道。

每个通道负责将通知以特定方式呈现给用户或记录到系统。
所有通道方法均应在主线程调用（由 NotifyManager 保证）。
"""
import logging
from typing import Any

from PySide6.QtWidgets import QMessageBox, QDialog, QSystemTrayIcon, QWidget

from .defs import Channel, NotifyLevel

# ---- 日志通道 ----
_NOTIFY_LOGGER = logging.getLogger("snaplens.notify")


def _level_to_log(level: str) -> int:
    """NotifyLevel → logging 级别。"""
    _map = {
        NotifyLevel.INFO: logging.INFO,
        NotifyLevel.WARNING: logging.WARNING,
        NotifyLevel.ERROR: logging.ERROR,
    }
    return _map.get(level, logging.INFO)


def _level_to_icon(level: str) -> QSystemTrayIcon.MessageIcon:
    """NotifyLevel → QSystemTrayIcon 图标。"""
    _map = {
        NotifyLevel.INFO: QSystemTrayIcon.MessageIcon.Information,
        NotifyLevel.WARNING: QSystemTrayIcon.MessageIcon.Warning,
        NotifyLevel.ERROR: QSystemTrayIcon.MessageIcon.Critical,
    }
    return _map.get(level, QSystemTrayIcon.MessageIcon.Information)


def _level_to_msgbox_icon(level: str) -> QMessageBox.Icon:
    """NotifyLevel → QMessageBox 图标。"""
    _map = {
        NotifyLevel.INFO: QMessageBox.Icon.Information,
        NotifyLevel.WARNING: QMessageBox.Icon.Warning,
        NotifyLevel.ERROR: QMessageBox.Icon.Critical,
    }
    return _map.get(level, QMessageBox.Icon.Information)


# =====================================================================
# LogChannel
# =====================================================================

class LogChannel:
    """控制台日志通道。

    所有通知统一以 "[Notify][级别]" 前缀输出到 snaplens.notify logger。
    """

    @staticmethod
    def send(level: str, title: str, message: str) -> None:
        log_level = _level_to_log(level)
        _NOTIFY_LOGGER.log(log_level, "[Notify][%s] %s: %s",
                           level.upper(), title, message)


# =====================================================================
# TrayChannel
# =====================================================================

class TrayChannel:
    """系统托盘通知通道。

    通过 QSystemTrayIcon.showMessage() 在 Windows 右下角弹出通知，
    3 秒后自动消失，无需用户操作。

    tray 未绑定时自动降级为日志记录，不会崩溃。
    """

    def __init__(self):
        self._tray: QSystemTrayIcon | None = None

    def set_tray(self, tray: QSystemTrayIcon) -> None:
        """绑定系统托盘图标实例。"""
        self._tray = tray

    @property
    def available(self) -> bool:
        return self._tray is not None

    def send(self, level: str, title: str, message: str) -> bool:
        if self._tray is None:
            _NOTIFY_LOGGER.debug(
                "[Notify][FALLBACK] tray 未绑定，降级为日志: %s: %s",
                title, message,
            )
            return False
        icon = _level_to_icon(level)
        self._tray.showMessage(title, message, icon, 3000)
        return True


# =====================================================================
# DialogChannel
# =====================================================================

class DialogChannel:
    """模态弹窗通道。

    支持标准 QMessageBox（info / warn / confirm）和自定义 QDialog。
    内置去重机制：同一弹窗未关闭前不会重复弹出。

    注意：notify() 流程极少走 DIALOG 通道（主要用于设置校验等阻断场景），
    大部分弹窗需求通过 confirm/warn/info 等专用方法调用。
    """

    def __init__(self):
        self._default_parent: QWidget | None = None
        self._active_key: str | None = None  # 当前活跃弹窗的去重 key

    def set_default_parent(self, parent: QWidget | None) -> None:
        self._default_parent = parent

    # ---- 标准 QMessageBox ----

    def _effective_parent(self, parent: QWidget | None) -> QWidget | None:
        return parent if parent is not None else self._default_parent

    def send(self, level: str, title: str, message: str,
             parent: QWidget | None = None) -> None:
        """通过 notify() 调用时的模态弹窗（极少使用，去重保护）。"""
        dedup_key = f"notify:{title}"
        if self._active_key == dedup_key:
            return  # 同一通知的弹窗正显示中
        self._active_key = dedup_key
        try:
            QMessageBox(
                _level_to_msgbox_icon(level),
                title, message,
                QMessageBox.StandardButton.Ok,
                self._effective_parent(parent),
            ).exec()
        finally:
            if self._active_key == dedup_key:
                self._active_key = None

    def info(self, title: str, message: str,
             parent: QWidget | None = None) -> None:
        """通用信息弹窗（不受通知设置控制）。"""
        QMessageBox.information(
            self._effective_parent(parent), title, message,
        )

    def warn(self, title: str, message: str,
             parent: QWidget | None = None) -> None:
        """通用警告弹窗（不受通知设置控制）。"""
        QMessageBox.warning(
            self._effective_parent(parent), title, message,
        )

    def confirm(self, title: str, message: str,
                parent: QWidget | None = None) -> bool:
        """确认弹窗（不受通知设置控制），返回 True 表示用户选择"是"。"""
        reply = QMessageBox.question(
            self._effective_parent(parent), title, message,
            QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No,
        )
        return reply == QMessageBox.StandardButton.Yes

    # ---- 自定义 QDialog ----

    def custom(self, dialog: QDialog) -> int:
        """执行自定义 QDialog（模态），返回 QDialog.DialogCode。

        调用者负责创建 dialog 实例并设置 parent。
        """
        return dialog.exec()  # type: ignore[no-any-return]
