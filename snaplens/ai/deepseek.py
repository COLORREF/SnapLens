"""DeepSeek 翻译器（向后兼容别名）。

DeepSeek V4 是纯文本模型，不支持视觉输入。
因此采用 OCR + API 两步策略：OCR 提取文字 → DeepSeek 翻译。

此模块保留为向后兼容别名，实际实现见 openai_compat.OpenAICompatibleTranslator。
新代码请直接使用 OpenAICompatibleTranslator 或工厂函数 create_translator()。
"""
from .openai_compat import OpenAICompatibleTranslator as DeepSeekTranslator  # noqa: F401
