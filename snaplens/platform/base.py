"""平台能力抽象接口。

所有平台实现（Windows/macOS/Linux）都遵守同一契约，
上层代码（app.py、ui/）只依赖本文件定义的接口，不感知具体平台。
"""
from typing import Callable

from PySide6.QtCore import QObject, Signal
from PySide6.QtGui import QGuiApplication


class HotkeyProvider(QObject):
    """全局热键。

    sequence_text 约定为 QKeySequence PortableText（如 "Ctrl+Alt+A"），
    各平台后端自行解析为本平台的键位表示。
    """

    triggered = Signal()

    def start(self, sequence_text: str) -> bool:
        """注册热键。成功返回 True；失败多为键位冲突或键值不支持。"""
        raise NotImplementedError

    def stop(self) -> None:
        """注销热键。"""
        raise NotImplementedError


class NullHotkeyProvider(HotkeyProvider):
    """尚未支持的平台上的占位实现：start 恒失败，由上层提示用户。"""

    def start(self, sequence_text: str) -> bool:
        return False

    def stop(self) -> None:
        pass


class WindowProvider:
    """顶层窗口枚举（用于“单击窗口截图”的命中检测）。

    enum_windows() 返回 [{"hwnd": 任意句柄, "rect": (l, t, r, b), "title": str}, ...]
    - rect：物理像素坐标（与截图像素坐标系一致）；
    - 顺序：Z 序自上而下，命中检测从头遍历取第一个包含鼠标点的窗口。
    """

    def enum_windows(self) -> list:
        raise NotImplementedError


class NullWindowProvider(WindowProvider):
    """尚未支持的平台上的占位实现：返回空列表，
    覆盖层因此永不高亮窗口，点选自动退化为整屏截图。"""

    def enum_windows(self) -> list:
        return []


class CursorProvider:
    """获取鼠标在虚拟桌面上的物理像素坐标。

    physical_position() 返回 (x: int, y: int) —— 供放大镜等需要像素级精确定位的功能使用。
    坐标原点为虚拟桌面左上角，单位是物理像素（与截图像素坐标系一致）。
    """

    def physical_position(self) -> tuple[int, int]:
        """返回当前鼠标的物理像素坐标 (x, y)。"""
        raise NotImplementedError


class NullCursorProvider(CursorProvider):
    """尚未支持的平台上的占位实现：用 Qt 逻辑坐标 × DPR 近似，
    在非整数 DPR 下会跳过部分物理像素。"""

    def physical_position(self) -> tuple[int, int]:
        from PySide6.QtGui import QCursor
        gpos = QCursor.pos()
        screen = QGuiApplication.screenAt(gpos)
        if screen is None:
            return 0, 0
        dpr = screen.devicePixelRatio()
        local = gpos - screen.geometry().topLeft()
        return round(local.x() * dpr), round(local.y() * dpr)


class CancelProvider:
    """截图取消拦截：通过原生事件监听 Esc 键，不依赖焦点/键盘抓取。

    上层代码调用 install(callback) 安装拦截，
    回调触发后由上层负责解除（调用 uninstall()）。
    """

    def install(self, callback: Callable[[], None]) -> bool:
        """安装原生 Esc 键拦截。成功返回 True。"""
        raise NotImplementedError

    def uninstall(self) -> None:
        """卸载拦截。"""
        raise NotImplementedError


class NullCancelProvider(CancelProvider):
    """尚未支持的平台上的占位实现：不安装任何拦截，
    取消依赖覆盖层自身的 keyPressEvent（需焦���/键盘抓取）。"""

    def install(self, callback: Callable[[], None]) -> bool:
        return False

    def uninstall(self) -> None:
        pass


class ClipCursorProvider:
    """限制光标移动范围：将鼠标物理移动约束在指定矩形内。

    用于截图时防止光标漂移到其他屏幕，提升多屏使用体验。
    """

    def clip_to_rect(self, left: int, top: int,
                     right: int, bottom: int) -> bool:
        """将光标移动限制在指定的物理像素矩形内。成功返回 True。"""
        raise NotImplementedError

    def release(self) -> None:
        """解除光标限制，恢复正常移动范围。"""
        raise NotImplementedError


class NullClipCursorProvider(ClipCursorProvider):
    """尚未支持的平台上的占位实现：不做任何限制。

    Wayland 等不允许客户端控制全局光标的环境也使用此实现。
    """

    def clip_to_rect(self, left: int, top: int,
                     right: int, bottom: int) -> bool:
        return False

    def release(self) -> None:
        pass
