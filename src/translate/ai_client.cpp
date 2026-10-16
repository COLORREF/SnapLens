#include "ai_client.h"
#include "providers.h"
#include "log/log_manager.h"

#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>

// ============================================================================
// ApiClient 实现
// ============================================================================

ApiClient::ApiClient(QObject* parent)
    : QObject(parent)
    , m_nam(new QNetworkAccessManager(this))
{
}

ApiClient::~ApiClient() = default;

// ---------------------------------------------------------------------------
// 构建请求体
// ---------------------------------------------------------------------------

QJsonObject ApiClient::buildRequestBody(const QString& model,
                                        const QJsonArray& messages,
                                        int maxTokens,
                                        double temperature,
                                        double topP,
                                        double frequencyPenalty,
                                        double presencePenalty,
                                        int seed,
                                        bool stream)
{
    QJsonObject body;
    body["model"] = model;
    body["messages"] = messages;
    body["max_tokens"] = maxTokens;
    body["temperature"] = temperature;
    body["top_p"] = topP;
    body["frequency_penalty"] = frequencyPenalty;
    body["presence_penalty"] = presencePenalty;
    body["stream"] = stream;
    if (seed != 0) {
        body["seed"] = seed;
    }
    return body;
}

// ---------------------------------------------------------------------------
// 设置请求头
// ---------------------------------------------------------------------------

void ApiClient::setupRequest(QNetworkRequest& req, const QString& apiKey, int timeoutSecs)
{
    if (!apiKey.trimmed().isEmpty())
        req.setRawHeader("Authorization", ("Bearer " + apiKey.trimmed()).toUtf8());
    req.setRawHeader("Content-Type", "application/json");
    req.setRawHeader("Accept", "application/json");
    req.setTransferTimeout(timeoutSecs * 1000);
}

// ---------------------------------------------------------------------------
// 解析错误响应体
// ---------------------------------------------------------------------------

QString ApiClient::parseErrorBody(const QByteArray& body)
{
    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(body, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        return QString::fromUtf8(body).left(500);
    }
    QJsonObject obj = doc.object();
    if (obj.contains("error")) {
        QJsonObject errObj = obj["error"].toObject();
        if (errObj.contains("message")) {
            return errObj["message"].toString();
        }
        return QJsonDocument(errObj).toJson(QJsonDocument::Compact);
    }
    return QString::fromUtf8(body).left(500);
}

// ---------------------------------------------------------------------------
// 工具：运行事件循环直至回复完成
// ---------------------------------------------------------------------------

int ApiClient::runEventLoop(QNetworkReply* reply, int timeoutSecs, std::atomic<int>* cancelFlag)
{
    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);

    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);

    // 取消标志检查定时器
    QTimer cancelTimer;
    if (cancelFlag) {
        cancelTimer.setInterval(100);
        QObject::connect(&cancelTimer, &QTimer::timeout, [&]() {
            if (cancelFlag->load()) {
                SNAP_LOG_INFO("ApiClient::runEventLoop: 检测到取消请求，立即 abort 并断开网络连接");
                reply->abort();
                loop.quit();
            }
        });
        cancelTimer.start();
    }

    if (cancelFlag && cancelFlag->load(std::memory_order_relaxed)) {
        reply->abort();
        return -3;
    }
    timer.start(timeoutSecs * 1000);
    if (!reply->isFinished()) loop.exec();

    timer.stop();
    if (cancelFlag) cancelTimer.stop();

    if (!reply->isFinished()) {
        reply->abort();
        return -2; // 超时
    }
    return 0;
}

// ============================================================================
// 非流式聊天补全
// ============================================================================

ApiClient::ChatResult ApiClient::chatCreate(const QString& apiKey,
                                            const QString& apiBase,
                                            const QString& model,
                                            const QJsonArray& messages,
                                            int timeoutSecs,
                                            double temperature,
                                            int maxTokens,
                                            double topP,
                                            double frequencyPenalty,
                                            double presencePenalty,
                                            int seed,
                                            std::atomic<int>* cancelFlag)
{
    ChatResult result;
    result.errorCode = 0;

    // 在发起网络请求前验证配置和消息。
    if (apiBase.trimmed().isEmpty()) {
        result.errorCode = -1;
        result.errorMsg = "API 地址未配置，请在设置中检查服务商与 API Base";
        return result;
    }
    if (model.trimmed().isEmpty()) {
        result.errorCode = -1;
        result.errorMsg = "未选择模型，请在设置中配置翻译/AI 模型";
        return result;
    }
    bool hasContent = false;
    for (const auto& m : messages) {
        if (!m.toObject().value("content").toString().trimmed().isEmpty()) {
            hasContent = true;
            break;
        }
    }
    if (messages.isEmpty() || !hasContent) {
        result.errorCode = -1;
        result.errorMsg = "消息内容为空，无法发送 API 请求";
        return result;
    }

    QUrl url = providerEndpoint(apiBase, QStringLiteral("chat/completions"));
    if (url.isEmpty()) {
        result.errorCode = -1;
        result.errorMsg = "API 地址无效，请填写 http:// 或 https:// 开头的接口根地址";
        return result;
    }
    QNetworkRequest req(url);
    setupRequest(req, apiKey, timeoutSecs);

    QJsonObject body = buildRequestBody(model, messages, maxTokens, temperature,
                                        topP, frequencyPenalty, presencePenalty,
                                        seed, false);
    QByteArray bodyData = QJsonDocument(body).toJson(QJsonDocument::Compact);

    SNAP_LOG_DEBUG(QString("ApiClient::chatCreate: url=%1 model=%2 msgs=%3")
                       .arg(url.toString(), model).arg(messages.size()));

    QNetworkReply* reply = m_nam->post(req, bodyData);
    runEventLoop(reply, timeoutSecs, cancelFlag);

    if (reply->error() == QNetworkReply::OperationCanceledError) {
        // 区分用户取消与超时（窗口关闭/替换时 requestCancel 置位触发 abort）
        if (cancelFlag && cancelFlag->load()) {
            result.errorCode = -3;
            result.errorMsg = "用户取消";
        } else {
            result.errorCode = -2;
            result.errorMsg = "请求超时";
        }
        reply->deleteLater();
        return result;
    }

    if (reply->error() != QNetworkReply::NoError) {
        result.errorCode = -1;
        QByteArray respBody = reply->readAll();
        int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        QString errDetail = parseErrorBody(respBody);

        if (httpStatus == 401 || httpStatus == 403) {
            result.errorMsg = "API Key 鉴权失败，请确认 Key 是否正确";
        } else if (httpStatus == 429) {
            result.errorMsg = "API 额度不足或受到频率限制";
        } else {
            result.errorMsg = QString("API 请求失败 (%1): %2")
                                  .arg(httpStatus).arg(errDetail);
        }
        SNAP_LOG_ERROR(QString("ApiClient::chatCreate: FAILED %1").arg(result.errorMsg));
        reply->deleteLater();
        return result;
    }

    QByteArray respData = reply->readAll();
    reply->deleteLater();

    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(respData, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        result.errorCode = -1;
        result.errorMsg = "解析 API 响应失败";
        return result;
    }

    QJsonObject resp = doc.object();
    QJsonArray choices = resp["choices"].toArray();
    if (choices.isEmpty()) {
        result.errorCode = -1;
        result.errorMsg = "API 未返回有效内容";
        return result;
    }

    QJsonObject choice = choices[0].toObject();
    QJsonObject message = choice["message"].toObject();
    result.content = message["content"].toString();

    // 提取思考内容（如果模型支持 reasoning_content）
    if (message.contains("reasoning_content")) {
        result.thinking = message["reasoning_content"].toString();
    }

    SNAP_LOG_DEBUG(QString("ApiClient::chatCreate: OK content=%1 chars").arg(result.content.size()));
    return result;
}

// ============================================================================
// 流式聊天补全
// ============================================================================

int ApiClient::chatCreateStream(const QString& apiKey,
                                const QString& apiBase,
                                const QString& model,
                                const QJsonArray& messages,
                                int timeoutSecs,
                                double temperature,
                                int maxTokens,
                                double topP,
                                double frequencyPenalty,
                                double presencePenalty,
                                int seed,
                                StreamChunkCallback callback,
                                std::atomic<int>* cancelFlag)
{
    // 在发起网络请求前验证配置和消息。
    auto failWith = [&callback](const QString& msg) {
        if (callback) callback(QString(), QString(), true, -1, msg);
    };
    if (apiBase.trimmed().isEmpty()) {
        failWith("API 地址未配置，请在设置中检查服务商与 API Base");
        return -1;
    }
    if (model.trimmed().isEmpty()) {
        failWith("未选择模型，请在设置中配置翻译/AI 模型");
        return -1;
    }
    bool hasContent = false;
    for (const auto& m : messages) {
        if (!m.toObject().value("content").toString().trimmed().isEmpty()) {
            hasContent = true;
            break;
        }
    }
    if (messages.isEmpty() || !hasContent) {
        failWith("消息内容为空，无法发送 API 请求");
        return -1;
    }

    QUrl url = providerEndpoint(apiBase, QStringLiteral("chat/completions"));
    if (url.isEmpty()) {
        failWith("API 地址无效，请填写 http:// 或 https:// 开头的接口根地址");
        return -1;
    }
    QNetworkRequest req(url);
    setupRequest(req, apiKey, timeoutSecs);

    QJsonObject body = buildRequestBody(model, messages, maxTokens, temperature,
                                        topP, frequencyPenalty, presencePenalty,
                                        seed, true);
    // 流式请求需要设置 stream_options
    QJsonObject streamOptions;
    streamOptions["include_usage"] = false;
    body["stream_options"] = streamOptions;

    QByteArray bodyData = QJsonDocument(body).toJson(QJsonDocument::Compact);

    SNAP_LOG_DEBUG(QString("ApiClient::chatCreateStream: url=%1 model=%2 msgs=%3")
                       .arg(url.toString(), model).arg(messages.size()));

    QNetworkReply* reply = m_nam->post(req, bodyData);

    // 连接绑定栈上 context；函数返回后不再访问缓冲区和回调。
    QEventLoop streamContext;
    QByteArray buffer;
    QString contentAccum;
    QString thinkingAccum;
    bool receivedChunk = false;

    auto consumeAvailable = [&](bool final) {
        // HTTP 错误正文留给统一的错误处理，不能当 SSE 丢弃。
        if (reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() >= 400) return;
        if (reply->isOpen()) buffer.append(reply->readAll());
        while (!buffer.isEmpty()) {
            const auto newline = buffer.indexOf('\n');
            if (newline < 0 && !final) break;
            const auto length = newline < 0 ? buffer.size() : newline;
            const QByteArray line = buffer.left(length).trimmed();
            buffer.remove(0, newline < 0 ? buffer.size() : newline + 1);
            if (!line.startsWith("data:")) continue;
            const QByteArray data = line.mid(5).trimmed();
            if (data == "[DONE]") continue;
            const QJsonDocument doc = QJsonDocument::fromJson(data);
            const QJsonArray choices = doc.object().value("choices").toArray();
            if (choices.isEmpty()) continue;
            const QJsonObject delta = choices.first().toObject().value("delta").toObject();
            receivedChunk = true;
            const QString content = delta.value("content").toString();
            const QString thinking = delta.value("reasoning_content").toString();
            contentAccum += content;
            thinkingAccum += thinking;
            if (callback && (!content.isEmpty() || !thinking.isEmpty()))
                callback(contentAccum, thinkingAccum, false, 0, {});
        }
    };
    QObject::connect(reply, &QNetworkReply::readyRead, &streamContext,
                     [&] { consumeAvailable(false); });
    runEventLoop(reply, timeoutSecs, cancelFlag);
    consumeAvailable(true);

    int errorCode = 0;
    QString errorMsg;

    if (reply->error() == QNetworkReply::OperationCanceledError) {
        if (cancelFlag && cancelFlag->load()) {
            errorCode = -3; // 用户取消
        } else {
            errorCode = -2; // 超时
            errorMsg = "请求超时";
        }
    } else if (reply->error() != QNetworkReply::NoError) {
        errorCode = -1;
        QByteArray errBody = reply->readAll();
        int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        QString errDetail = parseErrorBody(errBody);

        if (httpStatus == 401 || httpStatus == 403) {
            errorMsg = "API Key 鉴权失败，请确认 Key 是否正确";
        } else if (httpStatus == 429) {
            errorMsg = "API 额度不足或受到频率限制";
        } else {
            errorMsg = QString("API 请求失败 (%1): %2").arg(httpStatus).arg(errDetail);
        }
    }

    if (errorCode == 0 && !receivedChunk) {
        errorCode = -1;
        errorMsg = "API 未返回有效流式内容";
    }

    reply->deleteLater();

    // 最终回调
    if (callback) {
        callback(contentAccum, thinkingAccum, true, errorCode, errorMsg);
    }

    SNAP_LOG_DEBUG(QString("ApiClient::chatCreateStream: done content=%1 chars error=%2")
                       .arg(contentAccum.size()).arg(errorCode));
    return errorCode;
}

// ============================================================================
// 获取模型列表
// ============================================================================

ApiClient::ModelsResult ApiClient::listModels(const QString& apiKey,
                                               const QString& apiBase,
                                               int timeoutSecs)
{
    ModelsResult result;
    result.errorCode = 0;

    // 在发起网络请求前验证配置。
    if (apiBase.trimmed().isEmpty()) {
        result.errorCode = -1;
        result.errorMsg = "API 地址未配置，请在设置中检查服务商与 API Base";
        return result;
    }

    QUrl url = providerEndpoint(apiBase, QStringLiteral("models"));
    if (url.isEmpty()) {
        result.errorCode = -1;
        result.errorMsg = "API 地址无效，请填写接口根地址";
        return result;
    }
    QNetworkRequest req(url);
    setupRequest(req, apiKey, timeoutSecs);

    SNAP_LOG_DEBUG(QString("ApiClient::listModels: url=%1").arg(url.toString()));

    QNetworkReply* reply = m_nam->get(req);
    runEventLoop(reply, timeoutSecs);

    if (reply->error() == QNetworkReply::OperationCanceledError) {
        result.errorCode = -2;
        result.errorMsg = "请求超时";
        reply->deleteLater();
        return result;
    }

    if (reply->error() != QNetworkReply::NoError) {
        result.errorCode = -1;
        QByteArray body = reply->readAll();
        int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        QString errDetail = parseErrorBody(body);

        if (httpStatus == 401 || httpStatus == 403) {
            result.errorMsg = "API Key 鉴权失败，请确认 Key 是否正确";
        } else if (httpStatus == 429) {
            result.errorMsg = "API 额度不足或受到频率限制";
        } else {
            result.errorMsg = QString("获取模型列表失败 (%1): %2").arg(httpStatus).arg(errDetail);
        }
        SNAP_LOG_ERROR(QString("ApiClient::listModels: FAILED %1").arg(result.errorMsg));
        reply->deleteLater();
        return result;
    }

    QByteArray respData = reply->readAll();
    reply->deleteLater();

    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(respData, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        result.errorCode = -1;
        result.errorMsg = "解析模型列表响应失败";
        return result;
    }

    QJsonObject resp = doc.object();
    QJsonArray data = resp["data"].toArray();
    for (const QJsonValue& v : data) {
        QJsonObject m = v.toObject();
        QString id = m["id"].toString();
        if (!id.isEmpty()) {
            result.modelIds.append(id);
        }
    }

    SNAP_LOG_DEBUG(QString("ApiClient::listModels: OK %1 models").arg(result.modelIds.size()));
    return result;
}

// ============================================================================
// 异步获取模型列表
// ============================================================================

void ApiClient::listModelsAsync(const QString& apiKey, const QString& apiBase,
                                int timeoutSecs,
                                std::function<void(const ModelsResult&)> callback)
{
    ModelsResult result;
    result.errorCode = 0;

    // ---- 输入前置检查（与同步版一致）----
    if (apiBase.trimmed().isEmpty()) {
        result.errorCode = -1;
        result.errorMsg = "API 地址未配置，请在设置中检查服务商与 API Base";
        if (callback) callback(result);
        return;
    }

    QUrl url = providerEndpoint(apiBase, QStringLiteral("models"));
    if (url.isEmpty()) {
        result.errorCode = -1;
        result.errorMsg = "API 地址无效，请填写接口根地址";
        if (callback) callback(result);
        return;
    }
    QNetworkRequest req(url);
    setupRequest(req, apiKey, timeoutSecs);

    SNAP_LOG_DEBUG(QString("ApiClient::listModelsAsync: url=%1").arg(url.toString()));

    QNetworkReply* reply = m_nam->get(req);

    // 回调以 this 为 context：本对象析构时连接自动断开（reply 作为 m_nam 子对象
    // 随之销毁），回调不会在对象销毁后被触发；reply 全部分支 deleteLater 防泄漏。
    QObject::connect(reply, &QNetworkReply::finished, this,
                     [this, reply, callback]() {
        ModelsResult result;
        result.errorCode = 0;

        // 超时：setTransferTimeout 触发（与"用户取消/对象析构"区分——
        // 后两者发生时连接已断开，回调根本不会执行到这里）。
        if (reply->error() == QNetworkReply::OperationCanceledError) {
            result.errorCode = -2;
            result.errorMsg = "请求超时";
            reply->deleteLater();
            if (callback) callback(result);
            return;
        }

        if (reply->error() != QNetworkReply::NoError) {
            result.errorCode = -1;
            QByteArray body = reply->readAll();
            int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            QString errDetail = parseErrorBody(body);

            if (httpStatus == 401 || httpStatus == 403) {
                result.errorMsg = "API Key 鉴权失败，请确认 Key 是否正确";
            } else if (httpStatus == 429) {
                result.errorMsg = "API 额度不足或受到频率限制";
            } else {
                result.errorMsg = QString("获取模型列表失败 (%1): %2")
                                      .arg(httpStatus).arg(errDetail);
            }
            SNAP_LOG_ERROR(QString("ApiClient::listModelsAsync: FAILED %1").arg(result.errorMsg));
            reply->deleteLater();
            if (callback) callback(result);
            return;
        }

        QByteArray respData = reply->readAll();
        reply->deleteLater();

        QJsonParseError err;
        QJsonDocument doc = QJsonDocument::fromJson(respData, &err);
        if (err.error != QJsonParseError::NoError || !doc.isObject()) {
            result.errorCode = -1;
            result.errorMsg = "解析模型列表响应失败";
            if (callback) callback(result);
            return;
        }

        QJsonObject resp = doc.object();
        QJsonArray data = resp["data"].toArray();
        for (const QJsonValue& v : data) {
            QString id = v.toObject()["id"].toString();
            if (!id.isEmpty()) {
                result.modelIds.append(id);
            }
        }

        SNAP_LOG_DEBUG(QString("ApiClient::listModelsAsync: OK %1 models")
                           .arg(result.modelIds.size()));
        if (callback) callback(result);
    });
}
