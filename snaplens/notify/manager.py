"""通知管理器 — 通知系统的统一入口。

职责：
1. 提供 notify() 方法：设置控制的通知（托盘 + 日志），根据 NOTIFY_DEFS 路由。
2. 提供 confirm/warn/info 方法：不受设置控制的弹窗（操作确认 / 校验失败）。
3. 提供 ask_custom() 方法：执行自定义 QDialog 并返回结果。
4. 管理三个通道（Tray/Dialog/Log）的生命周期。

所有 UI 操作均在主线程执行（由调用方保证，通常来自 Qt 信号槽）。
"""
from __future__ import annotations

from typing import Any

from .channel import DialogChannel, LogChannel, TrayChannel
from .defs import Channel, NotifyLevel, get_def


class NotifyManager:
    """通知管理器。

    用法：
        # 启动阶段：先用 None 初始化（settings 尚未加载）
        nm = NotifyManager(None)
        nm.set_tray(tray_icon)

        # 配置加载完成后：绑定真实 settings
        nm.set_settings(settings)

        # 设置控制的通知
        nm.notify("save_success", title="SnapLens", message="已保存：...")

        # 不受设置控制的弹窗
        if nm.confirm("确认删除", "确定要删除吗？"):
            ...
        nm.warn("设置", "快捷键不能为空")
        nm.info("完成", "清理了 5 个文件")

        # 自定义弹窗
        result = nm.ask_custom(my_dialog)
    """

    def __init__(self, settings):
        self._settings = settings
        self._tray_channel = TrayChannel()
        self._dialog_channel = DialogChannel()
        self._log_channel = LogChannel()

    # ---------------------------------------------------------------- 绑定
    def set_tray(self, tray) -> None:
        """绑定系统托盘图标（TrayChannel 初始化较晚时调用）。"""
        self._tray_channel.set_tray(tray)

    def set_default_parent(self, parent) -> None:
        """设置弹窗的默认父窗口。"""
        self._dialog_channel.set_default_parent(parent)

    def set_settings(self, settings) -> None:
        """（重新）绑定设置对象。

        用于启动阶段延迟加载：NotifyManager 先以 None 初始化，
        配置加载完成后通过此方法绑定真实的 Settings 实例。
        """
        self._settings = settings

    # ---------------------------------------------------------------- 设置控制的通知
    def notify(self, notify_id: str, title: str, message: str) -> None:
        """发送设置控制的通知。

        根据 NOTIFY_DEFS 中对应 id 的配置：
        - 检查关联设置项是否允许此通知；
        - 按定义的通道分发（TRAY / DIALOG / LOG）。

        未知 notify_id 时降级为仅 LOG 通道（WARNING 级别）。
        """
        ndef = get_def(notify_id)
        if ndef is None:
            # 未知通知类型：仅日志，不崩溃
            self._log_channel.send(
                NotifyLevel.WARNING,
                title,
                f"{message} [未注册的通知类型: {notify_id}]",
            )
            return

        # 检查设置开关（settings 未加载时允许所有通知通过）
        if self._settings is not None and ndef.setting is not None:
            if not getattr(self._settings, ndef.setting, True):
                return  # 用户关闭了此通知

        # 多通道分发
        channels = ndef.channels
        if Channel.LOG in channels and (self._settings is None
                                        or (self._settings.log_debug_enabled or self._settings.log_info_enabled
                                            or self._settings.log_warning_enabled or self._settings.log_error_enabled)):
            self._log_channel.send(ndef.level, title, message)
        if Channel.TRAY in channels:
            self._tray_channel.send(ndef.level, title, message)
        if Channel.DIALOG in channels:
            self._dialog_channel.send(ndef.level, title, message)

    # ---------------------------------------------------------------- 不受设置控制的弹窗
    def confirm(self, title: str, message: str,
                parent=None) -> bool:
        """确认对话框，返回 True 表示用户选择"是"。"""
        return self._dialog_channel.confirm(title, message, parent)

    def warn(self, title: str, message: str,
             parent=None) -> None:
        """警告对话框（模态）。"""
        self._dialog_channel.warn(title, message, parent)

    def info(self, title: str, message: str,
             parent=None) -> None:
        """信息对话框（模态）。"""
        self._dialog_channel.info(title, message, parent)

    def ask_custom(self, dialog) -> int:
        """执行自定义 QDialog 并返回 QDialog.DialogCode。"""
        return self._dialog_channel.custom(dialog)
