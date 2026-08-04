"""C++ OCR 库的 Python 绑定层。

通过 ctypes 加载 snaplens_ocr.dll，提供 extract_text 等函数。
与 AI 和 Platform 绑定层保持一致的架构：

- 模块级 DLL 单例缓存（_dll_cache）
- 显式 argtypes/restype 签名设置（防 64 位截断）
- os.add_dll_directory 处理 DLL 依赖搜索（AI 模块的 Qt bin 同理）
- 查找 snaplens_ocr.dll 的候选路径：native/bin → cmake-build-* → exe 同级

设计要点：
    - OCR 引擎 init 只需要调用一次，之后 extract 直接复用
    - extract_text(image_path, ocr_langs) 保持与 core/ocr.py 相同的签名
    - 加载失败返回清晰错误信息，不静默
"""
import ctypes
import os
import sys
from pathlib import Path

from ..log import log_info, log_warning, log_error, log_debug

# 错误码（与 snaplens_ocr.h 中 SNAP_OCR_ERR_* 一致）
SNAP_OCR_OK = 0
SNAP_OCR_ERR_NOT_INIT = -1
SNAP_OCR_ERR_IMAGE = -2
SNAP_OCR_ERR_RECOGNIZE = -3
SNAP_OCR_ERR_PARAM = -4
SNAP_OCR_ERR_FILE = -5

_ERROR_MESSAGES = {
    SNAP_OCR_ERR_NOT_INIT: "OCR 引擎未初始化",
    SNAP_OCR_ERR_IMAGE: "图像数据无效",
    SNAP_OCR_ERR_RECOGNIZE: "文字识别失败",
    SNAP_OCR_ERR_PARAM: "参数无效",
    SNAP_OCR_ERR_FILE: "图片文件读取失败",
}

# ----------------------------------------------------------------- DLL 加载

_dll_cache: ctypes.CDLL | None = None
_sdk_bin_added = False
_engine_initialized = False
_language_cache: str = ""

# 自定义路径（从 settings 设置，空字符串 = 自动检测）
_custom_sdk_bin: str = ""
_custom_tessdata: str = ""


def apply_settings(sdk_bin_dir: str = "", tessdata_dir: str = "") -> None:
    """应用用户自定义路径设置。

    调用后 _find_sdk_bin / find_tessdata_dir 优先使用自定义路径。
    """
    global _custom_sdk_bin, _custom_tessdata
    _custom_sdk_bin = sdk_bin_dir.strip() if sdk_bin_dir else ""
    _custom_tessdata = tessdata_dir.strip() if tessdata_dir else ""
    if _custom_sdk_bin:
        log_info(f"[snap_ocr PY] apply_settings: custom_sdk_bin={_custom_sdk_bin}")
    if _custom_tessdata:
        log_info(f"[snap_ocr PY] apply_settings: custom_tessdata={_custom_tessdata}")


def _find_sdk_bin() -> Path | None:
    """查找 Tesseract SDK 的 DLL 目录（snaplens_ocr.dll 依赖 tesseract55.dll 等）。"""
    # 优先使用用户自定义路径
    if _custom_sdk_bin:
        p = Path(_custom_sdk_bin)
        if p.is_dir() and (p / "tesseract55.dll").is_file():
            log_info(f"[snap_ocr PY] _find_sdk_bin: using custom path {p}")
            return p
        log_warning(f"[snap_ocr PY] _find_sdk_bin: WARNING custom path invalid: {p}")

    # __file__ = snaplens/ocr/native_binding.py → 项目根目录
    project_root = Path(__file__).parent.parent.parent
    log_debug(f"[snap_ocr PY] _find_sdk_bin: project_root={project_root}")

    candidates = [
        project_root / "sdk" / "tesseract" / "bin",
    ]
    # PyInstaller 打包场景
    if getattr(sys, "frozen", False):
        candidates.append(Path(sys.executable).parent / "sdk" / "tesseract" / "bin")

    for p in candidates:
        log_debug(f"[snap_ocr PY] _find_sdk_bin: checking {p}")
        if p.is_dir() and (p / "tesseract55.dll").is_file():
            log_info(f"[snap_ocr PY] _find_sdk_bin: found {p}")
            return p

    log_warning(f"[snap_ocr PY] _find_sdk_bin: NOT FOUND in candidates={[str(c) for c in candidates]}")
    return None


def _ensure_sdk_dll_dir() -> None:
    """确保 Tesseract SDK 的 DLL 目录在搜索路径中（仅执行一次）。"""
    global _sdk_bin_added
    if _sdk_bin_added:
        return
    sdk_bin = _find_sdk_bin()
    if sdk_bin:
        os.add_dll_directory(str(sdk_bin))
        log_info(f"[snap_ocr PY] add_dll_directory: {sdk_bin}")
    else:
        log_warning("[snap_ocr PY] WARNING: SDK bin dir not found, "
              "DLL loading may fail")
    # Also add native/bin for snaplens_log.dll dependency
    project_root = Path(__file__).parent.parent.parent
    native_bin = project_root / "native" / "bin"
    if native_bin.is_dir():
        os.add_dll_directory(str(native_bin))
        log_debug(f"[snap_ocr PY] add_dll_directory (native): {native_bin}")
    _sdk_bin_added = True


def _load_dll() -> ctypes.CDLL:
    """加载 snaplens_ocr.dll，失败抛 OSError。

    查找顺序：
    1. native/bin/              （CMake 构建输出目录）
    2. native/cmake-build-*/bin/ （CLion 构建目录）
    3. 可执行文件同级             （PyInstaller 打包）
    """
    _ensure_sdk_dll_dir()

    # __file__ = snaplens/ocr/native_binding.py → 项目根目录
    project_root = Path(__file__).parent.parent.parent
    candidates = [
        project_root / "native" / "bin" / "snaplens_ocr.dll",
    ]
    # CLion 构建目录（按时间倒序，优先最新的）
    for build_dir in sorted(
        project_root.glob("native/cmake-build-*/bin"), reverse=True
    ):
        candidates.append(build_dir / "snaplens_ocr.dll")
    if getattr(sys, "frozen", False):
        candidates.append(Path(sys.executable).parent / "snaplens_ocr.dll")

    for path in candidates:
        if path.is_file():
            try:
                log_info(f"[snap_ocr PY] 加载 DLL: {path}")
                dll = ctypes.CDLL(str(path))
                _setup_signatures(dll)
                log_info(f"[snap_ocr PY] DLL 加载成功: {path}")
                return dll
            except OSError as e:
                log_error(f"[snap_ocr PY] 加载失败 {path}: {e}")
                raise OSError(f"加载 snaplens_ocr.dll 失败 ({path}): {e}") from e

    searched = "\n  ".join(str(p) for p in candidates)
    raise OSError(
        f"未找到 snaplens_ocr.dll，已搜索：\n  {searched}\n\n"
        f"请先编译 OCR 模块：\n"
        f"  cd native\n"
        f"  cmake -B cmake-build-release -G Ninja -DCMAKE_BUILD_TYPE=Release\n"
        f"  cmake --build cmake-build-release"
    )


def _setup_signatures(dll: ctypes.CDLL) -> None:
    """设置所有 C ABI 函数的参数和返回类型。

    必须在调用任何函数前完成，否则 ctypes 默认按 int 处理参数，
    64 位指针会被截断。
    """
    # 生命周期
    dll.snap_ocr_init.restype = ctypes.c_int
    dll.snap_ocr_init.argtypes = [ctypes.c_char_p, ctypes.c_char_p]
    dll.snap_ocr_shutdown.restype = None
    dll.snap_ocr_shutdown.argtypes = []
    dll.snap_ocr_is_initialized.restype = ctypes.c_int
    dll.snap_ocr_is_initialized.argtypes = []

    # 文字提取（内存像素）
    dll.snap_ocr_extract_text.restype = ctypes.c_int
    dll.snap_ocr_extract_text.argtypes = [
        ctypes.POINTER(ctypes.c_ubyte),  # image_data
        ctypes.c_int,                     # width
        ctypes.c_int,                     # height
        ctypes.c_int,                     # bytes_per_pixel
        ctypes.c_int,                     # bytes_per_line
        ctypes.c_char_p,                  # text_out
        ctypes.c_int,                     # text_size
        ctypes.c_char_p,                  # error_out
        ctypes.c_int,                     # error_size
    ]

    # 文字提取（文件路径）
    dll.snap_ocr_extract_text_file.restype = ctypes.c_int
    dll.snap_ocr_extract_text_file.argtypes = [
        ctypes.c_char_p,     # image_path (UTF-8)
        ctypes.c_char_p,     # text_out
        ctypes.c_int,        # text_size
        ctypes.c_char_p,     # error_out
        ctypes.c_int,        # error_size
    ]

    # 引擎配置
    dll.snap_ocr_set_variable.restype = ctypes.c_int
    dll.snap_ocr_set_variable.argtypes = [ctypes.c_char_p, ctypes.c_char_p]

    # 版本
    dll.snap_ocr_get_version.restype = ctypes.c_char_p
    dll.snap_ocr_get_version.argtypes = []


def _get_dll() -> ctypes.CDLL:
    """获取已加载并配置签名的 DLL 单例。"""
    global _dll_cache
    if _dll_cache is None:
        _dll_cache = _load_dll()
    return _dll_cache


# ----------------------------------------------------------------- 公共 API

def is_available() -> bool:
    """检查 OCR DLL 是否可用（加载成功返回 True）。"""
    log_debug("[snap_ocr PY] is_available: checking...")
    try:
        dll = _get_dll()
        ver = get_version()
        log_info(f"[snap_ocr PY] is_available: True (version={ver})")
        return True
    except OSError as e:
        log_warning(f"[snap_ocr PY] is_available: False — {e}")
        return False


def get_version() -> str:
    """获取 Tesseract 版本字符串，始终可用（无需初始化引擎）。"""
    ver_ptr = _get_dll().snap_ocr_get_version()
    if ver_ptr:
        ver = ver_ptr.decode("utf-8", errors="replace") if isinstance(ver_ptr, bytes) else str(ver_ptr)
        log_debug(f"[snap_ocr PY] get_version: {ver}")
        return ver
    log_warning("[snap_ocr PY] get_version: returned NULL")
    return "unknown"


def init_ocr(data_path: str, language: str) -> bool:
    """初始化 OCR 引擎（加载语言模型）。

    Args:
        data_path: tessdata 目录的父路径
        language:  Tesseract 语言代码（+ 分隔多语言）

    Returns:
        True 初始化成功
    """
    global _engine_initialized, _language_cache
    log_info(f"[snap_ocr PY] init_ocr: data_path={data_path} language={language}")
    ret = _get_dll().snap_ocr_init(
        data_path.encode("utf-8"),
        language.encode("utf-8"),
    )
    if ret == SNAP_OCR_OK:
        _engine_initialized = True
        _language_cache = language
        log_info(f"[snap_ocr PY] init_ocr: OK, version={get_version()}")
        return True
    else:
        _engine_initialized = False
        log_error(f"[snap_ocr PY] init_ocr: FAIL ret={ret}")
        return False


def shutdown_ocr() -> None:
    """关闭 OCR 引擎。"""
    global _engine_initialized, _language_cache
    log_info("[snap_ocr PY] shutdown_ocr")
    _get_dll().snap_ocr_shutdown()
    _engine_initialized = False
    _language_cache = ""


def _ensure_engine(data_path: str, language: str) -> None:
    """确保引擎已初始化（如需切换语言则重建）。"""
    global _engine_initialized, _language_cache
    if _engine_initialized and _language_cache == language:
        log_debug(f"[snap_ocr PY] _ensure_engine: reusing engine (language={language})")
        return
    log_info(f"[snap_ocr PY] _ensure_engine: initializing engine "
          f"(language={language}, was={_language_cache}, initialized={_engine_initialized})")
    init_ocr(data_path, language)


def extract_text_raw(image_data: bytes, width: int, height: int,
                       bytes_per_pixel: int, bytes_per_line: int) -> str:
    """从原始像素数据提取文字（内存直传，跳过文件 I/O）。

    Args:
        image_data: 原始像素数据（RGB 或 RGBA 字节）。
        width:     图像宽度（像素）。
        height:    图像高度（像素）。
        bytes_per_pixel: 每像素字节数（3=RGB, 4=RGBA）。
        bytes_per_line:  每行字节数（stride）。

    Returns:
        提取的文字（UTF-8），可能为空字符串。

    Raises:
        RuntimeError: 引擎未初始化或识别失败。
    """
    if not _engine_initialized:
        raise RuntimeError("OCR 引擎未初始化，请先调用 init_ocr()")

    data_len = len(image_data)
    expected = bytes_per_line * height
    if data_len < expected:
        raise RuntimeError(
            f"像素数据不足：期望 ≥{expected} 字节，实际 {data_len} 字节")

    data_array = (ctypes.c_ubyte * data_len).from_buffer_copy(image_data)
    data_ptr = ctypes.cast(data_array, ctypes.POINTER(ctypes.c_ubyte))

    text_buf = ctypes.create_string_buffer(65536)
    error_buf = ctypes.create_string_buffer(1024)

    ret = _get_dll().snap_ocr_extract_text(
        data_ptr, width, height,
        bytes_per_pixel, bytes_per_line,
        text_buf, 65536,
        error_buf, 1024,
    )

    if ret == SNAP_OCR_OK:
        result = (text_buf.value.decode("utf-8", errors="replace")
                  if text_buf.value else "")
        log_debug(f"[snap_ocr PY] extract_text_raw: OK len={len(result)}")
        return result

    error_msg = (error_buf.value.decode("utf-8", errors="replace")
                 if error_buf.value
                 else _ERROR_MESSAGES.get(ret, f"未知错误 ({ret})"))
    log_error(f"[snap_ocr PY] extract_text_raw: FAIL ret={ret} "
              f"error={error_msg}")
    raise RuntimeError(f"OCR 识别失败: {error_msg}")


def extract_text_file(image_path: str) -> str:
    """从图片文件提取文字（引擎需已通过 init_ocr 初始化）。

    Args:
        image_path: 图片文件路径（PNG/JPEG/BMP 等）

    Returns:
        提取的文字（UTF-8），可能为空字符串

    Raises:
        RuntimeError: 引擎未初始化或识别失败
    """
    if not _engine_initialized:
        raise RuntimeError("OCR 引擎未初始化，请先调用 init_ocr()")

    text_buf = ctypes.create_string_buffer(65536)
    error_buf = ctypes.create_string_buffer(1024)

    ret = _get_dll().snap_ocr_extract_text_file(
        image_path.encode("utf-8"),
        text_buf, 65536,
        error_buf, 1024,
    )

    if ret == SNAP_OCR_OK:
        result = text_buf.value.decode("utf-8", errors="replace") if text_buf.value else ""
        log_debug(f"[snap_ocr PY] extract_text_file: OK len={len(result)}")
        return result

    error_msg = error_buf.value.decode("utf-8", errors="replace") if error_buf.value else _ERROR_MESSAGES.get(ret, f"未知错误 ({ret})")
    log_error(f"[snap_ocr PY] extract_text_file: FAIL ret={ret} error={error_msg}")
    raise RuntimeError(f"OCR 识别失败: {error_msg}")


def extract_text(image_path: str,
                 ocr_langs: str = "chi_sim+eng+jpn+kor") -> str:
    """从图片文件提取文字（DLL 方案）。

    自动初始化引擎（首次调用或语言切换时），
    保持与 core/ocr.py 完全相同的函数签名。

    Args:
        image_path: 图片文件路径。
        ocr_langs:  Tesseract 语言代码（+ 分隔）。

    Returns:
        提取的文字（已 strip），失败时可能为空字符串。

    Raises:
        ImportError: DLL 不可用。
        RuntimeError: Tesseract 初始化失败或其他运行时错误。
    """
    log_info(f"[snap_ocr PY] extract_text: START "
          f"image_path={image_path} ocr_langs={ocr_langs}")

    # Step 1: 检查 DLL 可用性
    if not is_available():
        log_error("[snap_ocr PY] extract_text: FAIL — DLL not available")
        raise ImportError(
            "snaplens_ocr.dll 不可用，请确认已编译 OCR 模块。\n"
            "详见 docs/native-build-notes.md"
        )

    # Step 2: 查找 tessdata
    tessdata = find_tessdata_dir()
    if not tessdata:
        log_error("[snap_ocr PY] extract_text: FAIL — tessdata not found")
        raise RuntimeError(
            "未找到 Tesseract 语言包（tessdata）。\n\n"
            "请将 .traineddata 文件放入以下任一目录：\n"
            "  · 项目中的 sdk/tesseract/tessdata/\n"
            "  · 项目中的 tesseract/tessdata/\n"
            "  · C:\\Program Files\\Tesseract-OCR\\tessdata\\\n\n"
            "语言包可在设置对话框中在线下载。"
        )

    # Step 3: 确保引擎已初始化
    # Tesseract 的 data_path 参数直接指向含 .traineddata 的目录，
    # 不自动追加 tessdata/ 子路径
    data_path = tessdata
    _ensure_engine(data_path, ocr_langs)

    # Step 4: 执行 OCR
    if not os.path.isfile(image_path):
        log_error(f"[snap_ocr PY] extract_text: FAIL — file not found: {image_path}")
        raise RuntimeError(f"图片文件不存在: {image_path}")

    file_size = os.path.getsize(image_path)
    log_debug(f"[snap_ocr PY] extract_text: calling DLL extract_text_file "
          f"(file_size={file_size} bytes)")
    text = extract_text_file(image_path)
    result = text.strip()
    log_info(f"[snap_ocr PY] extract_text: OK result_len={len(result)} "
          f"preview={repr(result[:60])}")
    return result


def extract_text_pixels(image_data: bytes, width: int, height: int,
                        bytes_per_pixel: int, bytes_per_line: int,
                        ocr_langs: str = "chi_sim+eng+jpn+kor") -> str:
    """从原始像素数据提取文字（DLL 方案，内存直传）。

    自动初始化引擎（首次调用或语言切换时），
    不经过临时文件，直接通过 snap_ocr_extract_text() 传递像素数据。

    Args:
        image_data:      原始像素数据（RGB888 或 RGBA8888 字节）。
        width:           图像宽度（像素）。
        height:          图像高度（像素）。
        bytes_per_pixel: 每像素字节数（3=RGB, 4=RGBA）。
        bytes_per_line:  每行字节数（stride，通常 = width * bytes_per_pixel）。
        ocr_langs:       Tesseract 语言代码（+ 分隔）。

    Returns:
        提取的文字（已 strip），失败时可能为空字符串。

    Raises:
        ImportError: DLL 不可用。
        RuntimeError: Tesseract 初始化失败或其他运行时错误。
    """
    log_info(f"[snap_ocr PY] extract_text_pixels: START "
             f"{width}x{height} bpp={bytes_per_pixel} bpl={bytes_per_line} "
             f"ocr_langs={ocr_langs}")

    # Step 1: 检查 DLL 可用性
    if not is_available():
        log_error("[snap_ocr PY] extract_text_pixels: FAIL — DLL not available")
        raise ImportError(
            "snaplens_ocr.dll 不可用，请确认已编译 OCR 模块。\n"
            "详见 docs/native-build-notes.md"
        )

    # Step 2: 查找 tessdata
    tessdata = find_tessdata_dir()
    if not tessdata:
        log_error("[snap_ocr PY] extract_text_pixels: FAIL — tessdata not found")
        raise RuntimeError(
            "未找到 Tesseract 语言包（tessdata）。\n\n"
            "请将 .traineddata 文件放入以下任一目录：\n"
            "  · 项目中的 sdk/tesseract/tessdata/\n"
            "  · 项目中的 tesseract/tessdata/\n"
            "  · C:\\Program Files\\Tesseract-OCR\\tessdata\\\n\n"
            "语言包可在设置对话框中在线下载。"
        )

    # Step 3: 确保引擎已初始化
    _ensure_engine(tessdata, ocr_langs)

    # Step 4: 执行 OCR（像素直传）
    log_debug(f"[snap_ocr PY] extract_text_pixels: calling DLL extract_text_raw "
              f"(data_bytes={len(image_data)})")
    text = extract_text_raw(image_data, width, height,
                            bytes_per_pixel, bytes_per_line)
    result = text.strip()
    log_info(f"[snap_ocr PY] extract_text_pixels: OK result_len={len(result)} "
             f"preview={repr(result[:60])}")
    return result


# ----------------------------------------------------------------- tessdata 查找

def find_tessdata_dir() -> str | None:
    """查找 tessdata 目录（含语言包的路径）。

    返回 tessdata 目录本身，如 D:\\...\\sdk\\tesseract\\tessdata。

    查找顺序：
    0. 用户自定义路径（ocr_tessdata_dir 设置项）
    1. 项目 sdk/tesseract/tessdata/
    2. 项目 tesseract/tessdata/
    3. 项目根目录下的 tessdata/
    4. C:\\Program Files\\Tesseract-OCR\\tessdata\\
    5. C:\\Program Files (x86)\\Tesseract-OCR\\tessdata\\
    """
    # 优先使用用户自定义路径
    if _custom_tessdata:
        td = os.path.normpath(_custom_tessdata)
        if os.path.isdir(td):
            try:
                files = os.listdir(td)
            except OSError:
                pass
            else:
                if any(f.endswith(".traineddata") for f in files):
                    log_info(f"[snap_ocr PY] find_tessdata_dir: using custom path {td}")
                    return td
                # 自定义路径有效但无语言包 — 仍然使用它（供下载）
                log_warning(f"[snap_ocr PY] find_tessdata_dir: custom path exists "
                      f"but no traineddata: {td}")
                return td
        log_warning(f"[snap_ocr PY] find_tessdata_dir: WARNING custom path invalid: {td}")

    if getattr(sys, "frozen", False):
        candidates = [
            os.path.dirname(sys.executable),
        ]
    else:
        # __file__ = snaplens/ocr/native_binding.py → 项目根目录
        project_root = Path(__file__).parent.parent.parent
        candidates = [
            str(project_root),
        ]

    # 系统安装路径
    candidates.extend([
        r"C:\Program Files\Tesseract-OCR",
        r"C:\Program Files (x86)\Tesseract-OCR",
    ])

    for parent in candidates:
        for td_rel in ("sdk/tesseract/tessdata", "tesseract/tessdata", "tessdata"):
            td = os.path.join(parent, td_rel)
            if not os.path.isdir(td):
                continue
            try:
                files = os.listdir(td)
            except OSError:
                continue
            if any(f.endswith(".traineddata") for f in files):
                result = os.path.normpath(td)
                log_info(f"[snap_ocr PY] find_tessdata_dir: found {result}")
                return result

    log_warning("[snap_ocr PY] find_tessdata_dir: NOT FOUND in any candidate path")
    return None
