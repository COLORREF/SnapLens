"""AI 翻译后台服务。

将翻译请求放到独立线程中执行，通过 Qt 信号通知结果，
避免阻塞主 UI 线程。以 QPixmap 像素直传替代临时文件。
"""
from PySide6.QtCore import QThread, Signal
from PySide6.QtGui import QPixmap

from ..ai import create_translator
from ..core.settings import Settings


class TranslateService(QThread):
    """后台执行图片翻译的线程。

    Args:
        pixmap: QPixmap 截图（像素直传，跳过临时文件 PNG 编解码）。
        target_lang: 翻译目标语言。
        settings: Settings 实例。
    """

    # 翻译完成信号：翻译文本
    translated = Signal(str)
    # OCR 原始文本信号：从图片中提取的原文
    ocr_text = Signal(str)
    # AI 思考/推理过程（如果模型启用了思考模式）
    thinking = Signal(str)
    # 翻译出错信号：错误消息
    error = Signal(str)

    def __init__(self,
                 pixmap: QPixmap,
                 target_lang: str,
                 settings: Settings,
                 parent=None):
        super().__init__(parent)
        self._pixmap = pixmap
        self._target_lang = target_lang
        self._settings = settings

    def run(self):
        try:
            translator = create_translator(
                provider=self._settings.ai_provider,
                api_key=self._settings.ai_api_key,
                api_base=self._settings.ai_api_base,
                model=self._settings.ai_model,
                timeout=self._settings.ai_timeout,
                ocr_langs=self._settings.ai_ocr_langs,
                temperature=self._settings.ai_temperature,
                max_tokens=self._settings.ai_max_tokens,
                top_p=self._settings.ai_top_p,
                frequency_penalty=self._settings.ai_frequency_penalty,
                presence_penalty=self._settings.ai_presence_penalty,
                seed=self._settings.ai_seed,
                stream_thinking=self._settings.ai_stream_thinking,
                on_thinking=lambda text: self.thinking.emit(text),
            )
            result = translator.translate(
                pixmap=self._pixmap,
                target_lang=self._target_lang,
                prompt_template=self._settings.ai_translation_prompt,
            )
            # OCR 原文和思考内容：流式模式下思考已在上面实时推送
            ocr = result.get("ocr_text", "")
            if ocr:
                self.ocr_text.emit(ocr)
            if not self._settings.ai_stream_thinking:
                thinking = result.get("thinking", "")
                if thinking:
                    self.thinking.emit(thinking)
            self.translated.emit(result.get("translated", ""))
        except Exception as e:
            self.error.emit(str(e))
