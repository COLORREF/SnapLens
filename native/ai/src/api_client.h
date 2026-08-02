// api_client.h - OpenAI 兼容 API 客户端（内部实现）
//
// 基于 QNetworkAccessManager 封装，支持：
// - 非流式 POST /chat/completions
// - 流式 POST /chat/completions（SSE 解析）
// - GET /models
//
// 线程安全：每个 ApiClient 实例需要在调用线程创建 QEventLoop，
// 支持从非 Qt 主线程调用（如 Python QThread）。
//
#ifndef SNAP_AI_CLIENT_IMPL_H
#define SNAP_AI_CLIENT_IMPL_H

#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <QEventLoop>
#include <QTimer>
#include <QString>
#include <functional>
#include <string>

// ---- 日志工具（调试用，后续清理）----
#include <cstdio>
#include <cstdarg>

namespace snap_ai_log {

inline void log_impl(const char* file, int line, const char* fmt, ...) {
    const char* slash = file;
    if (const char* f = strrchr(file, '\\')) slash = f + 1;
    if (const char* f = strrchr(file, '/'))  slash = f + 1;

    fprintf(stderr, "[snap_ai %s:%d] ", slash, line);
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
}

}  // namespace snap_ai_log

#define SNAP_AI_LOG(...) ::snap_ai_log::log_impl(__FILE__, __LINE__, __VA_ARGS__)

// ---- 流式回调类型 ----
using StreamChunkCallback = std::function<void(
    const QString& content,    // 本 chunk 的正文内容增量
    const QString& thinking,   // 本 chunk 的思考内容增量
    bool is_final,             // 是否为最后一个 chunk
    int error_code,            // 0=成功，非 0=错误码
    const QString& error_msg   // 错误消息
)>;

// ---- API 客户端 ----
class ApiClient : public QObject {
    Q_OBJECT
public:
    explicit ApiClient(QObject* parent = nullptr);
    ~ApiClient() override;

    // 非流式聊天补全：同步阻塞，返回 (content, thinking, error_code, error_msg)
    struct ChatResult {
        QString content;
        QString thinking;
        int error_code = 0;
        QString error_msg;
    };
    ChatResult chatCreate(const QString& api_key,
                          const QString& api_base,
                          const QString& model,
                          const QJsonArray& messages,
                          int timeout_secs,
                          double temperature,
                          int max_tokens,
                          double top_p,
                          double frequency_penalty,
                          double presence_penalty,
                          int seed);

    // 流式聊天补全：同步阻塞直到完成，通过 callback 推送每个 chunk
    // cancel_flag: 指向外部中断标志，非 0 时中断请求
    int chatCreateStream(const QString& api_key,
                         const QString& api_base,
                         const QString& model,
                         const QJsonArray& messages,
                         int timeout_secs,
                         double temperature,
                         int max_tokens,
                         double top_p,
                         double frequency_penalty,
                         double presence_penalty,
                         int seed,
                         StreamChunkCallback callback,
                         volatile int* cancel_flag = nullptr);

    // 获取模型列表：同步阻塞
    struct ModelsResult {
        QStringList model_ids;
        int error_code = 0;
        QString error_msg;
    };
    ModelsResult listModels(const QString& api_key,
                            const QString& api_base,
                            int timeout_secs);

private:
    // 构建请求体 JSON
    QJsonObject buildRequestBody(const QString& model,
                                 const QJsonArray& messages,
                                 int max_tokens,
                                 double temperature,
                                 double top_p,
                                 double frequency_penalty,
                                 double presence_penalty,
                                 int seed,
                                 bool stream);

    // 设置公共请求头
    void setupRequest(QNetworkRequest& req,
                      const QString& api_key,
                      int timeout_secs);

    // 解析 HTTP 错误响应
    static QString parseErrorBody(const QByteArray& body);

    // 运行局部事件循环，支持超时和取消
    int runEventLoop(QNetworkReply* reply,
                     int timeout_secs,
                     volatile int* cancel_flag = nullptr,
                     QTimer* cancel_timer = nullptr);

    QNetworkAccessManager* nam_;
};

#endif  // SNAP_AI_CLIENT_IMPL_H