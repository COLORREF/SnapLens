"""平台相关能力的统一入口。

Windows 平台所有能力由 native/ 目录下的 C++ DLL 提供（snaplens_platform.dll）。
DLL 加载失败时程序直接退出，不再降级到纯 Python 实现。

接入新平台（macOS/Linux）时：
1. 在 platform/ 下新建实现模块，继承 base 中的对应抽象类；
2. 在下方工厂函数中按 sys.platform 登记。
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


# ----------------------------------------------------------------- native DLL 加载
# Windows 平台：必须加载 snaplens_platform.dll，失败则直接退出程序
_native_module = None


def _get_native():
    """返回已加载的 native_binding 模块。

    DLL 加载失败时直接抛出 RuntimeError 并退出。
    """
    global _native_module
    if _native_module is None:
        from . import native_binding
        native_binding._get_dll()  # 触发 DLL 加载 + 签名设置
        _native_module = native_binding
    return _native_module


# ----------------------------------------------------------------- 工厂函数
def create_hotkey_provider(parent=None) -> HotkeyProvider:
    if sys.platform == "win32":
        return _get_native().NativeHotkeyProvider(parent)
    # TODO: macOS / Linux 热键后端
    return NullHotkeyProvider(parent)


def create_window_provider() -> WindowProvider:
    if sys.platform == "win32":
        return _get_native().NativeWindowProvider()
    # TODO: macOS（CGWindowList）/ Linux（X11）窗口枚举
    return NullWindowProvider()


def create_cursor_provider() -> CursorProvider:
    if sys.platform == "win32":
        return _get_native().NativeCursorProvider()
    # TODO: macOS（CGEventGetLocation）/ Linux（XQueryPointer）光标坐标
    return NullCursorProvider()


def create_cancel_provider() -> CancelProvider:
    if sys.platform == "win32":
        return _get_native().NativeCancelProvider()
    # TODO: macOS（CGEvent）/ Linux（X11）原生按键拦截
    return NullCancelProvider()


def create_clip_cursor_provider() -> ClipCursorProvider:
    if sys.platform == "win32":
        return _get_native().NativeClipCursorProvider()
    # TODO: macOS（CGEventTap + CGWarpMouseCursorPosition）
    # TODO: Linux X11（X11 event filter + XWarpPointer）
    # Wayland 无法实现，降级为 NullClipCursorProvider
    return NullClipCursorProvider()