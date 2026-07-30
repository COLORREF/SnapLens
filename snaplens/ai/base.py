"""AI 翻译器抽象接口。

所有翻译服务商的实现都继承此基类，统一 translate() 签名。
"""
from abc import ABC, abstractmethod


class AITranslator(ABC):
    """AI 翻译器抽象基类。

    translate() 接收图片文件路径 + 目标语言 + 提示词模板，
    返回翻译后的文本，出错时抛出异常。
    """

    @abstractmethod
    def translate(self,
                  image_path: str,
                  target_lang: str,
                  prompt_template: str) -> dict:
        """翻译图片中的文字为目标语言。

        Args:
            image_path: 本地图片文件路径。
            target_lang: 目标语言，如 "简体中文"。
            prompt_template: 包含 {target_lang} 占位符的提示词模板。

        Returns:
            dict，包含以下键：
            - "translated": 翻译后的纯文本
            - "ocr_text": OCR 提取的原始文字（可能为空字符串）
            - "thinking": AI 思考/推理过程（可能为空字符串）

        Raises:
            ValueError: 配置无效（如 API key 为空）。
            ConnectionError: 网络连接失败。
            TimeoutError: 请求超时。
            RuntimeError: API 返回错误（鉴权失败/额度不足等）。
        """
        ...
