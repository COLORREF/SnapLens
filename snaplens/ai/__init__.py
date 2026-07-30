"""AI 翻译模块。

通过工厂函数按 provider 名称创建对应的 AITranslator 实例，
上层代码不感知具体服务商实现。

支持的服务商通过 PROVIDER_CONFIGS 注册表管理，
每个条目包含默认 API 地址和默认模型名称。
"""
from .base import AITranslator
from .openai_compat import OpenAICompatibleTranslator

__all__ = ["AITranslator", "OpenAICompatibleTranslator",
           "create_translator", "PROVIDER_CONFIGS", "list_providers"]

# ----------------------------------------------------------------- 厂商注册表
# 每个厂商包含：
#   base_url: 默认 API 地址（OpenAI 兼容）
#   label: 中文显示名称（UI 用）
# 注意：部分厂商的 /models 端点可能不可用（如千问、豆包），
#       获取失败时设置页会提示手动输入模型名称。
PROVIDER_CONFIGS: dict[str, dict] = {
    "deepseek": {
        "base_url": "https://api.deepseek.com/v1",
        "label": "DeepSeek (深度求索)",
    },
    "openai": {
        "base_url": "https://api.openai.com/v1",
        "label": "OpenAI",
    },
    "qwen": {
        "base_url": "https://dashscope.aliyuncs.com/compatible-mode/v1",
        "label": "通义千问 (Qwen)",
    },
    "kimi": {
        "base_url": "https://api.moonshot.cn/v1",
        "label": "Kimi (月之暗面)",
    },
    "glm": {
        "base_url": "https://open.bigmodel.cn/api/paas/v4/",
        "label": "智谱 GLM (ZhipuAI)",
    },
    "hunyuan": {
        "base_url": "https://api.hunyuan.cloud.tencent.com/v1",
        "label": "腾讯混元 (HunYuan)",
    },
    "doubao": {
        "base_url": "https://ark.cn-beijing.volces.com/api/v3",
        "label": "豆包 (火山方舟)",
    },
    "qianfan": {
        "base_url": "https://qianfan.baidubce.com/v2",
        "label": "文心一言 (百度千帆)",
    },
}


def list_providers() -> list[str]:
    """返回所有支持的服务商标识符列表。"""
    return list(PROVIDER_CONFIGS.keys())


def create_translator(
    provider: str,
    api_key: str,
    api_base: str = "",
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
    on_thinking=None,
) -> AITranslator:
    """根据服务商名称创建翻译器实例。

    所有已注册的服务商均使用 OpenAICompatibleTranslator（统一底层），
    仅通过 base_url 和默认 model 区分。

    Args:
        provider: 服务商标识符，如 "deepseek"、"openai"、"qwen"。
        api_key: API 密钥。
        api_base: API 地址（为空时使用厂商默认值）。
        model: 模型名称（用户手动输入或自动获取）。
        其余参数见 OpenAICompatibleTranslator。

    Returns:
        AITranslator 实例。

    Raises:
        ValueError: 不支持的 provider。
    """
    provider_lower = provider.lower()
    config = PROVIDER_CONFIGS.get(provider_lower)
    if config is None:
        raise ValueError(
            f"不支持的 AI 服务商：{provider}\n"
            f"当前支持：{', '.join(list_providers())}"
        )

    return OpenAICompatibleTranslator(
        api_key=api_key,
        api_base=api_base or config["base_url"],
        model=model,
        timeout=timeout,
        ocr_langs=ocr_langs,
        temperature=temperature,
        max_tokens=max_tokens,
        top_p=top_p,
        frequency_penalty=frequency_penalty,
        presence_penalty=presence_penalty,
        seed=seed,
        stream_thinking=stream_thinking,
        on_thinking=on_thinking,
    )
