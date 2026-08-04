"""共享 OCR 模块：Tesseract 查找和文字提取。

调用方（ocr_service.py、openai_compat.py、settings_dialog.py、setup_wizard.py）
通过此模块执行 OCR 或查找 tessdata 路径。

实现：通过 C++ DLL（snaplens_ocr.dll → Tesseract 5.5 C API）进程内调用，
不再依赖 pytesseract 子进程。
"""
import os
import sys

from ..log import log_debug, log_info

# Tesseract 下载地址（供外部引用）
TESSERACT_DOWNLOAD_URL = "https://github.com/UB-Mannheim/tesseract/wiki"

_tessdata_dir_cache: str | None = None


def _app_dir() -> str:
    """应用根目录（兼容开发模式和 PyInstaller 打包）。"""
    if getattr(sys, "frozen", False):
        return os.path.dirname(sys.executable)
    return os.path.dirname(
        os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    )


def _default_tessdata_path() -> str:
    """首选 tessdata 目录路径（用于创建和下载）。"""
    base = os.path.dirname(sys.executable) if getattr(sys, "frozen", False) else _app_dir()
    return os.path.normpath(os.path.join(base, "sdk", "tesseract", "tessdata"))


def find_tessdata_dir() -> str | None:
    """查找 tessdata 目录（含语言包的路径）。结果缓存，避免重复扫描。
    """
    global _tessdata_dir_cache
    if _tessdata_dir_cache is not None:
        return _tessdata_dir_cache

    candidates = []
    if getattr(sys, "frozen", False):
        candidates.append(os.path.dirname(sys.executable))
    else:
        candidates.append(_app_dir())

    candidates.extend([
        r"C:\Program Files\Tesseract-OCR",
        r"C:\Program Files (x86)\Tesseract-OCR",
    ])

    # 在每个候选父目录中检查各种可能的 tessdata 子路径
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
                _tessdata_dir_cache = result
                log_debug(f"[snap_ocr PY] find_tessdata_dir: found {result}")
                return result

    # 找不到已有语言包 → 返回首选默认路径，供 settings_dialog 下载
    fallback = _default_tessdata_path()
    _tessdata_dir_cache = fallback
    log_debug(f"[snap_ocr PY] find_tessdata_dir: no traineddata found, "
             f"fallback to {fallback}")
    return fallback


def extract_text(image_path: str, ocr_langs: str = "chi_sim+eng+jpn+kor") -> str:
    """从图片中提取文字（C++ DLL 方案，Tesseract 5.5 C API）。

    Args:
        image_path: 图片文件路径。
        ocr_langs: Tesseract 语言代码（+ 分隔），如 "chi_sim+eng"。

    Returns:
        提取的文字（已 strip），失败时可能为空字符串。
    """
    from ..ocr import extract_text as dll_extract_text

    log_info(f"[snap_ocr PY] extract_text: image_path={image_path} ocr_langs={ocr_langs}")
    text = dll_extract_text(image_path, ocr_langs)
    log_info(f"[snap_ocr PY] extract_text: OK, result_len={len(text)}")
    return text


def extract_text_from_pixmap(pixmap, ocr_langs: str = "chi_sim+eng+jpn+kor") -> str:
    """从 QPixmap 直接提取文字（像素直传 DLL，跳过临时文件 + PNG 编解码）。

    与 extract_text() 相比，省去 QPixmap → temp PNG → 磁盘 I/O
    → Leptonica pixRead() 解码的整个文件往返。像素数据直接从
    QImage 的 constBits() 传入 DLL 的 snap_ocr_extract_text()。

    Args:
        pixmap: PySide6 QPixmap 对象。
        ocr_langs: Tesseract 语言代码（+ 分隔），如 "chi_sim+eng"。

    Returns:
        提取的文字（已 strip），失败时可能为空字符串。
    """
    from PySide6.QtGui import QImage

    from ..ocr import extract_text_pixels as dll_extract_text_pixels

    # 转为 RGB888 QImage（Tesseract 接受 RGB 三通道）
    image = pixmap.toImage()
    if image.format() != QImage.Format.Format_RGB888:
        image = image.convertToFormat(QImage.Format.Format_RGB888)

    width = image.width()
    height = image.height()
    bpp = 3          # RGB888 = 3 bytes per pixel
    bpl = image.bytesPerLine()
    byte_count = bpl * height

    # 获取原始像素数据
    # PySide6 不同版本 constBits() 返回类型不同（QByteArray / bytes / voidptr）。
    # 用 bytes() 直接试，因为 Python bytes() 原生支持 QByteArray / bytes / bytearray。
    bits_ptr = image.constBits()
    try:
        data = bytes(bits_ptr)
    except (TypeError, ValueError):
        # voidptr 类型：bytes() 失败，走 ctypes 指针读取
        if hasattr(bits_ptr, 'asarray'):
            data = bytes(bits_ptr.asarray(byte_count))
        else:
            import ctypes as _ct
            ptr = _ct.cast(int(bits_ptr), _ct.POINTER(_ct.c_ubyte))
            data = _ct.string_at(ptr, byte_count)

    log_info(f"[snap_ocr PY] extract_text_from_pixmap: "
             f"{width}x{height} bpl={bpl} bytes={byte_count} ocr_langs={ocr_langs}")

    text = dll_extract_text_pixels(data, width, height, bpp, bpl, ocr_langs)
    log_info(f"[snap_ocr PY] extract_text_from_pixmap: OK, result_len={len(text)}")
    return text
