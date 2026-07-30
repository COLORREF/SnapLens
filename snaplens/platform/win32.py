"""Windows 平台后端。

- Win32HotkeyProvider：RegisterHotKey（注册到 GUI 线程）+
  QAbstractNativeEventFilter 拦截 WM_HOTKEY，无需第三方库；
- Win32WindowProvider：EnumWindows 枚举顶层窗口，
  窗口矩形优先取 DWM 扩展边框（去除 Win10/11 隐形阴影边距）。

ctypes 调用集中在本文件，上层不接触任何 Win32 细节。
"""
import ctypes
from ctypes import wintypes

from PySide6.QtCore import QAbstractNativeEventFilter, QCoreApplication, Qt
from PySide6.QtGui import QKeySequence

from .base import CancelProvider, ClipCursorProvider, CursorProvider, \
    HotkeyProvider, WindowProvider

user32 = ctypes.windll.user32
kernel32 = ctypes.windll.kernel32
try:
    dwmapi = ctypes.windll.dwmapi
except OSError:  # 极少数精简系统可能没有 dwmapi
    dwmapi = None

# ---- 热键相关常量 ----
MOD_ALT = 0x0001
MOD_CONTROL = 0x0002
MOD_SHIFT = 0x0004
MOD_WIN = 0x0008
MOD_NOREPEAT = 0x4000  # 按住不重复触发
WM_HOTKEY = 0x0312

# ---- DWM 属性 ----
DWMWA_EXTENDED_FRAME_BOUNDS = 9  # 窗口可见边框（物理像素）
DWMWA_CLOAKED = 14               # UWP 等被“遮蔽”的隐形窗口

WNDENUMPROC = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)

# ---- 函数签名（避免 64 位句柄被默认 c_int 截断）----
user32.EnumWindows.argtypes = [WNDENUMPROC, wintypes.LPARAM]
user32.IsWindowVisible.argtypes = [wintypes.HWND]
user32.IsIconic.argtypes = [wintypes.HWND]
user32.GetWindowThreadProcessId.argtypes = [wintypes.HWND, wintypes.LPDWORD]
user32.GetWindowTextW.argtypes = [wintypes.HWND, wintypes.LPWSTR, ctypes.c_int]
user32.GetWindowRect.argtypes = [wintypes.HWND, wintypes.LPRECT]
user32.RegisterHotKey.argtypes = [wintypes.HWND, ctypes.c_int,
                                  wintypes.UINT, wintypes.UINT]
user32.RegisterHotKey.restype = wintypes.BOOL
user32.UnregisterHotKey.argtypes = [wintypes.HWND, ctypes.c_int]
if dwmapi is not None:
    dwmapi.DwmGetWindowAttribute.argtypes = [
        wintypes.HWND, wintypes.DWORD, wintypes.LPVOID, wintypes.DWORD,
    ]
kernel32.GetCurrentProcessId.restype = wintypes.DWORD


# =====================================================================
# 全局热键
# =====================================================================

# Qt.Key -> Win32 虚拟键码（字母和数字键值相同，单独处理）
_SPECIAL_KEYS = {
    Qt.Key.Key_Space: 0x20,
    Qt.Key.Key_Backspace: 0x08,
    Qt.Key.Key_Tab: 0x09,
    Qt.Key.Key_Return: 0x0D,
    Qt.Key.Key_Enter: 0x0D,
    Qt.Key.Key_Escape: 0x1B,
    Qt.Key.Key_Print: 0x2C,   # PrintScreen
    Qt.Key.Key_Insert: 0x2D,
    Qt.Key.Key_Delete: 0x2E,
    Qt.Key.Key_Home: 0x24,
    Qt.Key.Key_End: 0x23,
    Qt.Key.Key_PageUp: 0x21,
    Qt.Key.Key_PageDown: 0x22,
    Qt.Key.Key_Left: 0x25,
    Qt.Key.Key_Up: 0x26,
    Qt.Key.Key_Right: 0x27,
    Qt.Key.Key_Down: 0x28,
}


def _qt_key_to_vk(key: int) -> int | None:
    """把 Qt 键值转换成 Win32 虚拟键码，不支持时返回 None。"""
    # 数字键 0-9、字母键 A-Z：Qt 键值与 VK 码一致
    if 0x30 <= key <= 0x39 or 0x41 <= key <= 0x5A:
        return key
    # F1-F24
    f1, f24 = int(Qt.Key.Key_F1), int(Qt.Key.Key_F24)
    if f1 <= key <= f24:
        return 0x70 + (key - f1)
    try:
        enum_key = Qt.Key(key)
    except ValueError:
        return None
    return _SPECIAL_KEYS.get(enum_key)


def _qt_mods_to_win(mods) -> int:
    """Qt 修饰键 -> Win32 MOD_* 组合（附加 MOD_NOREPEAT）。"""
    value = MOD_NOREPEAT
    if mods & Qt.KeyboardModifier.AltModifier:
        value |= MOD_ALT
    if mods & Qt.KeyboardModifier.ControlModifier:
        value |= MOD_CONTROL
    if mods & Qt.KeyboardModifier.ShiftModifier:
        value |= MOD_SHIFT
    if mods & Qt.KeyboardModifier.MetaModifier:
        value |= MOD_WIN
    return value


class _HotkeyEventFilter(QAbstractNativeEventFilter):
    """拦截线程消息队列中的 WM_HOTKEY。"""

    def __init__(self, hotkey_id: int, callback):
        super().__init__()
        self._hotkey_id = hotkey_id
        self._callback = callback

    def nativeEventFilter(self, event_type, message):
        try:
            if bytes(event_type) in (b"windows_generic_MSG",
                                     b"windows_dispatcher_MSG"):
                msg = wintypes.MSG.from_address(int(message))
                if msg.message == WM_HOTKEY and msg.wParam == self._hotkey_id:
                    self._callback()
        except Exception:
            pass  # 过滤器里绝不向外抛异常
        return False, 0


class Win32HotkeyProvider(HotkeyProvider):
    """Windows 全局热键（RegisterHotKey）。"""

    HOTKEY_ID = 0x5053  # 任意 0x0000-0xBFFF 内的唯一值

    def __init__(self, parent=None):
        super().__init__(parent)
        self._registered = False
        self._filter = _HotkeyEventFilter(self.HOTKEY_ID, self.triggered.emit)
        QCoreApplication.instance().installNativeEventFilter(self._filter)

    def start(self, sequence_text: str) -> bool:
        self.stop()
        seq = QKeySequence.fromString(sequence_text)
        if seq.isEmpty():
            return False
        combination = seq[0]  # 只取第一组按键组合
        vk = _qt_key_to_vk(int(combination.key()))
        if vk is None:
            return False
        mods = _qt_mods_to_win(combination.keyboardModifiers())
        self._registered = bool(
            user32.RegisterHotKey(None, self.HOTKEY_ID, mods, vk)
        )
        return self._registered

    def stop(self) -> None:
        if self._registered:
            user32.UnregisterHotKey(None, self.HOTKEY_ID)
            self._registered = False


# =====================================================================
# 窗口枚举
# =====================================================================

def _frame_bounds(hwnd) -> tuple | None:
    """取窗口可见边框 (left, top, right, bottom)，单位：物理像素。"""
    rect = wintypes.RECT()
    if dwmapi is not None:
        hr = dwmapi.DwmGetWindowAttribute(
            hwnd, DWMWA_EXTENDED_FRAME_BOUNDS,
            ctypes.byref(rect), ctypes.sizeof(rect),
        )
        if hr == 0:  # S_OK
            return rect.left, rect.top, rect.right, rect.bottom
    if user32.GetWindowRect(hwnd, ctypes.byref(rect)):
        return rect.left, rect.top, rect.right, rect.bottom
    return None


def _is_cloaked(hwnd) -> bool:
    """是否为被系统遮蔽（实际不可见）的窗口。"""
    if dwmapi is None:
        return False
    value = wintypes.DWORD(0)
    hr = dwmapi.DwmGetWindowAttribute(
        hwnd, DWMWA_CLOAKED, ctypes.byref(value), ctypes.sizeof(value)
    )
    return hr == 0 and value.value != 0


class Win32WindowProvider(WindowProvider):
    """Windows 顶层窗口枚举（Z 序自上而下，物理像素矩形）。"""

    def enum_windows(self) -> list:
        own_pid = kernel32.GetCurrentProcessId()
        results = []

        @WNDENUMPROC
        def _callback(hwnd, _lparam):
            # 只保留可见且未最小化的窗口
            if not user32.IsWindowVisible(hwnd) or user32.IsIconic(hwnd):
                return True
            # 排除本进程自己的窗口（覆盖层、钉图窗口等）
            pid = wintypes.DWORD(0)
            user32.GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
            if pid.value == own_pid:
                return True
            # 排除被 DWM 遮蔽的隐形窗口（如挂起的 UWP 应用）
            if _is_cloaked(hwnd):
                return True
            bounds = _frame_bounds(hwnd)
            if bounds is None:
                return True
            left, top, right, bottom = bounds
            if right - left < 8 or bottom - top < 8:  # 忽略过小窗口
                return True
            title_buf = ctypes.create_unicode_buffer(512)
            user32.GetWindowTextW(hwnd, title_buf, 512)
            results.append({
                "hwnd": int(hwnd),
                "rect": (left, top, right, bottom),
                "title": title_buf.value,
            })
            return True

        user32.EnumWindows(_callback, 0)
        return results


# =====================================================================
# 光标物理像素坐标
# =====================================================================

class Win32CursorProvider(CursorProvider):
    """通过 GetCursorPos 直接获取鼠标的桌面物理像素坐标。

    Qt 的鼠标事件坐标始终为整数逻辑坐标，乘以非整数 DPR 会跳过部分物理像素。
    本实现绕过 Qt 坐标体系，直接取系统级的物理像素位置，保证像素级精度。
    """

    def physical_position(self) -> tuple[int, int]:
        pt = wintypes.POINT()
        user32.GetCursorPos(ctypes.byref(pt))
        return pt.x, pt.y


# =====================================================================
# 原生事件拦截：Esc 取消截图
# =====================================================================

_WM_KEYDOWN = 0x0100
_VK_ESCAPE = 0x1B


class Win32CancelProvider(CancelProvider):
    """通过 Win32 原生消息循环拦截 Esc 键，不依赖焦点/键盘抓取。"""

    def __init__(self):
        self._filter = None

    def install(self, callback) -> bool:

        class _Filter(QAbstractNativeEventFilter):
            def nativeEventFilter(_self, event_type, message):
                try:
                    et = bytes(event_type)
                    if et in (b"windows_generic_MSG",
                              b"windows_dispatcher_MSG"):
                        msg = wintypes.MSG.from_address(int(message))
                        if msg.message == _WM_KEYDOWN \
                                and msg.wParam == _VK_ESCAPE:
                            callback()
                except Exception:
                    pass
                return False, 0

        self._filter = _Filter()
        QCoreApplication.instance().installNativeEventFilter(self._filter)
        return True

    def uninstall(self) -> None:
        if self._filter is not None:
            QCoreApplication.instance().removeNativeEventFilter(self._filter)
            self._filter = None


# =====================================================================
# 光标限制：防止截图时光标漂移到其他屏幕
# =====================================================================

# 设置 ClipCursor 的函数签名（RECT 指针或 NULL）
user32.ClipCursor.argtypes = [ctypes.POINTER(wintypes.RECT)]
user32.ClipCursor.restype = wintypes.BOOL


class Win32ClipCursorProvider(ClipCursorProvider):
    """通过 Win32 ClipCursor API 将光标移动限制在指定矩形内。

    传入 None（NULL）解除限制，传入 RECT 结构体设置限制区域。
    """

    def clip_to_rect(self, left: int, top: int,
                     right: int, bottom: int) -> bool:
        rect = wintypes.RECT(left, top, right, bottom)
        return bool(user32.ClipCursor(ctypes.byref(rect)))

    def release(self) -> None:
        user32.ClipCursor(None)
