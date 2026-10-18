#include "ai_request_options.h"
#include "providers.h"
#include "model_capabilities.h"
#include "../settings.h"
#include <QJsonDocument>
#include <QRegularExpression>
#include <utility>

AiRequestOptions AiRequestOptions::fromSettings(const Settings& s, const QString& model)
{
    const QString selectedModel = model.trimmed().isEmpty() ? s.ai_model : model.trimmed();
    const auto profiles = QJsonDocument::fromJson(s.ai_model_profiles.toUtf8()).object();
    // 旧配置首次使用保留显式思考等选项；有模型配置后，未知模型回到自动。
    const Settings profile(profiles.isEmpty() && selectedModel == s.ai_model ? s.toDict()
        : profiles.value(aiModelProfileKey(s.ai_api_base, selectedModel)).toObject());
    AiRequestOptions o;
    o.provider          = s.ai_provider;
    o.apiKey            = s.ai_api_key;
    o.apiBase           = s.ai_api_base;
    o.model             = selectedModel;
    o.modelMetadata = ModelCapabilities::cachedModel(
        QJsonDocument::fromJson(s.ai_model_metadata.toUtf8()).object(), s.ai_api_base, selectedModel);
    o.timeout           = s.ai_timeout;
    o.responseIdleTimeout = s.ai_response_idle_timeout;
    o.temperature       = profile.ai_temperature;
    o.maxTokens         = profile.ai_max_tokens;
    o.topP              = profile.ai_top_p;
    o.frequencyPenalty  = profile.ai_frequency_penalty;
    o.presencePenalty   = profile.ai_presence_penalty;
    o.enabledParameters = profile.ai_enabled_parameters;
    o.extraBody = profile.ai_extra_body;
    o.thinkingFormat = profile.ai_thinking_format;
    o.maxTokensField = profile.ai_max_tokens_field;
    o.seed              = profile.ai_seed;
    o.doSample          = profile.ai_do_sample;
    o.thinking          = profile.ai_thinking;
    o.reasoningEffort   = profile.ai_reasoning_effort;
    o.stop              = AiRequestOptions::parseStop(profile.ai_stop);
    o.streamUsage       = profile.ai_stream_usage;
    o.streamThinking    = s.ai_stream_thinking;
    return o;
}

QStringList AiRequestOptions::parseStop(const QString& raw)
{
    QStringList result;
    const QStringList lines = raw.split(QRegularExpression(QStringLiteral("[\\r\\n]+")), Qt::SkipEmptyParts);
    for (const QString& line : lines) {
        const QString value = line.trimmed();
        if (!value.isEmpty()) result.append(value);
    }
    return result;
}

QString AiRequestOptions::joinStop(const QStringList& stop)
{
    return stop.join(QLatin1Char('\n'));
}

ApiClient::ChatParams AiRequestOptions::toChatParams(const QJsonArray& messages, std::atomic<int>* cancelFlag) const
{
    ApiClient::ChatParams params;
    params.apiKey        = apiKey;
    params.apiBase       = apiBase;
    params.model         = model;
    params.provider      = provider;
    params.messages      = messages;
    params.timeoutSecs       = timeout;
    params.responseIdleTimeoutSecs = responseIdleTimeout;
    params.temperature       = temperature;
    params.maxTokens     = maxTokens;
    params.topP          = topP;
    params.frequencyPenalty  = frequencyPenalty;
    params.presencePenalty   = presencePenalty;
    params.enabledParameters = enabledParameters;
    params.extraBody = extraBody;
    params.thinkingFormat = thinkingFormat;
    params.maxTokensField = maxTokensField;
    params.seed          = seed;
    params.doSample      = doSample;
    params.thinking      = thinking;
    params.reasoningEffort   = reasoningEffort;
    params.modelMetadata = modelMetadata;
    params.stop          = stop;
    params.streamUsage       = streamUsage;
    params.cancelFlag    = cancelFlag;

    return params;
}

int AiRequestOptions::execute(const QJsonArray& messages, std::atomic<int>* cancelFlag,
                              ApiClient::StreamChunkCallback callback) const
{
    if (cancelFlag && cancelFlag->load(std::memory_order_relaxed)) {
        if (callback) callback({}, {}, true, -3, QStringLiteral("用户取消"));
        return -3;
    }
    ApiClient client;
    const auto params = toChatParams(messages, cancelFlag);
    if (streamThinking) return client.chatCreateStream(params, std::move(callback));
    const auto result = client.chatCreate(params);
    if (callback) callback(result.content, result.thinking, true, result.errorCode, result.errorMsg);
    return result.errorCode;
}
