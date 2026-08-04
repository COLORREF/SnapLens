"""共享 API 客户端。

基于 C++ snaplens_ai.dll（Qt Network 后端），
兼容 DeepSeek / OpenAI / 通义千问及任何实现 OpenAI 兼容接口的服务商。

函数签名与旧版 openai SDK 实现完全一致，上层代码无需修改。
"""
import ctypes

from ..ai.native_binding import call_chat as _native_call_chat
from ..ai.native_binding import call_chat_stream as _native_call_chat_stream
from ..ai.native_binding import list_models as _native_list_models
from ..log import log_error, log_info


def call_chat(
    api_key: str,
    api_base: str,
    model: str,
    messages: list[dict],
    timeout: int = 30,
    temperature: float = 0.1,
    max_tokens: int = 4096,
    top_p: float = 1.0,
    frequency_penalty: float = 0.0,
    presence_penalty: float = 0.0,
    seed: int = 0,
) -> dict:
    """调用 OpenAI 兼容聊天补全 API（非流式）。

    Args:
        api_key: API 密钥。
        api_base: API 基础地址（前缀），如 "https://api.deepseek.com/v1"。
        model: 模型名称。
        messages: 消息列表，格式 [{"role": "user", "content": "..."}]。
        timeout: 请求超时秒数。
        temperature: 采样温度 (0.0-2.0)。
        max_tokens: 最大输出 token。
        top_p: 核采样阈值 (0.0-1.0)。
        frequency_penalty: 频率惩罚 (-2.0-2.0)。
        presence_penalty: 存在惩罚 (-2.0-2.0)。
        seed: 随机种子，0 表示不指定。

    Returns:
        {"content": str, "thinking": str}

    Raises:
        ValueError: API Key 为空。
        ConnectionError: 网络连接失败。
        TimeoutError: 请求超时。
        RuntimeError: API 返回错误。
    """
    if not api_base.strip():
        raise ValueError("API 地址未配置，请在设置中检查服务商与 API Base")
    if not model.strip():
        raise ValueError("未选择模型，请在设置中配置翻译/AI 模型")
    if not messages or not any(m.get("content", "").strip() for m in messages):
        raise ValueError("消息内容为空，无法发送 API 请求")

    log_info(f"call_chat: base={api_base} model={model} msgs={len(messages)} timeout={timeout}")
    try:
        result = _native_call_chat(
            api_key=api_key,
            api_base=api_base,
            model=model,
            messages=messages,
            timeout=timeout,
            temperature=temperature,
            max_tokens=max_tokens,
            top_p=top_p,
            frequency_penalty=frequency_penalty,
            presence_penalty=presence_penalty,
            seed=seed,
        )
        log_info(f"call_chat: OK content={len(result.get('content', ''))} chars")
        return result
    except Exception as e:
        log_error(f"call_chat: FAILED {e}")
        raise


def call_chat_stream(
    api_key: str,
    api_base: str,
    model: str,
    messages: list[dict],
    timeout: int = 30,
    temperature: float = 0.1,
    max_tokens: int = 4096,
    top_p: float = 1.0,
    frequency_penalty: float = 0.0,
    presence_penalty: float = 0.0,
    seed: int = 0,
    on_thinking=None,
) -> dict:
    """流式调用 OpenAI 兼容聊天补全 API。

    参数与 call_chat() 相同，额外支持：
        on_thinking(cumulative_text: str): 每次收到思考内容时调用。

    Returns:
        {"content": str, "thinking": str}
    """
    if not api_base.strip():
        raise ValueError("API 地址未配置，请在设置中检查服务商与 API Base")
    if not model.strip():
        raise ValueError("未选择模型，请在设置中配置翻译/AI 模型")
    if not messages or not any(m.get("content", "").strip() for m in messages):
        raise ValueError("消息内容为空，无法发送 API 请求")

    log_info(f"call_chat_stream: base={api_base} model={model} msgs={len(messages)} timeout={timeout}")
    try:
        result = _native_call_chat_stream(
            api_key=api_key,
            api_base=api_base,
            model=model,
            messages=messages,
            timeout=timeout,
            temperature=temperature,
            max_tokens=max_tokens,
            top_p=top_p,
            frequency_penalty=frequency_penalty,
            presence_penalty=presence_penalty,
            seed=seed,
            on_thinking=on_thinking,
        )
        log_info(f"call_chat_stream: OK content={len(result.get('content', ''))} chars")
        return result
    except Exception as e:
        log_error(f"call_chat_stream: FAILED {e}")
        raise


def list_models(api_key: str, api_base: str, timeout: int = 10) -> list[str]:
    """获取服务商支持的模型列表。

    Returns:
        模型 ID 字符串列表。
    """
    log_info(f"list_models: base={api_base}")
    try:
        models = _native_list_models(
            api_key=api_key,
            api_base=api_base,
            timeout=timeout,
        )
        log_info(f"list_models: OK {len(models)} models")
        return models
    except Exception as e:
        log_error(f"list_models: FAILED {e}")
        raise