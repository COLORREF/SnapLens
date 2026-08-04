"""文本翻译后台服务：将文本发送给 AI API 进行翻译。

复用 core.api_client 统一 API 调用，避免重复代码。
"""
from PySide6.QtCore import QThread, Signal

from .api_client import call_chat, call_chat_stream


class TextTranslateService(QThread):
    """后台线程执行文本翻译。"""

    translated = Signal(str)   # 翻译完成：翻译文本
    thinking = Signal(str)     # AI 思考过程
    error = Signal(str)         # 翻译出错：错误消息

    def __init__(self,
                 source_text: str,
                 target_lang: str,
                 scenario: str,
                 prompt_template: str,
                 full_prompt: str = "",
                 api_key: str = "",
                 api_base: str = "",
                 model: str = "",
                 timeout: int = 30,
                 temperature: float = 0.1,
                 max_tokens: int = 4096,
                 top_p: float = 1.0,
                 frequency_penalty: float = 0.0,
                 presence_penalty: float = 0.0,
                 seed: int = 0,
                 stream_thinking: bool = True,
                 parent=None):
        super().__init__(parent)
        self._source_text = source_text
        self._target_lang = target_lang
        self._scenario = scenario
        self._prompt_template = prompt_template
        self._full_prompt = full_prompt
        self._api_key = api_key
        self._api_base = api_base
        self._model = model
        self._timeout = timeout
        self._temperature = temperature
        self._max_tokens = max_tokens
        self._top_p = top_p
        self._frequency_penalty = frequency_penalty
        self._presence_penalty = presence_penalty
        self._seed = seed
        self._stream_thinking = stream_thinking

    def run(self):
        try:
            if not self._source_text.strip():
                raise ValueError("翻译内容为空")

            # 如果传入了完整提示词则直接使用，否则从模板构建
            if self._full_prompt:
                prompt = self._full_prompt
            else:
                prompt = self._prompt_template.format(
                    target_lang=self._target_lang,
                    scenario=self._scenario,
                    source_text=self._source_text,
                )

            common = dict(
                api_key=self._api_key,
                api_base=self._api_base,
                model=self._model,
                messages=[{"role": "user", "content": prompt}],
                timeout=self._timeout,
                temperature=self._temperature,
                max_tokens=self._max_tokens,
                top_p=self._top_p,
                frequency_penalty=self._frequency_penalty,
                presence_penalty=self._presence_penalty,
                seed=self._seed,
            )

            if self._stream_thinking:
                result = call_chat_stream(
                    **common,
                    on_thinking=lambda text: self.thinking.emit(text),
                )
            else:
                result = call_chat(**common)
                thinking_text = result.get("thinking", "")
                if thinking_text:
                    self.thinking.emit(thinking_text)

            self.translated.emit(result["content"])

        except (ValueError, RuntimeError, ConnectionError, TimeoutError) as e:
            self.error.emit(str(e))
        except Exception as e:
            self.error.emit(f"翻译失败：{e}")
