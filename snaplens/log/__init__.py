"""C++ 统一日志库的 Python 绑定层。

通过 ctypes 加载 snaplens_log.dll，提供 Python 端日志输出能力。
格式为 "[snap LEVEL file:line] msg"，输出到 stderr。
"""
import ctypes
import os
import sys
from pathlib import Path

# ----------------------------------------------------------------- 日志等级
LOG_LEVEL_DEBUG = 0
LOG_LEVEL_INFO = 1
LOG_LEVEL_WARNING = 2
LOG_LEVEL_ERROR = 3

# ----------------------------------------------------------------- DLL 加载
_dll_cache: ctypes.CDLL | None = None
_native_bin_added = False


def _add_native_bin_to_search_path() -> None:
    """确保 native/bin/ 在 DLL 搜索路径中。"""
    global _native_bin_added
    if _native_bin_added:
        return
    project_root = Path(__file__).parent.parent.parent
    native_bin = project_root / "native" / "bin"
    if native_bin.is_dir():
        os.add_dll_directory(str(native_bin))
    _native_bin_added = True


def _load_dll() -> ctypes.CDLL:
    """加载 snaplens_log.dll。"""
    _add_native_bin_to_search_path()

    project_root = Path(__file__).parent.parent.parent
    candidates = [
        project_root / "native" / "bin" / "snaplens_log.dll",
    ]
    for build_dir in sorted(
        project_root.glob("native/cmake-build-*/bin"), reverse=True
    ):
        candidates.append(build_dir / "snaplens_log.dll")
    if getattr(sys, "frozen", False):
        candidates.append(Path(sys.executable).parent / "snaplens_log.dll")

    for path in candidates:
        if path.is_file():
            dll = ctypes.CDLL(str(path))
            _setup_signatures(dll)
            return dll

    searched = "\n  ".join(str(p) for p in candidates)
    raise OSError(f"未找到 snaplens_log.dll，已搜索：\n  {searched}")


def _setup_signatures(dll: ctypes.CDLL) -> None:
    """设置 C ABI 函数签名。"""
    dll.snap_log_init.restype = ctypes.c_int
    dll.snap_log_init.argtypes = []
    dll.snap_log_shutdown.restype = None
    dll.snap_log_shutdown.argtypes = []
    dll.snap_log_write_msg.restype = None
    dll.snap_log_write_msg.argtypes = [
        ctypes.c_int, ctypes.c_char_p, ctypes.c_int, ctypes.c_char_p, ctypes.c_char_p,
    ]


def _get_dll() -> ctypes.CDLL:
    global _dll_cache
    if _dll_cache is None:
        _dll_cache = _load_dll()
    return _dll_cache


# ----------------------------------------------------------------- 级别过滤
_enabled_levels: set[int] = {
    LOG_LEVEL_DEBUG, LOG_LEVEL_INFO, LOG_LEVEL_WARNING, LOG_LEVEL_ERROR,
}


def set_enabled_levels(debug: bool, info: bool, warning: bool, error: bool) -> None:
    """更新启用的日志级别。由 AppController 在加载/更新设置后调用。"""
    global _enabled_levels
    _enabled_levels = set()
    if debug:    _enabled_levels.add(LOG_LEVEL_DEBUG)
    if info:     _enabled_levels.add(LOG_LEVEL_INFO)
    if warning:  _enabled_levels.add(LOG_LEVEL_WARNING)
    if error:    _enabled_levels.add(LOG_LEVEL_ERROR)


def init() -> None:
    _get_dll().snap_log_init()


def shutdown() -> None:
    _get_dll().snap_log_shutdown()


def log(level: int, message: str) -> None:
    """写入一条日志。"""
    if level not in _enabled_levels:
        return

    import inspect
    frame = inspect.currentframe()
    if frame is not None:
        frame = frame.f_back
    if frame is not None:
        frame = frame.f_back
    caller = frame
    file = caller.f_code.co_filename if caller else "?"
    line = caller.f_lineno if caller else 0
    func = caller.f_code.co_name if caller else ""

    _get_dll().snap_log_write_msg(
        level,
        file.encode("utf-8"),
        line,
        func.encode("utf-8") if func else None,
        message.encode("utf-8"),
    )


def log_debug(message: str) -> None:
    log(LOG_LEVEL_DEBUG, message)


def log_info(message: str) -> None:
    log(LOG_LEVEL_INFO, message)


def log_warning(message: str) -> None:
    log(LOG_LEVEL_WARNING, message)


def log_error(message: str) -> None:
    log(LOG_LEVEL_ERROR, message)
