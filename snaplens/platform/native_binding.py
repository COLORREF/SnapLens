"""C++ 原生平台库的 Python 绑定层。

通过 ctypes 加载 snaplens_platform.dll，实现与 platform/base.py 相同
的 Provider 接口。

设计要点：
- 所有 Provider 类与 base.py 接口签名完全一致，上层无感知
- CFUNCTYPE 回调必须保持 Python 侧引用，否则会被 GC 导致崩溃
- 跨线程回调（hotkey/cancel）只 emit Qt 信号，不直接操作 UI；
  Qt 信号槽机制自动将事件投递到主线程，符合线程安全约定
"""
import ctypes
import os
import sys
from pathlib import Path

from PySide6.QtCore import Qt, QObject, Signal
from PySide6.QtGui import QKeySequence

from .base import (CancelProvider, ClipCursorProvider, CursorProvider,
                   HotkeyProvider, WindowProvider)

# Win32 修饰键常量（与 snaplens_platform.h 中 SNAP_MOD_* 一致）
_MOD_ALT = 0x0001
_MOD_CONTROL = 0x0002
_MOD_SHIFT = 0x0004
_MOD_WIN = 0x0008
_MOD_NOREPEAT = 0x4000

# ----------------------------------------------------------------- Qt 键码映射
# Qt.Key → Win32 虚拟键码
# 字母 A-Z、数字 0-9 的 Qt 键值与 VK 码一致，无需映射
_SPECIAL_KEYS = {
    Qt.Key.Key_Space: 0x20,
    Qt.Key.Key_Backspace: 0x08,
    Qt.Key.Key_Tab: 0x09,
    Qt.Key.Key_Return: 0x0D,
    Qt.Key.Key_Enter: 0x0D,
    Qt.Key.Key_Escape: 0x1B,
    Qt.Key.Key_Print: 0x2C,
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
        return _SPECIAL_KEYS.get(Qt.Key(key))
    except ValueError:
        return None

def _qt_mods_to_win(mods) -> int:
    """Qt 修饰键 -> Win32 MOD_* 组合（附加 MOD_NOREPEAT）。"""
    value = _MOD_NOREPEAT
    if mods & Qt.KeyboardModifier.AltModifier:
        value |= _MOD_ALT
    if mods & Qt.KeyboardModifier.ControlModifier:
        value |= _MOD_CONTROL
    if mods & Qt.KeyboardModifier.ShiftModifier:
        value |= _MOD_SHIFT
    if mods & Qt.KeyboardModifier.MetaModifier:
        value |= _MOD_WIN
    return value

# ----------------------------------------------------------------- 回调类型
# 模块级 CFUNCTYPE：保持类型稳定，避免每次创建新类型
# 实例引用必须由调用方保持，否则 ctypes 回调对象会被 GC 导致崩溃
HotkeyCallbackC = ctypes.CFUNCTYPE(None,        # void 返回
                                    ctypes.c_int,    # hotkey_id
                                    ctypes.c_void_p)  # user_data
CancelCallbackC = ctypes.CFUNCTYPE(None,         # void 返回
                                    ctypes.c_void_p)  # user_data

# ----------------------------------------------------------------- DLL 加载
# 全局单例：DLL 加载一次，签名设置一次
# 注意：变量名与函数名不可相同！Python 的 def 会覆盖同名模块级变量
_dll_cache: ctypes.CDLL | None = None

_native_bin_added = False

def _add_native_bin_to_search_path() -> None:
    global _native_bin_added
    if _native_bin_added:
        return
    project_root = Path(__file__).parent.parent.parent
    native_bin = project_root / "native" / "bin"
    if native_bin.is_dir():
        os.add_dll_directory(str(native_bin))
    _native_bin_added = True

def _load_dll() -> ctypes.CDLL:
    """加载 snaplens_platform.dll，失败抛 OSError。

    查找顺序：
    1. native/bin/              （CMake 构建输出目录）
    2. native/build/*/bin/       （CLion cmake-build-* 目录）
    3. 可执行文件同级             （PyInstaller 打包）
    """
    _add_native_bin_to_search_path()

    # __file__ = snaplens/platform/native_binding.py → 项目根目录
    project_root = Path(__file__).parent.parent.parent
    candidates = [
        project_root / "native" / "bin" / "snaplens_platform.dll",
    ]
    # CLion 构建目录
    for build_dir in sorted(project_root.glob("native/cmake-build-*/bin"), reverse=True):
        candidates.append(build_dir / "snaplens_platform.dll")
    if getattr(sys, "frozen", False):
        candidates.append(Path(sys.executable).parent / "snaplens_platform.dll")

    for path in candidates:
        if path.is_file():
            try:
                dll = ctypes.CDLL(str(path))
                _setup_signatures(dll)
                return dll
            except OSError as e:
                raise OSError(f"加载 {path} 失败：{e}") from e

    searched = "\n  ".join(str(p) for p in candidates)
    raise OSError(f"未找到 snaplens_platform.dll，已搜索：\n  {searched}")

def _setup_signatures(dll: ctypes.CDLL) -> None:
    """设置所有 C ABI 函数的参数和返回类型。

    必须在调用任何函数前完成，否则 ctypes 默认按 int 处理参数，
    64 位句柄会被截断。
    """
    # 生命周期
    dll.snap_init.restype = ctypes.c_int
    dll.snap_init.argtypes = []
    dll.snap_shutdown.restype = None
    dll.snap_shutdown.argtypes = []

    # 热键
    dll.snap_hotkey_set_callback.restype = None
    dll.snap_hotkey_set_callback.argtypes = [HotkeyCallbackC, ctypes.c_void_p]
    dll.snap_hotkey_register.restype = ctypes.c_int
    dll.snap_hotkey_register.argtypes = [ctypes.c_int,
                                          ctypes.c_uint, ctypes.c_uint]
    dll.snap_hotkey_unregister.restype = None
    dll.snap_hotkey_unregister.argtypes = [ctypes.c_int]

    # 窗口枚举
    dll.snap_window_enum.restype = ctypes.c_int
    dll.snap_window_enum.argtypes = []
    dll.snap_window_get_item.restype = ctypes.c_int
    dll.snap_window_get_item.argtypes = [
        ctypes.c_int,
        ctypes.POINTER(ctypes.c_longlong),
        ctypes.POINTER(ctypes.c_int),
        ctypes.POINTER(ctypes.c_int),
        ctypes.POINTER(ctypes.c_int),
        ctypes.POINTER(ctypes.c_int),
        ctypes.c_wchar_p,
        ctypes.c_int,
    ]

    # 光标
    dll.snap_cursor_get_pos.restype = None
    dll.snap_cursor_get_pos.argtypes = [ctypes.POINTER(ctypes.c_int),
                                          ctypes.POINTER(ctypes.c_int)]

    # Esc 拦截
    dll.snap_cancel_install.restype = ctypes.c_int
    dll.snap_cancel_install.argtypes = [CancelCallbackC, ctypes.c_void_p]
    dll.snap_cancel_uninstall.restype = None
    dll.snap_cancel_uninstall.argtypes = []

    # 光标限制
    dll.snap_clip_cursor.restype = ctypes.c_int
    dll.snap_clip_cursor.argtypes = [ctypes.c_int, ctypes.c_int,
                                      ctypes.c_int, ctypes.c_int]
    dll.snap_clip_cursor_release.restype = None
    dll.snap_clip_cursor_release.argtypes = []

def _get_dll() -> ctypes.CDLL:
    """获取已加载并配置签名的 DLL 单例。"""
    global _dll_cache
    if _dll_cache is None:
        _dll_cache = _load_dll()
    return _dll_cache

# =================================================================
# Provider 实现
# =================================================================

class NativeHotkeyProvider(HotkeyProvider):
    """基于 C++ DLL 的全局热键实现。

    热键触发时由 C++ 后台线程调用回调，回调仅 emit Qt 信号，
    Qt 信号机制自动跨线程投递到主线程槽。
    """

    HOTKEY_ID = 0x5053  # 任意 0x0000-0xBFFF 内的唯一值

    def __init__(self, parent=None):
        super().__init__(parent)
        self._registered = False
        # 保持回调引用，防止 ctypes 回调对象被 GC 导致崩溃
        # 回调函数必须是模块级 CFUNCTYPE 实例，且 self 持有引用
        self._cb_ref = HotkeyCallbackC(self._on_hotkey)
        _get_dll().snap_hotkey_set_callback(self._cb_ref, None)

    def _on_hotkey(self, hotkey_id: int, user_data) -> None:
        """C++ 后台线程回调，仅 emit 信号。"""
        self.triggered.emit()

    def start(self, sequence_text: str) -> bool:
        """注册热键。成功返回 True；失败多为键位冲突或键值不支持。"""
        self.stop()
        seq = QKeySequence.fromString(sequence_text)
        if seq.isEmpty():
            return False
        combination = seq[0]
        mods = _qt_mods_to_win(combination.keyboardModifiers())
        vk = _qt_key_to_vk(combination.key())
        if vk is None:
            return False
        ok = bool(_get_dll().snap_hotkey_register(
            self.HOTKEY_ID, mods, vk))
        self._registered = ok
        return ok

    def stop(self) -> None:
        """注销热键。"""
        if self._registered:
            _get_dll().snap_hotkey_unregister(self.HOTKEY_ID)
            self._registered = False

class NativeWindowProvider(WindowProvider):
    """基于 C++ DLL 的顶层窗口枚举（Z 序自上而下，物理像素矩形）。"""

    def enum_windows(self) -> list:
        count = _get_dll().snap_window_enum()
        results = []
        for idx in range(count):
            hwnd = ctypes.c_longlong()
            left, top, right, bottom = (ctypes.c_int() for _ in range(4))
            title_buf = ctypes.create_unicode_buffer(256)
            if _get_dll().snap_window_get_item(
                idx, ctypes.byref(hwnd),
                ctypes.byref(left), ctypes.byref(top),
                ctypes.byref(right), ctypes.byref(bottom),
                title_buf, 256
            ):
                results.append({
                    "hwnd": hwnd.value,
                    "rect": (left.value, top.value,
                             right.value, bottom.value),
                    "title": title_buf.value,
                })
        return results

class NativeCursorProvider(CursorProvider):
    """通过 Win32 GetCursorPos 直接获取鼠标的桌面物理像素坐标。

    Qt 的鼠标事件坐标始终为整数逻辑坐标，乘以非整数 DPR 会跳过部分物理像素。
    本实现绕过 Qt 坐标体系，直接取系统级的物理像素位置，保证像素级精度。
    """

    def physical_position(self) -> tuple[int, int]:
        x = ctypes.c_int()
        y = ctypes.c_int()
        _get_dll().snap_cursor_get_pos(ctypes.byref(x), ctypes.byref(y))
        return x.value, y.value

class NativeCancelProvider(QObject, CancelProvider):
    """通过 C++ DLL 的 WH_KEYBOARD_LL 低级钩子拦截 Esc 键。

    比 Qt 的 QAbstractNativeEventFilter 更可靠：
    - 不依赖焦点/键盘抓取
    - 即使覆盖层未获得焦点也能取消截图

    线程安全：C++ 钩子线程回调时 emit 一个 Qt Signal，
    Qt 的 AutoConnection 自动检测跨线程并用 QueuedConnection
    把回调投递到主线程事件循环，避免非主线程操作 Qt GUI 导致死锁。
    """

    # 内部信号：Esc 按下时 emit（可能从 C++ 钩子线程发出）
    _esc_pressed = Signal()

    def __init__(self):
        QObject.__init__(self)
        self._cb_ref: CancelCallbackC | None = None
        self._installed = False
        self._callback = None

    def install(self, callback) -> bool:
        """安装原生 Esc 键拦截。成功返回 True。

        callback 会被连接到内部 Signal，触发时由 Qt 自动
        投递到主线程执行，不会在 C++ 钩子线程中直接操作 GUI。
        """
        if self._installed:
            return True
        self._callback = callback
        # 将 callback 连接到内部信号，Qt 自动处理跨线程投递
        self._esc_pressed.connect(callback, Qt.ConnectionType.AutoConnection)
        # C++ 回调仅 emit 信号（不直接调用 callback），避免跨线程操作 GUI
        self._cb_ref = CancelCallbackC(lambda _user_data: self._esc_pressed.emit())
        self._installed = bool(_get_dll().snap_cancel_install(self._cb_ref, None))

    def uninstall(self) -> None:
        """卸载拦截。"""
        if self._installed:
            _get_dll().snap_cancel_uninstall()
            self._installed = False
            self._cb_ref = None
            # 断开信号连接，防止后续意外触发
            if self._callback is not None:
                try:
                    self._esc_pressed.disconnect(self._callback)
                except (TypeError, RuntimeError):
                    pass  # 已断开则忽略
                self._callback = None

class NativeClipCursorProvider(ClipCursorProvider):
    """通过 Win32 ClipCursor API 将光标移动限制在指定矩形内。"""

    def clip_to_rect(self, left: int, top: int,
                     right: int, bottom: int) -> bool:
        return bool(_get_dll().snap_clip_cursor(left, top, right, bottom))

    def release(self) -> None:
        _get_dll().snap_clip_cursor_release()
