"""OCR 识别后台服务。

复用 core.ocr 模块，避免重复 Tesseract 查找/配置/提取逻辑。
"""
from PySide6.QtCore import QThread, Signal

from ..core.ocr import extract_text


class OcrService(QThread):
    """后台执行 OCR 文字提取的线程。"""

    finished = Signal(str)
    error = Signal(str)

    def __init__(self, image_path: str, ocr_langs: str = "chi_sim+eng+jpn+kor",
                 parent=None):
        super().__init__(parent)
        self._image_path = image_path
        self._ocr_langs = ocr_langs

    def run(self):
        try:
            result = extract_text(self._image_path, self._ocr_langs)
            if not result:
                result = "图片中未检测到文字"
            self.finished.emit(result)
        except ImportError:
            self.error.emit(
                "OCR 引擎未安装，请运行：pip install pytesseract Pillow"
            )
        except RuntimeError as e:
            self.error.emit(str(e))
        except Exception as e:
            self.error.emit(f"OCR 失败：{e}")
