"""共享 OCR 模块：Tesseract 查找、配置和文字提取。

deepseek.py、ocr_service.py、settings_dialog.py、setup_wizard.py
均通过此模块获取 Tesseract 路径/目录，或执行 OCR 提取。
"""
import os
import sys

# Tesseract 便携版下载地址（Release 构建用）
TESSERACT_DOWNLOAD_URL = "https://github.com/UB-Mannheim/tesseract/wiki"


def _app_dir() -> str:
    """应用根目录（兼容开发模式和 PyInstaller 打包）。"""
    if getattr(sys, "frozen", False):
        return os.path.dirname(sys.executable)
    # 开发模式：__file__ = snaplens/core/ocr.py，往上 3 级到项目根
    return os.path.dirname(
        os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    )


def find_tesseract() -> tuple[str | None, str | None]:
    """查找 Tesseract 可执行文件和 tessdata 目录。

    Returns:
        (tesseract_exe_path, tessdata_dir) 或 (None, None)
    """
    candidates = [
        os.path.join(_app_dir(), "tesseract"),
        r"C:\Program Files\Tesseract-OCR",
        r"C:\Program Files (x86)\Tesseract-OCR",
    ]
    for tesseract_dir in candidates:
        exe_path = os.path.join(tesseract_dir, "tesseract.exe")
        tessdata = os.path.join(tesseract_dir, "tessdata")
        if os.path.isfile(exe_path) and os.path.isdir(tessdata):
            return exe_path, tessdata
    return None, None


def find_tessdata_dir() -> str | None:
    """查找 tessdata 目录（语言包路径）。"""
    exe_path, tessdata_dir = find_tesseract()
    return tessdata_dir


def setup_tesseract(pytesseract) -> None:
    """配置 pytesseract 的 Tesseract 路径。"""
    exe_path, tessdata_dir = find_tesseract()
    if exe_path is not None:
        pytesseract.pytesseract.tesseract_cmd = exe_path
    if tessdata_dir is not None:
        os.environ.setdefault("TESSDATA_PREFIX", tessdata_dir)

    try:
        pytesseract.get_tesseract_version()
    except pytesseract.TesseractNotFoundError:
        raise RuntimeError(
            "未找到 Tesseract OCR。\n\n"
            "开发环境：请从以下地址下载安装（勾选中文语言包）：\n"
            f"{TESSERACT_DOWNLOAD_URL}\n\n"
            "不想自行配置？请使用 Release 中已打包好的版本。\n"
            "打包部署：将便携版放入 exe 同级的 tesseract/ 目录即可。"
        )


def extract_text(image_path: str, ocr_langs: str = "chi_sim+eng+jpn+kor") -> str:
    """从图片中提取文字。

    Args:
        image_path: 图片文件路径。
        ocr_langs: Tesseract 语言代码（+ 分隔），如 "chi_sim+eng"。

    Returns:
        提取的文字（已 strip），失败时可能为空字符串。
    """
    try:
        import pytesseract
        from PIL import Image
    except ImportError:
        raise ImportError("pytesseract / Pillow not installed")

    setup_tesseract(pytesseract)

    img = Image.open(image_path)
    try:
        text = pytesseract.image_to_string(img, lang=ocr_langs)
    except pytesseract.TesseractError as e:
        if "not find" in str(e).lower() or "failed to load" in str(e).lower():
            text = pytesseract.image_to_string(img, lang="eng")
        else:
            raise
    return text.strip()
