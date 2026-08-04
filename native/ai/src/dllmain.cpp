// dllmain.cpp - DLL 入口 + C ABI 函数实现
//
// 将 ApiClient 的 C++ 接口转换为 C ABI，
// 供 Python ctypes 调用。
//
// 所有字符串参数使用宽字符（wchar_t*），
// JSON 数据以字符串形式传递，C++ 侧负责解析。
//
#include <windows.h>
#include "../include/ai_client.h"
#include <snaplens_log.h>
#include "api_client.h"

#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <cstring>
#include <string>

// ============================================================================
// 辅助函数
// ============================================================================

namespace {

// 宽字符串 → QString
inline QString w2q(const wchar_t* w) {
    if (!w) return QString();
    return QString::fromWCharArray(w);
}

// QString → 宽字符串（安全拷贝到外部缓冲区）
inline void q2w(const QString& q, wchar_t* out, int size) {
    if (!out || size <= 0) return;
    std::wstring ws = q.toStdWString();
    wcsncpy_s(out, size, ws.c_str(), _TRUNCATE);
}

// 解析 JSON 消息数组
QJsonArray parseMessages(const wchar_t* messages_json) {
    if (!messages_json || !*messages_json) return QJsonArray();
    QJsonDocument doc = QJsonDocument::fromJson(
        QString::fromWCharArray(messages_json).toUtf8());
    return doc.array();
}

}  // namespace

// ============================================================================
// DLL 入口
// ============================================================================
BOOL APIENTRY DllMain(HMODULE, DWORD reason, LPVOID) {
    switch (reason) {
        case DLL_PROCESS_ATTACH:
            SNAP_LOG_DEBUG("snaplens_ai.dll PROCESS_ATTACH");
            break;
        case DLL_PROCESS_DETACH:
            SNAP_LOG_DEBUG("snaplens_ai.dll PROCESS_DETACH");
            break;
        case DLL_THREAD_ATTACH:
        case DLL_THREAD_DETACH:
            break;
    }
    return TRUE;
}

// ============================================================================
// C ABI 实现
// ============================================================================
extern "C" {

// ---------- 生命周期 ----------

SNAP_AI_API int snap_ai_init(void) {
    SNAP_LOG_DEBUG("snap_ai_init() called");
    // Qt 网络功能不需要额外初始化，QNetworkAccessManager 按需创建即可
    return 0;
}

SNAP_AI_API void snap_ai_shutdown(void) {
    SNAP_LOG_DEBUG("snap_ai_shutdown() called");
}

// ---------- 非流式聊天补全 ----------

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
    wchar_t* error_out,    int error_size) {

    // 参数校验
    if (!api_key || !*api_key) {
        q2w(QString("API Key is empty"), error_out, error_size);
        SNAP_LOG_ERROR("snap_ai_chat_create: PARAM_ERROR: empty api_key");
        return SNAP_AI_ERR_PARAM;
    }
    if (!api_base || !*api_base) {
        q2w(QString("API base URL is empty"), error_out, error_size);
        SNAP_LOG_ERROR("snap_ai_chat_create: PARAM_ERROR: empty api_base");
        return SNAP_AI_ERR_PARAM;
    }

    SNAP_LOG_INFO("snap_ai_chat_create: base=%ls model=%ls timeout=%d",
                api_base, model, timeout_secs);

    ApiClient client;
    QJsonArray messages = parseMessages(messages_json);

    auto result = client.chatCreate(
        w2q(api_key), w2q(api_base), w2q(model),
        messages, timeout_secs, temperature, max_tokens,
        top_p, frequency_penalty, presence_penalty, seed);

    if (result.error_code != 0) {
        SNAP_LOG_ERROR("snap_ai_chat_create: FAIL code=%d msg=%s",
                    result.error_code, result.error_msg.toUtf8().constData());
        q2w(result.error_msg, error_out, error_size);
        return result.error_code;
    }

    q2w(result.content, content_out, content_size);
    q2w(result.thinking, thinking_out, thinking_size);
    SNAP_LOG_INFO("snap_ai_chat_create: OK");
    return SNAP_AI_OK;
}

// ---------- 流式聊天补全 ----------

// 流式回调的上下文
struct StreamCallbackCtx {
    SnapAiStreamCallback c_callback;
    void* user_data;
};

static void streamCallbackWrapper(
    const QString& content,
    const QString& thinking,
    bool is_final,
    int error_code,
    const QString& error_msg,
    StreamCallbackCtx* ctx) {

    if (!ctx || !ctx->c_callback) return;

    std::wstring content_ws = content.toStdWString();
    std::wstring thinking_ws = thinking.toStdWString();
    std::wstring error_ws = error_msg.toStdWString();

    ctx->c_callback(
        content_ws.c_str(),
        thinking_ws.c_str(),
        is_final ? 1 : 0,
        error_code,
        error_ws.c_str(),
        ctx->user_data);
}

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
    int* cancel_flag) {

    if (!on_chunk) {
        SNAP_LOG_ERROR("snap_ai_chat_create_stream: PARAM_ERROR: null callback");
        return SNAP_AI_ERR_PARAM;
    }
    if (!api_key || !*api_key || !api_base || !*api_base) {
        SNAP_LOG_ERROR("snap_ai_chat_create_stream: PARAM_ERROR: empty api_key/base");
        return SNAP_AI_ERR_PARAM;
    }

    SNAP_LOG_INFO("snap_ai_chat_create_stream: base=%ls model=%ls",
                api_base, model);

    StreamCallbackCtx ctx{on_chunk, user_data};

    ApiClient client;
    QJsonArray messages = parseMessages(messages_json);

    int ret = client.chatCreateStream(
        w2q(api_key), w2q(api_base), w2q(model),
        messages, timeout_secs, temperature, max_tokens,
        top_p, frequency_penalty, presence_penalty, seed,
        [&ctx](const QString& content, const QString& thinking,
               bool is_final, int error_code, const QString& error_msg) {
            streamCallbackWrapper(content, thinking, is_final,
                                  error_code, error_msg, &ctx);
        },
        cancel_flag);

    SNAP_LOG_INFO("snap_ai_chat_create_stream: return=%d", ret);
    return ret;
}

// ---------- 模型列表 ----------

SNAP_AI_API int snap_ai_list_models(
    const wchar_t* api_key,
    const wchar_t* api_base,
    int timeout_secs,
    wchar_t* models_out, int models_size,
    wchar_t* error_out,   int error_size) {

    if (!api_key || !*api_key || !api_base || !*api_base) {
        q2w(QString("API Key or base URL is empty"), error_out, error_size);
        return SNAP_AI_ERR_PARAM;
    }

    SNAP_LOG_INFO("snap_ai_list_models: base=%ls", api_base);

    ApiClient client;
    auto result = client.listModels(w2q(api_key), w2q(api_base), timeout_secs);

    if (result.error_code != 0) {
        SNAP_LOG_ERROR("snap_ai_list_models: FAIL code=%d msg=%s",
                    result.error_code, result.error_msg.toUtf8().constData());
        q2w(result.error_msg, error_out, error_size);
        return result.error_code;
    }

    // 将模型 ID 列表序列化为 JSON 数组字符串
    QJsonArray arr;
    for (const QString& id : result.model_ids) {
        arr.append(id);
    }
    QString json_str = QString::fromUtf8(
        QJsonDocument(arr).toJson(QJsonDocument::Compact));
    q2w(json_str, models_out, models_size);

    SNAP_LOG_INFO("snap_ai_list_models: OK, %d models", result.model_ids.size());
    return SNAP_AI_OK;
}

}  // extern "C"