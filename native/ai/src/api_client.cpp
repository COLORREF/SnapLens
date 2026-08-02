// api_client.cpp - OpenAI 兼容 API 客户端实现
//
// 流程：
// 1. 构建 JSON 请求体（QJsonDocument）
// 2. 通过 QNetworkAccessManager 发送 POST/GET
// 3. 非流式：等待 finished 信号，解析响应 JSON
// 4. 流式：连接 readyRead 信号，SSE 逐行解析，回调推送
// 5. 使用局部 QEventLoop 阻塞调用线程，支持超时/取消
//
#include "api_client.h"

#include <QNetworkRequest>
#include <QUrl>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

// ============================================================================
// ApiClient
// ============================================================================

ApiClient::ApiClient(QObject* parent)
    : QObject(parent)
    , nam_(new QNetworkAccessManager(this)) {
    SNAP_AI_LOG("ApiClient created");
}

ApiClient::~ApiClient() {
    SNAP_AI_LOG("ApiClient destroyed");
}

QJsonObject ApiClient::buildRequestBody(
    const QString& model,
    const QJsonArray& messages,
    int max_tokens,
    double temperature,
    double top_p,
    double frequency_penalty,
    double presence_penalty,
    int seed,
    bool stream) {

    QJsonObject body;
    body[QLatin1StringView("model")] = model;
    body[QLatin1StringView("messages")] = messages;
    body[QLatin1StringView("max_tokens")] = max_tokens;
    body[QLatin1StringView("temperature")] = temperature;
    body[QLatin1StringView("top_p")] = top_p;
    body[QLatin1StringView("frequency_penalty")] = frequency_penalty;
    body[QLatin1StringView("presence_penalty")] = presence_penalty;
    if (seed != 0) {
        body[QLatin1StringView("seed")] = seed;
    }
    body[QLatin1StringView("stream")] = stream;
    return body;
}

void ApiClient::setupRequest(QNetworkRequest& req,
                              const QString& api_key,
                              int timeout_secs) {
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    req.setRawHeader("Authorization",
                     QString("Bearer %1").arg(api_key).toUtf8());
    if (timeout_secs > 0) {
        req.setTransferTimeout(timeout_secs * 1000);
    }
}

QString ApiClient::parseErrorBody(const QByteArray& body) {
    if (body.isEmpty()) return QString();
    QJsonDocument doc = QJsonDocument::fromJson(body);
    if (doc.isObject()) {
        QJsonObject obj = doc.object();
        if (obj.contains(QLatin1StringView("error"))) {
            QJsonObject err = obj[QLatin1StringView("error")].toObject();
            return err[QLatin1StringView("message")].toString();
        }
    }
    return QString::fromUtf8(body.left(500));
}

int ApiClient::runEventLoop(QNetworkReply* reply,
                             int timeout_secs,
                             volatile int* cancel_flag,
                             QTimer* /*cancel_timer*/) {
    QEventLoop loop;
    QTimer timer;

    // 超时定时器
    timer.setSingleShot(true);
    QObject::connect(&timer, &QTimer::timeout, &loop, [&]() {
        SNAP_AI_LOG("Request timeout after %d seconds, aborting", timeout_secs);
        reply->abort();
        loop.quit();
    });
    timer.start(timeout_secs * 1000);

    // 取消轮询定时器（每 100ms 检查 cancel_flag）
    QTimer cancel_poll;
    if (cancel_flag) {
        QObject::connect(&cancel_poll, &QTimer::timeout, &loop, [&]() {
            if (*cancel_flag) {
                SNAP_AI_LOG("Cancel flag set, aborting request");
                reply->abort();
                loop.quit();
            }
        });
        cancel_poll.start(100);
    }

    // finished 信号退出事件循环
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);

    loop.exec();

    timer.stop();
    if (cancel_flag) {
        cancel_poll.stop();
    }

    // 检查取消
    if (cancel_flag && *cancel_flag) {
        return -5;  // SNAP_AI_ERR_CANCELLED
    }

    // 检查超时（timer 已触发但 reply 仍在运行）
    if (!timer.isActive() && reply->isRunning()) {
        return -1;  // SNAP_AI_ERR_NETWORK
    }

    return 0;
}

// ============================================================================
// 非流式聊天补全
// ============================================================================

ApiClient::ChatResult ApiClient::chatCreate(
    const QString& api_key,
    const QString& api_base,
    const QString& model,
    const QJsonArray& messages,
    int timeout_secs,
    double temperature,
    int max_tokens,
    double top_p,
    double frequency_penalty,
    double presence_penalty,
    int seed) {

    SNAP_AI_LOG("chatCreate: base=%s model=%s timeout=%d",
                api_base.toUtf8().constData(),
                model.toUtf8().constData(),
                timeout_secs);

    ChatResult result;

    QJsonObject body = buildRequestBody(model, messages, max_tokens,
                                         temperature, top_p,
                                         frequency_penalty, presence_penalty,
                                         seed, false);

    QUrl url(api_base + "/chat/completions");
    QNetworkRequest req(url);
    setupRequest(req, api_key, timeout_secs);

    QByteArray json_data = QJsonDocument(body).toJson(QJsonDocument::Compact);
    SNAP_AI_LOG("chatCreate: POST %s body=%d bytes",
                url.toString().toUtf8().constData(), json_data.size());

    QNetworkReply* reply = nam_->post(req, json_data);
    int loop_ret = runEventLoop(reply, timeout_secs);

    if (loop_ret != 0) {
        SNAP_AI_LOG("chatCreate: loop error %d", loop_ret);
        result.error_code = loop_ret;
        result.error_msg = (loop_ret == -5) ? QString("Request cancelled")
                                            : QString("Request timeout");
        reply->deleteLater();
        return result;
    }

    // 检查网络错误
    if (reply->error() != QNetworkReply::NoError) {
        SNAP_AI_LOG("chatCreate: network error %d: %s",
                    reply->error(), reply->errorString().toUtf8().constData());
        result.error_code = -1;
        result.error_msg = reply->errorString();
        reply->deleteLater();
        return result;
    }

    // 检查 HTTP 状态码
    int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    QByteArray resp_body = reply->readAll();
    SNAP_AI_LOG("chatCreate: HTTP %d, body=%d bytes", status, resp_body.size());

    if (status < 200 || status >= 300) {
        QString err_msg = parseErrorBody(resp_body);
        result.error_code = -2;
        result.error_msg = err_msg.isEmpty()
            ? QString("HTTP %1").arg(status)
            : QString("HTTP %1: %2").arg(status).arg(err_msg);
        SNAP_AI_LOG("chatCreate: HTTP error: %s", result.error_msg.toUtf8().constData());
        reply->deleteLater();
        return result;
    }

    // 解析响应
    QJsonDocument doc = QJsonDocument::fromJson(resp_body);
    if (!doc.isObject()) {
        result.error_code = -3;
        result.error_msg = "Invalid JSON response";
        reply->deleteLater();
        return result;
    }

    QJsonObject obj = doc.object();
    QJsonArray choices = obj[QLatin1StringView("choices")].toArray();
    if (choices.isEmpty()) {
        result.error_code = -3;
        result.error_msg = "No choices in response";
        reply->deleteLater();
        return result;
    }

    QJsonObject message = choices[0][QLatin1StringView("message")].toObject();
    result.content = message[QLatin1StringView("content")].toString().trimmed();
    // DeepSeek 扩展字段：reasoning_content
    if (message.contains(QLatin1StringView("reasoning_content"))) {
        result.thinking = message[QLatin1StringView("reasoning_content")].toString();
    }

    SNAP_AI_LOG("chatCreate: OK, content=%d chars, thinking=%d chars",
                result.content.size(), result.thinking.size());

    reply->deleteLater();
    return result;
}

// ============================================================================
// 流式聊天补全
// ============================================================================

int ApiClient::chatCreateStream(
    const QString& api_key,
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
    volatile int* cancel_flag) {

    SNAP_AI_LOG("chatCreateStream: base=%s model=%s",
                api_base.toUtf8().constData(),
                model.toUtf8().constData());

    QJsonObject body = buildRequestBody(model, messages, max_tokens,
                                         temperature, top_p,
                                         frequency_penalty, presence_penalty,
                                         seed, true);

    QUrl url(api_base + "/chat/completions");
    QNetworkRequest req(url);
    setupRequest(req, api_key, timeout_secs);
    req.setRawHeader("Accept", "text/event-stream");

    QByteArray json_data = QJsonDocument(body).toJson(QJsonDocument::Compact);
    SNAP_AI_LOG("chatCreateStream: POST %s (stream)", url.toString().toUtf8().constData());

    QNetworkReply* reply = nam_->post(req, json_data);

    // SSE 解析状态
    QByteArray sse_buffer;
    bool final_callback_sent = false;
    int chunk_count = 0;

    // readyRead：每收到一块数据就解析 SSE 行
    QObject::connect(reply, &QNetworkReply::readyRead, [&]() {
        if (cancel_flag && *cancel_flag) return;
        if (final_callback_sent) return;

        sse_buffer.append(reply->readAll());

        // 逐行解析
        while (true) {
            int nl = sse_buffer.indexOf('\n');
            if (nl < 0) break;  // 没有完整行，等待更多数据

            QByteArray line = sse_buffer.left(nl).trimmed();
            sse_buffer.remove(0, nl + 1);

            if (line.isEmpty()) continue;

            if (!line.startsWith("data: ")) continue;

            QByteArray json_str = line.mid(6);

            if (json_str == "[DONE]") {
                SNAP_AI_LOG("chatCreateStream: [DONE] after %d chunks", chunk_count);
                callback(QString(), QString(), true, 0, QString());
                final_callback_sent = true;
                return;
            }

            QJsonDocument doc = QJsonDocument::fromJson(json_str);
            if (!doc.isObject()) {
                SNAP_AI_LOG("chatCreateStream: invalid SSE JSON: %s",
                            json_str.left(100).constData());
                continue;
            }

            QJsonObject obj = doc.object();
            QJsonArray choices = obj[QLatin1StringView("choices")].toArray();
            if (choices.isEmpty()) continue;

            QJsonObject delta = choices[0][QLatin1StringView("delta")].toObject();
            QString content = delta[QLatin1StringView("content")].toString();
            QString thinking;

            if (delta.contains(QLatin1StringView("reasoning_content"))) {
                thinking = delta[QLatin1StringView("reasoning_content")].toString();
            }

            if (!content.isEmpty() || !thinking.isEmpty()) {
                chunk_count++;
                callback(content, thinking, false, 0, QString());
            }
        }
    });

    // 运行事件循环
    int loop_ret = runEventLoop(reply, timeout_secs, cancel_flag);

    // 处理提前退出
    if (loop_ret != 0 && !final_callback_sent) {
        QString err_msg = (loop_ret == -5)
            ? QString("Request cancelled")
            : QString("Request timeout after %1 seconds").arg(timeout_secs);
        callback(QString(), QString(), true, loop_ret, err_msg);
        final_callback_sent = true;
        reply->deleteLater();
        return loop_ret;
    }

    // 检查网络错误
    if (reply->error() != QNetworkReply::NoError && !final_callback_sent) {
        QString err = reply->errorString();
        SNAP_AI_LOG("chatCreateStream: network error: %s", err.toUtf8().constData());
        callback(QString(), QString(), true, -1, err);
        final_callback_sent = true;
        reply->deleteLater();
        return -1;
    }

    // 检查 HTTP 状态码（如果流没有正常结束）
    if (!final_callback_sent) {
        int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status < 200 || status >= 300) {
            QByteArray err_body = reply->readAll();
            QString err_msg = parseErrorBody(err_body);
            if (err_msg.isEmpty()) err_msg = QString("HTTP %1").arg(status);
            SNAP_AI_LOG("chatCreateStream: HTTP error: %s", err_msg.toUtf8().constData());
            callback(QString(), QString(), true, -2, err_msg);
            final_callback_sent = true;
        } else {
            // 流正常结束但没有 [DONE]（兼容某些 API）
            SNAP_AI_LOG("chatCreateStream: stream ended without [DONE], %d chunks", chunk_count);
            callback(QString(), QString(), true, 0, QString());
            final_callback_sent = true;
        }
    }

    if (!sse_buffer.isEmpty()) {
        SNAP_AI_LOG("chatCreateStream: residual buffer %d bytes after stream end",
                    sse_buffer.size());
    }

    SNAP_AI_LOG("chatCreateStream: done, %d chunks processed", chunk_count);

    reply->deleteLater();
    return 0;
}

// ============================================================================
// 模型列表
// ============================================================================

ApiClient::ModelsResult ApiClient::listModels(
    const QString& api_key,
    const QString& api_base,
    int timeout_secs) {

    SNAP_AI_LOG("listModels: base=%s", api_base.toUtf8().constData());

    ModelsResult result;

    QUrl url(api_base + "/models");
    QNetworkRequest req(url);
    setupRequest(req, api_key, timeout_secs);

    QNetworkReply* reply = nam_->get(req);
    int loop_ret = runEventLoop(reply, timeout_secs);

    if (loop_ret != 0) {
        result.error_code = loop_ret;
        result.error_msg = (loop_ret == -5) ? QString("Request cancelled")
                                            : QString("Request timeout");
        reply->deleteLater();
        return result;
    }

    if (reply->error() != QNetworkReply::NoError) {
        SNAP_AI_LOG("listModels: network error: %s",
                    reply->errorString().toUtf8().constData());
        result.error_code = -1;
        result.error_msg = reply->errorString();
        reply->deleteLater();
        return result;
    }

    int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    QByteArray resp_body = reply->readAll();
    SNAP_AI_LOG("listModels: HTTP %d, body=%d bytes", status, resp_body.size());

    if (status < 200 || status >= 300) {
        QString err_msg = parseErrorBody(resp_body);
        result.error_code = -2;
        result.error_msg = err_msg.isEmpty()
            ? QString("HTTP %1").arg(status)
            : QString("HTTP %1: %2").arg(status).arg(err_msg);
        reply->deleteLater();
        return result;
    }

    QJsonDocument doc = QJsonDocument::fromJson(resp_body);
    if (!doc.isObject()) {
        result.error_code = -3;
        result.error_msg = "Invalid JSON response";
        reply->deleteLater();
        return result;
    }

    QJsonArray data = doc.object()[QLatin1StringView("data")].toArray();
    for (const QJsonValue& v : data) {
        QString id = v[QLatin1StringView("id")].toString();
        if (!id.isEmpty()) {
            result.model_ids.append(id);
        }
    }

    SNAP_AI_LOG("listModels: OK, %d models found", result.model_ids.size());

    reply->deleteLater();
    return result;
}