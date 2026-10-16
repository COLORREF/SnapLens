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

/**
 * @brief OpenAI 兼容网络客户端，负责请求、SSE 解析、超时和取消。
 * @details 必须在创建对象的线程调用。同步接口使用局部事件循环；UI 获取模型列表
 * 应使用 listModelsAsync。返回码：0 成功，-1 请求/响应失败，-2 超时，-3 取消。
 */
class ApiClient : public QObject
{
    Q_OBJECT

public:
    /// 回调接收累计正文、累计思考文本、结束标志及错误信息。
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

    /** @brief 一次聊天补全请求的全部输入。
     * @details provider 决定哪些采样/思考字段会被真正发送：各服务商对
     * OpenAI 兼容字段的支持差异很大，不支持的字段一律不发送（见 providers.h）。
     */
    struct ChatParams {
        QString apiKey;
        QString apiBase;
        QString model;
        QString provider;              // 服务商 ID，用于能力裁剪
        QJsonArray messages;
        int timeoutSecs = 30;
        double temperature = 0.1;
        int maxTokens = 4096;
        double topP = 1.0;
        double frequencyPenalty = 0.0;
        double presencePenalty = 0.0;
        int seed = 0;                  // 0 = 不指定
        QString doSample;              // auto/on/off（仅 GLM 等支持）
        QString thinking;              // auto/on/off
        QString reasoningEffort;       // auto = 不发送
        QStringList stop;              // 停止序列
        bool streamUsage = true;       // stream_options.include_usage
        std::atomic<int>* cancelFlag = nullptr;
    };

    // 非流式聊天补全
    // cancelFlag：可选取消标志（原子），非空时每 100ms 轮询一次；
    // 置位则 abort 底层请求并断开网络（用于窗口关闭/替换时立即取消，省 token）。
    ChatResult chatCreate(const ChatParams& params);

    /** @brief 同步消费 SSE 响应；每次内容更新回调，结束时恰好一次 final 回调。
     * @return 与 final 回调相同的错误码；调用方不应据此重复发送错误通知。
     */
    int chatCreateStream(const ChatParams& params, StreamChunkCallback callback);

    // 获取模型列表
    ModelsResult listModels(const QString& apiKey, const QString& apiBase,
                            int timeoutSecs = 10);

    /** @brief 异步获取模型列表，在本对象所属线程调用 callback。
     * @details 对象销毁会断开回调并释放请求；callback 捕获的其他对象须由调用方保证有效。
     */
    void listModelsAsync(const QString& apiKey, const QString& apiBase,
                         int timeoutSecs,
                         std::function<void(const ModelsResult&)> callback);

private:
    /// 按 provider 能力构建请求体；stream=true 时附加 stream_options。
    QJsonObject buildRequestBody(const ChatParams& params, bool stream);
    void setupRequest(QNetworkRequest& req, const QString& apiKey, int timeoutSecs);
    QString parseErrorBody(const QByteArray& body);
    int runEventLoop(QNetworkReply* reply, int timeoutSecs, std::atomic<int>* cancelFlag = nullptr);

    QNetworkAccessManager* m_nam;
};

#endif // SNAPLENS_TRANSLATE_AI_CLIENT_H
