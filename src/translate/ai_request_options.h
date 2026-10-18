#pragma once

#include "ai_client.h"

class Settings;

/** @brief 一次 AI 翻译请求的不可变参数快照。
 * @details 在 GUI 线程构造，随后只由工作线程读取，避免与主线程并发访问
 * Settings（QString 隐式共享的并发读写为未定义行为）。参数按端点和模型隔离，
 * ApiClient 只发送显式配置的可选字段。
 */
struct AiRequestOptions {
    QString provider;
    QString apiKey;
    QString apiBase;
    QString model;
    int     timeout = 30; // 连接和发送阶段，秒；不限制模型思考总时长
    int     responseIdleTimeout = 0; // 响应静默等待，0 表示不限
    double  temperature = 0.1;
    int     maxTokens = 4096;
    double  topP = 1.0;
    double  frequencyPenalty = 0.0;
    double  presencePenalty = 0.0;
    int     seed = 0;
    QString enabledParameters;
    QJsonObject modelMetadata;
    QString extraBody;
    QString thinkingFormat = QStringLiteral("auto");
    QString maxTokensField = QStringLiteral("auto");
    QString doSample = QStringLiteral("auto");
    QString thinking = QStringLiteral("auto");
    QString reasoningEffort = QStringLiteral("auto");
    QStringList stop;
    bool    streamUsage = false;      // 流式请求附带 stream_options.include_usage
    bool    streamThinking = false;  // 流式输出 + 思考过程实时推送

    /// 从配置快照提取 AI 请求参数（仅构造线程调用）。
    static AiRequestOptions fromSettings(const Settings& s, const QString& model = {});
    ApiClient::ChatParams toChatParams(const QJsonArray& messages, std::atomic<int>* cancelFlag = nullptr) const;
    /// 在当前工作线程执行；两种传输模式使用相同的累计内容/final 回调契约。
    int execute(const QJsonArray& messages, std::atomic<int>* cancelFlag,
                ApiClient::StreamChunkCallback callback) const;
    /// 停止序列按行存储；不替上游猜测数量上限。
    static QStringList parseStop(const QString& raw);
    static QString     joinStop(const QStringList& stop);
};
