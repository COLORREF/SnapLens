"""平台相关能力的统一入口。

接入新平台（macOS/Linux）时：
1. 在 platform/ 下新建实现模块，继承 base.HotkeyProvider / base.WindowProvider
   （例如 macOS 热键可用 Carbon/Cocoa 事件监听，Linux/X11 用 XGrabKey）；
2. 在下方工厂函数中按 sys.platform 登记。
未接入的平台自动降级：热键注册失败（托盘提示）、窗口点选退化为框选/整屏。
"""
import sys

from .base import (CancelProvider, ClipCursorProvider, CursorProvider,
                   HotkeyProvider, NullCancelProvider,
                   NullClipCursorProvider, NullCursorProvider,
                   NullHotkeyProvider, NullWindowProvider, WindowProvider)

__all__ = [
    "create_cancel_provider",
    "create_clip_cursor_provider",
    "create_cursor_provider",
    "create_hotkey_provider",
    "create_window_provider",
    "CancelProvider",
    "ClipCursorProvider",
    "CursorProvider",
    "HotkeyProvider",
    "WindowProvider",
]


def create_hotkey_provider(parent=None) -> HotkeyProvider:
    if sys.platform == "win32":
        from .win32 import Win32HotkeyProvider
        return Win32HotkeyProvider(parent)
    # TODO: macOS / Linux 热键后端
    return NullHotkeyProvider(parent)


def create_window_provider() -> WindowProvider:
    if sys.platform == "win32":
        from .win32 import Win32WindowProvider
        return Win32WindowProvider()
    # TODO: macOS（CGWindowList）/ Linux（X11）窗口枚举
    return NullWindowProvider()


def create_cursor_provider() -> CursorProvider:
    """创建光标物理像素坐标提供者。

    优先使用系统原生 API（如 Win32 GetCursorPos）获取精确物理像素坐标；
    未接入的平台降级为 Qt 逻辑坐标 × DPR 近似（非整数 DPR 有精度损失）。
    """
    if sys.platform == "win32":
        from .win32 import Win32CursorProvider
        return Win32CursorProvider()
    # TODO: macOS（CGEventGetLocation）/ Linux（XQueryPointer）光标坐标
    return NullCursorProvider()


def create_cancel_provider() -> CancelProvider:
    """创建 Esc 键原生拦截提供者。

    通过系统原生事件循环拦截 Esc 键，不依赖焦点/键盘抓取即可取消截图。
    未接入的平台降级为空操作，取消依赖覆盖层自身的 keyPressEvent。
    """
    if sys.platform == "win32":
        from .win32 import Win32CancelProvider
        return Win32CancelProvider()
    # TODO: macOS（CGEvent）/ Linux（X11）原生按键拦截
    return NullCancelProvider()


def create_clip_cursor_provider() -> ClipCursorProvider:
    """创建光标限制提供者。

    截图时防止光标漂移到其他屏幕，提升多屏使用体验。
    未接入的平台降级为空操作（如 Wayland 不允许客户端控制全局光标）。
    """
    if sys.platform == "win32":
        from .win32 import Win32ClipCursorProvider
        return Win32ClipCursorProvider()
    # TODO: macOS（CGEventTap + CGWarpMouseCursorPosition）
    # TODO: Linux X11（X11 event filter + XWarpPointer）
    # Wayland 无法实现，降级为 NullClipCursorProvider
    return NullClipCursorProvider()
