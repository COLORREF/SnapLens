"""OpenAI 兼容翻译器。

通过 C++ DLL (Qt Network) 调用任意 OpenAI 兼容接口
（DeepSeek / OpenAI / 通义千问等），均采用 OCR + API 两步策略：

1. snaplens_ocr.dll（Tesseract 5.5 C API）从图片中提取原始文字
2. 将提取的文字发给 AI 翻译为目标语言

不区分服务商，仅通过 api_base 和 model 参数区分。
"""
from .base import AITranslator
from ..core.ocr import extract_text, extract_text_from_pixmap


class OpenAICompatibleTranslator(AITranslator):
    """基于 C++ DLL 的图片文字翻译器，兼容所有 OpenAI 接口的服务商。

    Pipeline:
    1. snaplens_ocr.dll 从图片中提取原始文字
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
                  image_path: str = "",
                  target_lang: str = "",
                  prompt_template: str = "",
                  pixmap=None) -> dict:
        """翻译图片中的文字为目标语言。

        Args:
            image_path: 本地图片文件路径（兜底路径，pixmap 存在时忽略）。
            target_lang: 目标语言，如 "简体中文"。
            prompt_template: 包含 {target_lang} 占位符的提示词模板。
            pixmap: QPixmap 截图（优先使用，像素直传 DLL）。

        Returns:
            dict: 详见基类 AITranslator.translate 文档。
        """
        if not self._api_key:
            raise ValueError("API Key 未设置，请在设置中配置")

        # 第一步：OCR 提取文字
        try:
            if pixmap is not None:
                source_text = extract_text_from_pixmap(pixmap, self._ocr_langs)
            elif image_path:
                source_text = extract_text(image_path, self._ocr_langs)
            else:
                raise ValueError("必须提供 pixmap 或 image_path 之一")
        except ImportError as e:
            raise RuntimeError(
                f"OCR DLL 不可用：{e}\n\n"
                "请确认已编译 snaplens_ocr.dll（详见 docs/native-build-notes.md）"
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
