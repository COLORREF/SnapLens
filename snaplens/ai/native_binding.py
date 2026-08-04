"""C++ AI 通信库的 Python 绑定层。

通过 ctypes 加载 snaplens_ai.dll，提供与 core/api_client.py 相同的
函数签名，上层代码无感知切换。

设计要点：
- 流式回调通过 CFUNCTYPE 传入 C++ 侧，C++ 侧在 QEventLoop 中回调
- ctypes 回调自动获取 GIL，可安全访问 Python 对象
- cancel_flag 通过 ctypes.c_int 指针传递，Python 侧设值即可中断
"""
import ctypes
import json
import os
import sys
from pathlib import Path

from ..log import log_info, log_warning, log_error

# 错误码（与 ai_client.h 中 SNAP_AI_* 一致）
SNAP_AI_OK = 0
SNAP_AI_ERR_NETWORK = -1
SNAP_AI_ERR_HTTP = -2
SNAP_AI_ERR_JSON = -3
SNAP_AI_ERR_API = -4
SNAP_AI_ERR_CANCELLED = -5
SNAP_AI_ERR_PARAM = -6

# ----------------------------------------------------------------- 回调类型
# 流式回调：void(content, thinking, is_final, error_code, error_msg, user_data)
StreamCallbackC = ctypes.CFUNCTYPE(
    None,                              # void 返回
    ctypes.c_wchar_p,                  # content
    ctypes.c_wchar_p,                  # thinking
    ctypes.c_int,                      # is_final (1=结束)
    ctypes.c_int,                      # error_code
    ctypes.c_wchar_p,                  # error_message
    ctypes.c_void_p,                   # user_data
)

# ----------------------------------------------------------------- DLL 加载
_dll_cache: ctypes.CDLL | None = None
_qt_bin_added = False


def _find_qt_bin_dir() -> Path | None:
    """查找 Qt6 的 DLL 所在目录（snaplens_ai.dll 依赖 Qt6Core/Network DLL）。"""
    # 1. 环境变量
    for var in ("QT6_DIR", "QTDIR"):
        val = os.environ.get(var, "")
        if val:
            p = Path(val) / "bin"
            if p.is_dir():
                return p

    # 2. 常见安装路径（D: 盘优先，适配当前开发环境）
    import glob as _glob
    for base in (Path("D:/ProgramFiles/Qt"), Path("C:/Qt")):
        if base.is_dir():
            versions = sorted(base.glob("6.*"), key=lambda p: p.name, reverse=True)
            for v in versions:
                for msvc in (v / "msvc2022_64", v / "msvc2019_64", v / "mingw_64"):
                    d = msvc / "bin"
                    if d.is_dir():
                        return d
    return None


def _ensure_qt_dll_dir() -> None:
    """确保 Qt DLL 目录在搜索路径中（仅执行一次）。"""
    global _qt_bin_added
    if _qt_bin_added:
        return
    qt_bin = _find_qt_bin_dir()
    if qt_bin:
        os.add_dll_directory(str(qt_bin))
        log_info(f"[snap_ai PY] Qt DLL 目录: {qt_bin}")
    _qt_bin_added = True


def _load_dll() -> ctypes.CDLL:
    """加载 snaplens_ai.dll，失败抛 OSError。

    查找顺序：
    1. native/bin/              （CMake 构建输出目录）
    2. native/build/*/bin/       （CLion cmake-build-* 目录）
    3. 可执行文件同级             （PyInstaller 打包）
    """
    _ensure_qt_dll_dir()

    # __file__ = snaplens/ai/native_binding.py → 项目根目录
    project_root = Path(__file__).parent.parent.parent
    candidates = [
        project_root / "native" / "bin" / "snaplens_ai.dll",
    ]
    # CLion 构建目录
    for build_dir in sorted(project_root.glob("native/cmake-build-*/bin"), reverse=True):
        candidates.append(build_dir / "snaplens_ai.dll")
    if getattr(sys, "frozen", False):
        candidates.append(Path(sys.executable).parent / "snaplens_ai.dll")

    for path in candidates:
        if path.is_file():
            try:
                log_info(f"[snap_ai PY] 加载 DLL: {path}")
                dll = ctypes.CDLL(str(path))
                _setup_signatures(dll)
                log_info(f"[snap_ai PY] DLL 加载成功，路径: {path}")
                return dll
            except OSError as e:
                log_error(f"[snap_ai PY] 加载失败 {path}: {e}")
                raise OSError(f"加载 {path} 失败：{e}") from e

    searched = "\n  ".join(str(p) for p in candidates)
    raise OSError(f"未找到 snaplens_ai.dll，已搜索：\n  {searched}")


def _setup_signatures(dll: ctypes.CDLL) -> None:
    """设置所有 C ABI 函数的参数和返回类型。"""

    # 生命周期
    dll.snap_ai_init.restype = ctypes.c_int
    dll.snap_ai_init.argtypes = []
    dll.snap_ai_shutdown.restype = None
    dll.snap_ai_shutdown.argtypes = []

    # 非流式聊天补全
    dll.snap_ai_chat_create.restype = ctypes.c_int
    dll.snap_ai_chat_create.argtypes = [
        ctypes.c_wchar_p,          # api_key
        ctypes.c_wchar_p,          # api_base
        ctypes.c_wchar_p,          # model
        ctypes.c_wchar_p,          # messages_json
        ctypes.c_int,              # timeout_secs
        ctypes.c_double,           # temperature
        ctypes.c_int,              # max_tokens
        ctypes.c_double,           # top_p
        ctypes.c_double,           # frequency_penalty
        ctypes.c_double,           # presence_penalty
        ctypes.c_int,              # seed
        ctypes.c_wchar_p,          # content_out
        ctypes.c_int,              # content_size
        ctypes.c_wchar_p,          # thinking_out
        ctypes.c_int,              # thinking_size
        ctypes.c_wchar_p,          # error_out
        ctypes.c_int,              # error_size
    ]

    # 流式聊天补全
    dll.snap_ai_chat_create_stream.restype = ctypes.c_int
    dll.snap_ai_chat_create_stream.argtypes = [
        ctypes.c_wchar_p,          # api_key
        ctypes.c_wchar_p,          # api_base
        ctypes.c_wchar_p,          # model
        ctypes.c_wchar_p,          # messages_json
        ctypes.c_int,              # timeout_secs
        ctypes.c_double,           # temperature
        ctypes.c_int,              # max_tokens
        ctypes.c_double,           # top_p
        ctypes.c_double,           # frequency_penalty
        ctypes.c_double,           # presence_penalty
        ctypes.c_int,              # seed
        StreamCallbackC,           # on_chunk
        ctypes.c_void_p,           # user_data
        ctypes.POINTER(ctypes.c_int),  # cancel_flag
    ]

    # 模型列表
    dll.snap_ai_list_models.restype = ctypes.c_int
    dll.snap_ai_list_models.argtypes = [
        ctypes.c_wchar_p,          # api_key
        ctypes.c_wchar_p,          # api_base
        ctypes.c_int,              # timeout_secs
        ctypes.c_wchar_p,          # models_out
        ctypes.c_int,              # models_size
        ctypes.c_wchar_p,          # error_out
        ctypes.c_int,              # error_size
    ]


def _get_dll() -> ctypes.CDLL:
    """获取已加载并配置签名的 DLL 单例。"""
    global _dll_cache
    if _dll_cache is None:
        _dll_cache = _load_dll()
    return _dll_cache


# =================================================================
# Python 封装函数（与 core/api_client.py 接口一致）
# =================================================================

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
        api_base: API 基础地址。
        model: 模型名称。
        messages: 消息列表。
        timeout: 超时秒数。
        temperature: 采样温度。
        max_tokens: 最大输出 token。
        top_p: 核采样阈值。
        frequency_penalty: 频率惩罚。
        presence_penalty: 存在惩罚。
        seed: 随机种子（0 表示不指定）。

    Returns:
        {"content": str, "thinking": str}

    Raises:
        ValueError: API Key 为空。
        ConnectionError: 网络连接失败。
        TimeoutError: 请求超时。
        RuntimeError: API 返回错误。
    """
    if not api_key:
        raise ValueError("API Key 未设置")

    messages_json = json.dumps(messages, ensure_ascii=False)
    msg_count = len(messages)
    msg_bytes = len(messages_json.encode("utf-8"))

    # 预分配输出缓冲区
    content_buf = ctypes.create_unicode_buffer(32768)
    thinking_buf = ctypes.create_unicode_buffer(32768)
    error_buf = ctypes.create_unicode_buffer(4096)

    log_info(f"[snap_ai PY] call_chat: base={api_base} model={model} "
             f"msgs={msg_count} json_bytes={msg_bytes} timeout={timeout}")

    ret = _get_dll().snap_ai_chat_create(
        api_key,
        api_base.rstrip("/"),
        model,
        messages_json,
        timeout,
        temperature,
        max_tokens,
        top_p,
        frequency_penalty,
        presence_penalty,
        seed,
        content_buf, 32768,
        thinking_buf, 32768,
        error_buf, 4096,
    )

    if ret == SNAP_AI_OK:
        content = content_buf.value
        thinking = thinking_buf.value
        log_info(f"[snap_ai PY] call_chat: OK ret={ret} "
                 f"content_len={len(content)} thinking_len={len(thinking)}")
        return {
            "content": content,
            "thinking": thinking,
        }

    # 错误映射
    error_msg = error_buf.value or "Unknown error"
    log_error(f"[snap_ai PY] call_chat: FAIL ret={ret} error={error_msg}")
    _raise_from_error(ret, error_msg)


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
    if not api_key:
        raise ValueError("API Key 未设置")

    messages_json = json.dumps(messages, ensure_ascii=False)
    msg_count = len(messages)
    msg_bytes = len(messages_json.encode("utf-8"))

    # 累积缓冲区
    content_parts: list[str] = []
    thinking_parts: list[str] = []
    final_error: tuple[int, str] | None = None
    chunk_count = 0

    # 取消标志：Python 侧可通过 cancel_flag.value 设为 1 来中断
    cancel_flag = ctypes.c_int(0)

    # 流式回调（CFUNCTYPE 实例，必须保持引用防止 GC）
    def _on_chunk(content_ptr, thinking_ptr, is_final, error_code, error_msg_ptr, _user_data):
        nonlocal final_error, chunk_count
        content = content_ptr or ""
        thinking = thinking_ptr or ""

        if content or thinking:
            chunk_count += 1
        if content:
            content_parts.append(content)
        if thinking:
            thinking_parts.append(thinking)
            if on_thinking is not None:
                on_thinking("".join(thinking_parts))

        if is_final:
            if error_code != 0:
                final_error = (error_code, error_msg_ptr or "Unknown error")

    # 保持回调引用，防止被 GC 导致崩溃
    cb_ref = StreamCallbackC(_on_chunk)

    log_info(f"[snap_ai PY] call_chat_stream: base={api_base} model={model} "
             f"msgs={msg_count} json_bytes={msg_bytes} timeout={timeout}")

    ret = _get_dll().snap_ai_chat_create_stream(
        api_key,
        api_base.rstrip("/"),
        model,
        messages_json,
        timeout,
        temperature,
        max_tokens,
        top_p,
        frequency_penalty,
        presence_penalty,
        seed,
        cb_ref,
        None,  # user_data
        ctypes.byref(cancel_flag),
    )

    if final_error is not None:
        code, msg = final_error
        log_error(f"[snap_ai PY] call_chat_stream: FAIL ret={ret} "
                  f"chunks={chunk_count} error_code={code} error={msg}")
        _raise_from_error(code, msg)

    content = "".join(content_parts).strip()
    thinking = "".join(thinking_parts)
    log_info(f"[snap_ai PY] call_chat_stream: OK ret={ret} "
             f"chunks={chunk_count} content_len={len(content)} thinking_len={len(thinking)}")
    return {
        "content": content,
        "thinking": thinking,
    }


def list_models(api_key: str, api_base: str, timeout: int = 10) -> list[str]:
    """获取服务商支持的模型列表。

    Returns:
        模型 ID 字符串列表。

    Raises:
        RuntimeError: API 调用失败。
    """
    models_buf = ctypes.create_unicode_buffer(65536)
    error_buf = ctypes.create_unicode_buffer(4096)

    log_info(f"[snap_ai PY] list_models: base={api_base} timeout={timeout}")

    ret = _get_dll().snap_ai_list_models(
        api_key,
        api_base.rstrip("/"),
        timeout,
        models_buf, 65536,
        error_buf, 4096,
    )

    if ret == SNAP_AI_OK:
        models_json = models_buf.value
        try:
            models = json.loads(models_json)
            log_info(f"[snap_ai PY] list_models: OK ret={ret} count={len(models)}")
            return models
        except json.JSONDecodeError:
            log_warning(f"[snap_ai PY] list_models: OK but JSON decode failed, "
                        f"raw_len={len(models_json)}")
            return []

    error_msg = error_buf.value or "Unknown error"
    log_error(f"[snap_ai PY] list_models: FAIL ret={ret} error={error_msg}")
    _raise_from_error(ret, error_msg)


def _raise_from_error(code: int, msg: str):
    """将 C++ 错误码转换为 Python 异常。"""
    error_names = {
        SNAP_AI_ERR_NETWORK: "ERR_NETWORK",
        SNAP_AI_ERR_HTTP: "ERR_HTTP",
        SNAP_AI_ERR_JSON: "ERR_JSON",
        SNAP_AI_ERR_API: "ERR_API",
        SNAP_AI_ERR_CANCELLED: "ERR_CANCELLED",
        SNAP_AI_ERR_PARAM: "ERR_PARAM",
    }
    err_name = error_names.get(code, f"UNKNOWN({code})")
    log_warning(f"[snap_ai PY] _raise_from_error: code={code} ({err_name}) msg={msg}")

    if code == SNAP_AI_ERR_NETWORK:
        raise ConnectionError(msg)
    elif code == SNAP_AI_ERR_HTTP:
        raise RuntimeError(msg)  # HTTP 错误（401/429/500 等）
    elif code == SNAP_AI_ERR_JSON:
        raise RuntimeError(f"API 返回格式异常：{msg}")
    elif code == SNAP_AI_ERR_API:
        raise RuntimeError(f"API 错误：{msg}")
    elif code == SNAP_AI_ERR_CANCELLED:
        raise RuntimeError("请求已取消")
    elif code == SNAP_AI_ERR_PARAM:
        raise ValueError(msg)
    else:
        raise RuntimeError(f"未知错误 ({code}): {msg}")