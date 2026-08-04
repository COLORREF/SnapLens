"""OCR 模块 C++ 原生绑定 + Python 公共接口。

通过 ctypes 加载 snaplens_ocr.dll 调用 Tesseract 5.5 C API，
替代原有的 pytesseract 子进程方案。

用法：
    from snaplens.ocr import extract_text, find_tessdata_dir

    tessdata = find_tessdata_dir()          # 查找 tessdata 目录
    text = extract_text(image_path, "chi_sim+eng+jpn+kor")
"""

from .native_binding import (
    apply_settings,
    extract_text,
    extract_text_file,
    extract_text_pixels,
    extract_text_raw,
    find_tessdata_dir,
    get_version,
    init_ocr,
    is_available,
    shutdown_ocr,
)
