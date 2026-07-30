"""SnapLens 通知系统。

提供统一的通知分发能力，支持三种通道：
- TrayChannel: 系统托盘通知（Windows 右下角弹窗）
- DialogChannel: 模态弹窗（QMessageBox / 自定义 QDialog）
- LogChannel: 控制台日志（logging 模块）

快速使用：
    from snaplens.notify import NotifyManager, Channel, NotifyLevel

    nm = NotifyManager(settings)
    nm.set_tray(tray_icon)

    # 设置控制的通知
    nm.notify("save_success", title="SnapLens", message="已保存")

    # 不受设置控制的弹窗
    if nm.confirm("确认", "确定？"):
        ...
    nm.warn("警告", "格式错误")
    nm.info("完成", "操作成功")
"""
from .manager import NotifyManager
from .defs import Channel, NotifyLevel, NotifyDef, NOTIFY_DEFS

__all__ = [
    "NotifyManager",
    "Channel",
    "NotifyLevel",
    "NotifyDef",
    "NOTIFY_DEFS",
]
