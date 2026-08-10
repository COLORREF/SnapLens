#ifndef SNAPLENS_TRANSLATE_AI_CLIENT_H
#define SNAPLENS_TRANSLATE_AI_CLIENT_H

#include <QObject>
#include <QJsonArray>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QString>
#include <QStringList>
#include <functional>
#include <atomic>

// ============================================================================
// ApiClient — OpenAI-compatible API 客户端（基于 Qt Network）
// ============================================================================

class ApiClient : public QObject
{
    Q_OBJECT

public:
    // 流式回调：content累积文本, thinking累积文本, isFinal, errorCode, errorMsg
    using StreamChunkCallback = std::function<void(
        const QString& content, const QString& thinking,
        bool isFinal, int errorCode, const QString& errorMsg)>;

    struct ChatResult {
        int errorCode = 0;
        QString errorMsg;
        QString content;
        QString thinking;
    };

    struct ModelsResult {
        int errorCode = 0;
        QString errorMsg;
        QStringList modelIds;
    };

    explicit ApiClient(QObject* parent = nullptr);
    ~ApiClient() override;

    // 非流式聊天补全
    // cancelFlag：可选取消标志（原子），非空时每 100ms 轮询一次；
    // 置位则 abort 底层请求并断开网络（用于窗口关闭/替换时立即取消，省 token）。
    ChatResult chatCreate(const QString& apiKey, const QString& apiBase,
                          const QString& model, const QJsonArray& messages,
                          int timeoutSecs = 30, double temperature = 0.1,
                          int maxTokens = 4096, double topP = 1.0,
                          double frequencyPenalty = 0.0, double presencePenalty = 0.0,
                          int seed = 0, std::atomic<int>* cancelFlag = nullptr);

    // 流式聊天补全（返回 errorCode，0=成功）
    int chatCreateStream(const QString& apiKey, const QString& apiBase,
                         const QString& model, const QJsonArray& messages,
                         int timeoutSecs, double temperature, int maxTokens,
                         double topP, double frequencyPenalty, double presencePenalty,
                         int seed, StreamChunkCallback callback,
                         std::atomic<int>* cancelFlag = nullptr);

    // 获取模型列表
    ModelsResult listModels(const QString& apiKey, const QString& apiBase,
                            int timeoutSecs = 10);

    // 异步获取模型列表（L11：不阻塞 UI）。
    // 立即返回，结果通过 callback 在主线程（本对象所属线程）回调。
    // 线程安全语义：
    //  - 回调连接以 this 为 context —— 本对象析构时连接自动断开，
    //    reply 作为 m_nam 子对象随之销毁，回调绝不在对象销毁后触发（无 use-after-free）；
    //  - reply 在成功/超时/失败全部分支 deleteLater（无泄漏）；
    //  - 超时由 setTransferTimeout 触发（OperationCanceledError → errorCode -2）；
    //  - 调用方（对话框）析构 = 取消请求，无需手动 abort。
    void listModelsAsync(const QString& apiKey, const QString& apiBase,
                         int timeoutSecs,
                         std::function<void(const ModelsResult&)> callback);

private:
    QJsonObject buildRequestBody(const QString& model, const QJsonArray& messages,
                                 int maxTokens, double temperature, double topP,
                                 double frequencyPenalty, double presencePenalty,
                                 int seed, bool stream);
    void setupRequest(QNetworkRequest& req, const QString& apiKey, int timeoutSecs);
    QString parseErrorBody(const QByteArray& body);
    int runEventLoop(QNetworkReply* reply, int timeoutSecs, std::atomic<int>* cancelFlag = nullptr);

    QNetworkAccessManager* m_nam;
};

#endif // SNAPLENS_TRANSLATE_AI_CLIENT_H