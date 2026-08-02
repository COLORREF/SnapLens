"""OpenAI 兼容翻译器。

通过 openai SDK 调用任意 OpenAI 兼容接口
（DeepSeek / OpenAI / 通义千问等），均采用 OCR + API 两步策略：

1. pytesseract 从图片中提取原始文字
2. 将提取的文字发给 AI 翻译为目标语言

不区分服务商，仅通过 api_base 和 model 参数区分。
"""
from .base import AITranslator
from ..core.ocr import extract_text as ocr_extract_text
from ..core.ocr import TESSERACT_DOWNLOAD_URL


class OpenAICompatibleTranslator(AITranslator):
    """基于 openai SDK 的图片文字翻译器，兼容所有 OpenAI 接口的服务商。

    Pipeline:
    1. pytesseract 从图片中提取原始文字
    2. 将提取的文字发给 AI API 翻译为目标语言

    支持流式和非流式两种模式：
    - 流式：思考内容通过 on_thinking 回调逐 chunk 实时推送
    - 非流式：思考内容在 API 调用完成后一次性返回
    """

    def __init__(self,
                 api_key: str,
                 api_base: str = "https://api.deepseek.com/v1",
                 model: str = "",
                 timeout: int = 30,
                 ocr_langs: str = "chi_sim+eng+jpn+kor",
                 temperature: float = 0.1,
                 max_tokens: int = 4096,
                 top_p: float = 1.0,
                 frequency_penalty: float = 0.0,
                 presence_penalty: float = 0.0,
                 seed: int = 0,
                 stream_thinking: bool = True,
                 on_thinking=None):
        self._api_key = api_key
        self._api_base = api_base
        self._model = model
        self._timeout = timeout
        self._ocr_langs = ocr_langs
        self._temperature = temperature
        self._max_tokens = max_tokens
        self._top_p = top_p
        self._frequency_penalty = frequency_penalty
        self._presence_penalty = presence_penalty
        self._seed = seed
        self._stream_thinking = stream_thinking
        self._on_thinking = on_thinking

    def translate(self,
                  image_path: str,
                  target_lang: str,
                  prompt_template: str) -> dict:
        """翻译图片中的文字为目标语言。

        详见基类 AITranslator.translate 文档。
        """
        if not self._api_key:
            raise ValueError("API Key 未设置，请在设置中配置")

        # 第一步：OCR 提取文字
        try:
            source_text = ocr_extract_text(image_path, self._ocr_langs)
        except ImportError:
            raise RuntimeError(
                "OCR 引擎未安装，请运行：\n"
                "pip install pytesseract Pillow\n\n"
                "并安装 Tesseract OCR（勾选中文语言包）：\n"
                f"{TESSERACT_DOWNLOAD_URL}\n\n"
                "打包部署时可将便携版放入 exe 同级的 tesseract/ 目录自动识别。"
            )
        except RuntimeError as e:
            raise RuntimeError(f"文字提取失败：{e}")

        if not source_text.strip():
            return {"translated": "图片中未检测到文字",
                    "ocr_text": "", "thinking": ""}

        # 第二步：AI 翻译
        prompt = prompt_template.format(target_lang=target_lang)
        full_prompt = f"{prompt}\n\n原文内容：\n{source_text}"

        translated, thinking = self._call_api(full_prompt)
        return {
            "translated": translated,
            "ocr_text": source_text,
            "thinking": thinking,
        }

    def _call_api(self, prompt: str) -> tuple[str, str]:
        """调用 AI API（复用共享客户端），根据 stream_thinking 选择模式。"""
        # 惰性导入，避免 core/api_client → ai/native_binding → ai/__init__
        # → openai_compat → core/api_client 的循环依赖
        from ..core.api_client import call_chat, call_chat_stream

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
        if self._stream_thinking and self._on_thinking:
            result = call_chat_stream(
                **common,
                on_thinking=self._on_thinking,
            )
        else:
            result = call_chat(**common)
        return result["content"], result["thinking"]
