"""共享 API 客户端。

基于 openai SDK 封装，兼容 DeepSeek / OpenAI / 通义千问及任何
实现 OpenAI 兼容接口的服务商。统一处理鉴权、额度、网络、超时等错误。

注意：openai SDK 内部会将 base_url 作为前缀，自动拼接 /chat/completions 等端点路径。
因此 api_base 应设为 "https://api.deepseek.com/v1" 而非完整 endpoint URL。
"""
from openai import OpenAI, AuthenticationError, RateLimitError, \
    APITimeoutError, APIConnectionError, APIError
from PySide6.QtCore import QThread


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
    """调用 OpenAI 兼容聊天补全 API。

    Args:
        api_key: API 密钥。
        api_base: API 基础地址（前缀），如 "https://api.deepseek.com/v1"。
                  openai SDK 会自动在此之后拼接 /chat/completions。
        model: 模型名称，如 "deepseek-v4-flash"。
        messages: 消息列表，格式 [{"role": "user", "content": "..."}]。
        timeout: 请求超时秒数。
        temperature: 采样温度 (0.0-2.0)，越低输出越确定。
        max_tokens: 最大输出 token。
        top_p: 核采样阈值 (0.0-1.0)。
        frequency_penalty: 频率惩罚 (-2.0-2.0)，正值降低重复。
        presence_penalty: 存在惩罚 (-2.0-2.0)，正值鼓励新话题。
        seed: 随机种子，0 表示不指定（每次不同）。

    Returns:
        {"content": str, "thinking": str}
        - content: 模型回复文本（已 strip）
        - thinking: 推理/思考��程（仅 DeepSeek reasoning_content，其他为空字符串）

    Raises:
        ValueError: API Key 为空。
        ConnectionError: 网络连接失败。
        TimeoutError: 请求超时。
        RuntimeError: API 返回错误���鉴权失败 / 额度不足 / 服务端错误）。
    """
    if not api_key:
        raise ValueError("API Key 未设置")

    # 直接使用 api_base 作为 SDK 的 base_url
    # SDK 内部会在此前缀后自动拼接 /chat/completions 等路径
    client = OpenAI(
        api_key=api_key,
        base_url=api_base.rstrip("/"),
        timeout=float(timeout),
        max_retries=0,
    )

    # 构建请求参数，seed=0 时不传入（表示随机）
    extra: dict = {}
    if seed != 0:
        extra["seed"] = seed

    try:
        response = client.chat.completions.create(
            model=model,
            messages=messages,
            max_tokens=max_tokens,
            temperature=temperature,
            top_p=top_p,
            frequency_penalty=frequency_penalty,
            presence_penalty=presence_penalty,
            **extra,
        )
    except AuthenticationError as e:
        raise RuntimeError(f"API 鉴权失败：{e}")
    except RateLimitError as e:
        raise RuntimeError(f"API 额度不足或频率限制：{e}")
    except APITimeoutError:
        raise TimeoutError(f"请求超时（{timeout}秒）")
    except APIConnectionError as e:
        raise ConnectionError(f"网络连接失败：{e}")
    except APIError as e:
        raise RuntimeError(f"API 错误：{e}")

    # 解析响应
    try:
        msg = response.choices[0].message
        content = (msg.content or "").strip()
        # reasoning_content 是 DeepSeek 扩展字段，通过 model_extra 或属性获取
        thinking = (
            getattr(msg, "reasoning_content", "")
            or ""
        )
        return {"content": content, "thinking": thinking}
    except (IndexError, AttributeError) as e:
        raise RuntimeError(f"API 返回格式异常，无法解析结果：{e}")


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
    on_thinking = None,
) -> dict:
    """流式调用 OpenAI 兼容聊天补全 API。

    参数与 call_chat() 相同，额外支持：
        on_thinking(cumulative_text: str): 每次收到思考内容 chunk 时调用，
                                          传入当前已累计的完整思考文本。

    通过 stream=True 逐 token 接收，on_thinking 在每个有 reasoning_content
    的 chunk 上被调用，实现"打字机效果"实时显示。

    Returns:
        {"content": str, "thinking": str}
    """
    if not api_key:
        raise ValueError("API Key 未设置")

    client = OpenAI(
        api_key=api_key,
        base_url=api_base.rstrip("/"),
        timeout=float(timeout),
        max_retries=0,
    )

    extra: dict = {}
    if seed != 0:
        extra["seed"] = seed

    try:
        stream = client.chat.completions.create(
            model=model,
            messages=messages,
            max_tokens=max_tokens,
            temperature=temperature,
            top_p=top_p,
            frequency_penalty=frequency_penalty,
            presence_penalty=presence_penalty,
            stream=True,
            **extra,
        )
    except AuthenticationError as e:
        raise RuntimeError(f"API 鉴权失败：{e}")
    except RateLimitError as e:
        raise RuntimeError(f"API 额度不足或频率限制：{e}")
    except APITimeoutError:
        raise TimeoutError(f"请求超时（{timeout}秒）")
    except APIConnectionError as e:
        raise ConnectionError(f"网络连接失败：{e}")
    except APIError as e:
        raise RuntimeError(f"API 错误：{e}")

    # 逐 chunk 累加
    thinking_parts: list[str] = []
    content_parts: list[str] = []

    try:
        for chunk in stream:
            # 支持外部中断（如用户关闭翻译窗口时调用 QThread.requestInterruption）
            _thread = QThread.currentThread()
            if _thread is not None and _thread.isInterruptionRequested():
                stream.close()
                break
            delta = (
                chunk.choices[0].delta
                if chunk.choices else None
            )
            if delta is None:
                continue

            # 思考内容（DeepSeek 扩展字段）
            rc = getattr(delta, "reasoning_content", None)
            if rc:
                thinking_parts.append(rc)
                if on_thinking is not None:
                    on_thinking("".join(thinking_parts))

            # 正文内容
            c = delta.content or ""
            if c:
                content_parts.append(c)

    except (APIError, APITimeoutError, APIConnectionError) as e:
        # 流中断：丢弃不完整结果，抛出异常
        raise RuntimeError(f"流式响应中断：{e}")

    return {
        "content": "".join(content_parts).strip(),
        "thinking": "".join(thinking_parts),
    }


def list_models(api_key: str, api_base: str, timeout: int = 10) -> list[str]:
    """获取服务商支持的模型列表。

    调用 OpenAI 兼容的 GET /models 端点，返回模型 ID 列表。
    失败时（网络错误、鉴权失败等）抛出异常，由调用方处理。

    Args:
        api_key: API 密钥。
        api_base: API 基础地址。
        timeout: 请求超时（秒），默认 10 秒。

    Returns:
        模型 ID 字符串列表，如 ["deepseek-v4-flash", "deepseek-v4-pro"]。

    Raises:
        RuntimeError: API 调用失败（网络、鉴权等）。
    """
    client = OpenAI(
        api_key=api_key,
        base_url=api_base.rstrip("/"),
        timeout=float(timeout),
        max_retries=0,
    )
    try:
        models = client.models.list()
    except AuthenticationError as e:
        raise RuntimeError(f"API 鉴权失败：{e}")
    except APIConnectionError as e:
        raise RuntimeError(f"网络连接失败：{e}")
    except APITimeoutError:
        raise RuntimeError(f"请求超时（{timeout}秒）")
    except APIError as e:
        raise RuntimeError(f"API 错误：{e}")

    return [m.id for m in models]
