#include "ai_client.h"
#include "providers.h"
#include "model_capabilities.h"
#include "reply_waiter.h"
#include <QLoggingCategory>

Q_LOGGING_CATEGORY(aiClientLog, "ai.client")

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>

namespace {
QString validateChat(const ApiClient::ChatParams& params)
{
    if (params.apiBase.trimmed().isEmpty())
        return QStringLiteral("API 地址未配置，请在设置中检查服务商与 API Base");
    if (params.model.trimmed().isEmpty())
        return QStringLiteral("未选择模型，请在设置中配置翻译/AI 模型");
    bool hasContent = false;
    for (const auto& message : params.messages)
        if (!message.toObject().value("content").toString().trimmed().isEmpty()) {
            hasContent = true;
            break;
        }
    if (!hasContent) return QStringLiteral("消息内容为空，无法发送 API 请求");
    if (providerEndpoint(params.apiBase, "chat/completions").isEmpty())
        return QStringLiteral("API 地址无效，请填写 http:// 或 https:// 开头的接口根地址");
    return {};
}
} // namespace

ApiClient::ApiClient(QObject* parent)
    : QObject(parent)
    , m_nam(new QNetworkAccessManager(this))
{
}

ApiClient::~ApiClient() = default;

QJsonObject ApiClient::buildRequestBody(const ChatParams& params, bool stream, QString* error)
{
    if (error) error->clear();
    auto fail = [error](const QString& message) {
        if (error) *error = message;
        return QJsonObject{};
    };
    const auto enabled = params.enabledParameters.split(',', Qt::SkipEmptyParts);
    const auto cfg = getProviderConfig(params.provider);
    const auto capabilities = ModelCapabilities::fromMetadata(params.modelMetadata);
    QJsonObject body{{"model", params.model}, {"messages", params.messages}, {"stream", stream}};
    if (enabled.contains("max_tokens")) {
        const QString field = params.maxTokensField == "auto" || params.maxTokensField.isEmpty()
            ? (capabilities.outputField.isEmpty() ? cfg.maxTokensField : capabilities.outputField)
            : params.maxTokensField;
        if (field != "max_tokens" && field != "max_completion_tokens")
            return fail("输出长度字段必须为 max_tokens 或 max_completion_tokens");
        body[field] = params.maxTokens;
    }
    if (enabled.contains("temperature")) body["temperature"] = params.temperature;
    if (enabled.contains("top_p")) body["top_p"] = params.topP;
    if (enabled.contains("frequency_penalty")) body["frequency_penalty"] = params.frequencyPenalty;
    if (enabled.contains("presence_penalty")) body["presence_penalty"] = params.presencePenalty;
    if (enabled.contains("seed")) body["seed"] = params.seed;
    if (!params.stop.isEmpty()) body["stop"] = QJsonArray::fromStringList(params.stop);
    if (params.doSample == "on" || params.doSample == "off")
        body["do_sample"] = params.doSample == "on";
    if (params.thinking == "on" || params.thinking == "off") {
        QString format = params.thinkingFormat;
        if ((format == "auto" || format.isEmpty()) && !capabilities.thinkingFormat.isEmpty())
            format = capabilities.thinkingFormat;
        if (format == "auto" || format.isEmpty()) {
            if (params.provider == "custom" || cfg.thinkingStyle == ThinkingStyle::None)
                return fail("未知的思考开关格式，请选择字段形式，或使用推理强度/自定义 JSON");
            format = cfg.thinkingStyle == ThinkingStyle::EnableThinkingFlag
                ? "enable_thinking" : "thinking";
        }
        if (format == "enable_thinking") body[format] = params.thinking == "on";
        else if (format == "thinking")
            body[format] = QJsonObject{{"type", params.thinking == "on" ? "enabled" : "disabled"}};
        else return fail("未知的思考开关格式，请使用自定义 JSON");
    }
    if (!params.reasoningEffort.trimmed().isEmpty() && params.reasoningEffort != "auto")
        body["reasoning_effort"] = params.reasoningEffort.trimmed();
    if (stream && params.streamUsage)
        body["stream_options"] = QJsonObject{{"include_usage", true}};
    if (!params.extraBody.trimmed().isEmpty()) {
        QJsonParseError parseError;
        const auto doc = QJsonDocument::fromJson(params.extraBody.toUtf8(), &parseError);
        if (parseError.error != QJsonParseError::NoError || !doc.isObject())
            return fail("自定义请求参数必须是 JSON 对象：" + parseError.errorString());
        const auto extra = doc.object();
        for (auto it = extra.begin(); it != extra.end(); ++it) {
            if (QStringList{"model", "messages", "stream"}.contains(it.key()))
                return fail("自定义参数不能覆盖 " + it.key());
            // null 是本地删除标记，避免将 null 与未指定混淆。
            if (it.value().isNull()) body.remove(it.key());
            else body[it.key()] = it.value();
        }
    }
    if (!stream) body.remove("stream_options");
    if (body.contains("max_tokens") && body.contains("max_completion_tokens"))
        return fail("不能同时发送 max_tokens 和 max_completion_tokens；请用 null 删除其中一个");
    for (const auto& field : {"max_tokens", "max_completion_tokens"}) {
        const auto value = body.value(field);
        if (!value.isUndefined() && (!value.isDouble() || value.toDouble() < 1
                || value.toDouble() != double(value.toInteger())))
            return fail(QString("%1 必须为正整数").arg(field));
    }
    return body;
}

ApiClient::ModelsResult ApiClient::parseModels(const QJsonObject& response)
{
    ModelsResult result;
    auto add = [&](const QString& id, QJsonObject model) {
        if (id.trimmed().isEmpty() || result.metadata.contains(id)) return;
        model["id"] = id;
        result.modelIds.append(id);
        result.metadata[id] = model;
    };
    if (response.value("data").isArray()) {
        for (const auto& value : response.value("data").toArray()) {
            const auto model = value.toObject();
            add(model.value("id").toString(), model);
        }
    } else if (response.value("models").isObject()) {
        // Enriched compatible gateways use a map; its key is the endpoint-facing ID.
        const auto models = response.value("models").toObject();
        for (auto it = models.begin(); it != models.end(); ++it)
            if (it.value().isObject()) add(it.key(), it.value().toObject());
    } else {
        result.errorCode = -1;
        result.errorMsg = "模型列表应包含 data 数组或 models 对象；请确认这是 OpenAI 兼容接口";
    }
    return result;
}

void ApiClient::setupRequest(QNetworkRequest& req, const QString& apiKey, int timeoutSecs)
{
    if (!apiKey.trimmed().isEmpty())
        req.setRawHeader("Authorization", ("Bearer " + apiKey.trimmed()).toUtf8());
    req.setRawHeader("Content-Type", "application/json");
    req.setRawHeader("Accept", "application/json");
    req.setTransferTimeout(qBound(0, timeoutSecs, 86400) * 1000);
}

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

ApiClient::ChatResult ApiClient::chatCreate(const ChatParams& params)
{
    const QString& apiKey = params.apiKey;
    const QString& apiBase = params.apiBase;
    const QString& model = params.model;
    const QJsonArray& messages = params.messages;
    const int timeoutSecs = params.timeoutSecs;
    std::atomic<int>* cancelFlag = params.cancelFlag;

    ChatResult result;
    result.errorCode = 0;
    if (cancelFlag && cancelFlag->load(std::memory_order_relaxed))
        return {-3, QStringLiteral("用户取消"), {}, {}};

    result.errorMsg = validateChat(params);
    if (!result.errorMsg.isEmpty()) {
        result.errorCode = -1;
        return result;
    }
    const QUrl url = providerEndpoint(apiBase, "chat/completions");
    QNetworkRequest req(url);
    setupRequest(req, apiKey, 0);

    QJsonObject body = buildRequestBody(params, false, &result.errorMsg);
    if (!result.errorMsg.isEmpty()) {
        result.errorCode = -1;
        return result;
    }
    QByteArray bodyData = QJsonDocument(body).toJson(QJsonDocument::Compact);

    qCDebug(aiClientLog).noquote() << (QString("ApiClient::chatCreate: url=%1 model=%2 msgs=%3")
                       .arg(url.toString(), model).arg(messages.size()));

    QNetworkReply* reply = m_nam->post(req, bodyData);
    const auto wait = waitForReply(reply, {timeoutSecs, params.responseIdleTimeoutSecs, 0}, cancelFlag);

    if (wait.errorCode != 0) {
        result.errorCode = wait.errorCode;
        result.errorMsg = wait.error;
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
        qCWarning(aiClientLog).noquote() << (QString("ApiClient::chatCreate: FAILED %1").arg(result.errorMsg));
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

    qCDebug(aiClientLog).noquote() << (QString("ApiClient::chatCreate: OK content=%1 chars").arg(result.content.size()));
    return result;
}

int ApiClient::chatCreateStream(const ChatParams& params, StreamChunkCallback callback)
{
    const QString& apiKey = params.apiKey;
    const QString& apiBase = params.apiBase;
    const QString& model = params.model;
    const QJsonArray& messages = params.messages;
    const int timeoutSecs = params.timeoutSecs;
    std::atomic<int>* cancelFlag = params.cancelFlag;

    // 在发起网络请求前验证配置和消息。
    if (cancelFlag && cancelFlag->load(std::memory_order_relaxed)) {
        if (callback) callback({}, {}, true, -3, QStringLiteral("用户取消"));
        return -3;
    }
    auto failWith = [&callback](const QString& msg) {
        if (callback) callback(QString(), QString(), true, -1, msg);
    };
    const auto validationError = validateChat(params);
    if (!validationError.isEmpty()) {
        failWith(validationError);
        return -1;
    }
    const QUrl url = providerEndpoint(apiBase, "chat/completions");
    QNetworkRequest req(url);
    setupRequest(req, apiKey, 0);

    // 流式与非流式请求共用参数校验，配置错误不进入网络层。
    QString configError;
    QJsonObject body = buildRequestBody(params, true, &configError);
    if (!configError.isEmpty()) {
        failWith(configError);
        return -1;
    }

    QByteArray bodyData = QJsonDocument(body).toJson(QJsonDocument::Compact);

    qCDebug(aiClientLog).noquote() << (QString("ApiClient::chatCreateStream: url=%1 model=%2 msgs=%3")
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
    const auto wait = waitForReply(reply, {timeoutSecs, params.responseIdleTimeoutSecs, 0}, cancelFlag);
    consumeAvailable(true);

    int errorCode = 0;
    QString errorMsg;

    if (wait.errorCode != 0) {
        errorCode = wait.errorCode;
        errorMsg = wait.error;
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

    qCDebug(aiClientLog).noquote() << (QString("ApiClient::chatCreateStream: done content=%1 chars error=%2")
                       .arg(contentAccum.size()).arg(errorCode));
    return errorCode;
}

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

    qCDebug(aiClientLog).noquote() << (QString("ApiClient::listModels: url=%1").arg(url.toString()));

    QNetworkReply* reply = m_nam->get(req);
    waitForReply(reply, {0, 0, qBound(1, timeoutSecs, 86400)});

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
        qCWarning(aiClientLog).noquote() << (QString("ApiClient::listModels: FAILED %1").arg(result.errorMsg));
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

    result = parseModels(doc.object());

    qCDebug(aiClientLog).noquote() << (QString("ApiClient::listModels: OK %1 models").arg(result.modelIds.size()));
    return result;
}

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

    qCDebug(aiClientLog).noquote() << (QString("ApiClient::listModelsAsync: url=%1").arg(url.toString()));

    QNetworkReply* reply = m_nam->get(req);

    // Discovery has a bounded total duration even if a server trickles bytes.
    auto* deadline = new QTimer(reply);
    deadline->setSingleShot(true);
    QObject::connect(deadline, &QTimer::timeout, reply, &QNetworkReply::abort);
    QObject::connect(reply, &QNetworkReply::finished, deadline, &QTimer::stop);
    deadline->start(qBound(1, timeoutSecs, 86400) * 1000);

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
            qCWarning(aiClientLog).noquote() << (QString("ApiClient::listModelsAsync: FAILED %1").arg(result.errorMsg));
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

        result = parseModels(doc.object());

        qCDebug(aiClientLog).noquote() << (QString("ApiClient::listModelsAsync: OK %1 models")
                           .arg(result.modelIds.size()));
        if (callback) callback(result);
    });
}
