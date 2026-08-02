// ai_client.h - SnapLens AI 通信层 C ABI 公共接口
//
// 该头文件定义了 snaplens_ai.dll 导出的所有 C 函数，
// 供 Python 通过 ctypes 调用，也可供未来 C++ 重构版本直接链接。
//
// 底层使用 Qt6 Network（QNetworkAccessManager），
// 支持 OpenAI 兼容 API 的非流式和流式（SSE）聊天补全。
//
// 所有函数返回值约定：
//   0 表示成功，< 0 表示失败（错误码见下方 SNAP_AI_ERR_*）
//   指针参数：out 形式输出，调用方负责分配内存
//
#ifndef SNAP_AI_CLIENT_H
#define SNAP_AI_CLIENT_H

#ifdef SNAP_AI_EXPORTS
    #define SNAP_AI_API __declspec(dllexport)
#else
    #define SNAP_AI_API __declspec(dllimport)
#endif

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// 错误码
// ============================================================================
#define SNAP_AI_OK              0
#define SNAP_AI_ERR_NETWORK    -1   // 网络连接失败
#define SNAP_AI_ERR_HTTP       -2   // HTTP 非 2xx 状态码
#define SNAP_AI_ERR_JSON       -3   // JSON 解析失败
#define SNAP_AI_ERR_API        -4   // API 返回业务错误
#define SNAP_AI_ERR_CANCELLED  -5   // 被取消标志中断
#define SNAP_AI_ERR_PARAM      -6   // 参数无效

// ============================================================================
// 生命周期
// ============================================================================

SNAP_AI_API int snap_ai_init(void);
SNAP_AI_API void snap_ai_shutdown(void);

// ============================================================================
// 非流式聊天补全
// ============================================================================

// 调用 OpenAI 兼容的 /chat/completions 端点，同步阻塞直到完成。
//
// 参数：
//   api_key       - API 密钥（宽字符串）
//   api_base      - API 基础 URL，如 "https://api.deepseek.com/v1"
//   model         - 模型名称
//   messages_json - 消息列表 JSON，如 "[{\"role\":\"user\",\"content\":\"你好\"}]"
//   timeout_secs  - 超时秒数
//   temperature   - 采样温度
//   max_tokens    - 最大输出 token
//   top_p         - 核采样阈值
//   frequency_penalty - 频率惩罚
//   presence_penalty  - 存在惩罚
//   seed          - 随机种子（0 表示不指定）
//   content_out   - 输出：回复正文缓冲区
//   content_size  - 输出缓冲区大小（字符数）
//   thinking_out  - 输出：思考内容缓冲区（DeepSeek reasoning_content）
//   thinking_size - 输出缓冲区大小（字符数）
//   error_out     - 输出：错误信息缓冲区（失败时填充）
//   error_size    - 输出缓冲区大小（字符数）
//
// 返回：SNAP_AI_OK 或错误码
//
SNAP_AI_API int snap_ai_chat_create(
    const wchar_t* api_key,
    const wchar_t* api_base,
    const wchar_t* model,
    const wchar_t* messages_json,
    int timeout_secs,
    double temperature,
    int max_tokens,
    double top_p,
    double frequency_penalty,
    double presence_penalty,
    int seed,
    wchar_t* content_out,  int content_size,
    wchar_t* thinking_out, int thinking_size,
    wchar_t* error_out,    int error_size
);

// ============================================================================
// 流式聊天补全（SSE）
// ============================================================================

// 流式回调类型。
// 每次收到 SSE chunk 时调用，从网络线程直接回调。
// content  / thinking 可能为空字符串（本 chunk 无该类型数据）。
// is_final==1 表示流已结束（收到 [DONE] 或出错）。
// 如果 is_final==1 且 error_code!=0，本次调用失败。
typedef void (*SnapAiStreamCallback)(
    const wchar_t* content,
    const wchar_t* thinking,
    int is_final,
    int error_code,
    const wchar_t* error_message,
    void* user_data
);

// 流式调用 OpenAI 兼容的 /chat/completions 端点。
// 函数立即返回，实际数据通过 on_chunk 回调异步推送。
// 内部使用 QEventLoop 阻塞当前线程，直到流结束或出错。
//
// cancel_flag 指向一个 int，C++ 侧在每次处理 chunk 前检查其值。
// 若 Python 侧将其设为 1，正在进行的请求会被中断。
//
// 参数与 snap_ai_chat_create 相同，额外：
//   on_chunk    - 流式数据回调
//   user_data   - 透传给回调的上下文指针
//   cancel_flag - 中断标志指针（可选，传 NULL 表示不支持取消）
//
// 返回：SNAP_AI_OK 表示成功启动，流结束后通过回调的 is_final 通知最终状态
//
SNAP_AI_API int snap_ai_chat_create_stream(
    const wchar_t* api_key,
    const wchar_t* api_base,
    const wchar_t* model,
    const wchar_t* messages_json,
    int timeout_secs,
    double temperature,
    int max_tokens,
    double top_p,
    double frequency_penalty,
    double presence_penalty,
    int seed,
    SnapAiStreamCallback on_chunk,
    void* user_data,
    int* cancel_flag
);

// ============================================================================
// 模型列表
// ============================================================================

// 获取服务商支持的模型列表（GET /models）。
//
// 参数：
//   api_key      - API 密钥
//   api_base     - API 基础 URL
//   timeout_secs - 超时秒数
//   models_out   - 输出：模型 ID 列表（JSON 数组字符串）
//   models_size  - 输出缓冲区大小
//   error_out    - 输出：错误信息
//   error_size   - 输出缓冲区大小
//
// 返回：SNAP_AI_OK 或错误码
//
SNAP_AI_API int snap_ai_list_models(
    const wchar_t* api_key,
    const wchar_t* api_base,
    int timeout_secs,
    wchar_t* models_out, int models_size,
    wchar_t* error_out,   int error_size
);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // SNAP_AI_CLIENT_H