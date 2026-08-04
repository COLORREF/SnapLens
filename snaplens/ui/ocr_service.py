"""OCR 识别后台服务。

复用 core.ocr 模块（C++ DLL 方案），以 QPixmap 像素直传替代临时文件，
在独立线程中执行以避免阻塞 UI。
"""
from PySide6.QtCore import QThread, Signal
from PySide6.QtGui import QPixmap

from ..core.ocr import extract_text_from_pixmap


class OcrService(QThread):
    """后台执行 OCR 文字提取的线程。

    Args:
        pixmap: QPixmap 截图（像素直传 DLL，跳过临时文件）。
        ocr_langs: Tesseract 语言代码（+ 分隔）。
    """

    finished = Signal(str)
    error = Signal(str)

    def __init__(self, pixmap: QPixmap, ocr_langs: str = "chi_sim+eng+jpn+kor",
                 parent=None):
        super().__init__(parent)
        self._pixmap = pixmap
        self._ocr_langs = ocr_langs

    def run(self):
        try:
            result = extract_text_from_pixmap(self._pixmap, self._ocr_langs)
            if not result:
                result = "图片中未检测到文字"
            self.finished.emit(result)
        except ImportError as e:
            self.error.emit(
                f"OCR DLL 不可用：{e}\n\n"
                "请确认已编译 snaplens_ocr.dll（详见 docs/native-build-notes.md）"
            )
        except RuntimeError as e:
            self.error.emit(str(e))
        except Exception as e:
            self.error.emit(f"OCR 失败：{e}")
