#include "model_capabilities.h"
#include "providers.h"

#include <QJsonArray>
#include <cmath>

namespace {
qint64 capacity(std::initializer_list<QJsonValue> values)
{
    for (const auto& value : values) {
        const double number = value.toDouble(-1);
        // Reject malformed, fractional and unsafe JSON integers instead of truncating.
        if (value.isDouble() && number > 0 && number <= 9007199254740991.0
            && std::floor(number) == number)
            return static_cast<qint64>(number);
    }
    return 0;
}

std::optional<QStringList> strings(const QJsonValue& value, bool allowIds = false)
{
    if (!value.isArray()) return std::nullopt;
    QStringList result;
    for (const auto& entry : value.toArray()) {
        const QString text = (allowIds && entry.isObject()
            ? entry.toObject().value("id").toString() : entry.toString()).trimmed();
        // A malformed nonempty list is unknown, never a false "not supported".
        if (text.isEmpty()) return std::nullopt;
        if (!result.contains(text)) result.append(text);
    }
    return result;
}

std::optional<QStringList> firstList(std::initializer_list<QJsonValue> values,
                                   bool allowIds = false)
{
    for (const auto& value : values)
        if (auto parsed = strings(value, allowIds)) return parsed;
    return std::nullopt;
}
}

ModelCapabilities ModelCapabilities::fromMetadata(const QJsonObject& model)
{
    ModelCapabilities result;
    const auto limit = model.value("limit").toObject();
    const auto top = model.value("top_provider").toObject();
    const auto reasoning = model.value("reasoning").toObject();
    const auto effort = model.value("effort").toObject();
    const auto schema = model.value("parameter_schema").toObject().value("properties").toObject();
    const auto thinkingType = schema.value("thinking").toObject()
        .value("properties").toObject().value("type").toObject();
    result.contextWindow = capacity({model.value("context_length"), model.value("context_window"),
        model.value("contextWindow"), limit.value("context")});
    result.maxInputTokens = capacity({model.value("max_input_tokens"), model.value("maxInputTokens"),
        limit.value("input")});
    result.maxOutputTokens = capacity({top.value("max_completion_tokens"), model.value("max_output_tokens"),
        model.value("maxOutputTokens"), model.value("maxTokens"), limit.value("output")});
    result.parameters = strings(model.value("supported_parameters"));
    for (const auto& value : {model.value("supports_reasoning"), model.value("supportsReasoning"),
                             model.value("reasoning")}) {
        if (value.isBool()) { result.reasoning = value.toBool(); break; }
    }
    result.efforts = firstList({effort.value("supported_levels"), model.value("supported_reasoning_efforts"), model.value("reasoning_efforts"),
        model.value("reasoningLevels"), model.value("reasoningEfforts"), reasoning.value("efforts"),
        schema.value("reasoning_effort").toObject().value("enum")}, true);
    if (!result.efforts) {
        for (const auto& entry : model.value("reasoning_options").toArray()) {
            const auto option = entry.toObject();
            if (option.value("type") == "effort" && option.value("values").isArray()) {
                // models.dev uses null for the provider default, not a wire effort.
                QJsonArray values;
                for (const auto& value : option.value("values").toArray())
                    if (!value.isNull() && value != "null") values.append(value);
                result.efforts = strings(values);
                if (result.efforts) break;
            }
        }
    }
    for (const auto& value : {effort.value("default_level"), model.value("reasoningDefaultLevel"), reasoning.value("defaultEffort"),
                              schema.value("reasoning_effort").toObject().value("default")}) {
        if (value.isString() && !value.toString().trimmed().isEmpty()) {
            result.defaultEffort = value.toString().trimmed();
            break;
        }
    }
    // A boolean field has known wire semantics. A bare "thinking" declaration
    // does not: it might require a budget or adaptive mode, so do not guess it.
    const bool boolThinking = (result.parameters && result.parameters->contains("enable_thinking"))
        || schema.value("enable_thinking").toObject().value("type") == "boolean";
    if (boolThinking) {
        result.thinkingFormat = "enable_thinking";
        const auto flag = schema.value("enable_thinking").toObject();
        if (flag.value("type") == "boolean") {
            QJsonArray values{true, false};
            if (flag.contains("const")) values = QJsonArray{flag.value("const")};
            else if (flag.value("enum").isArray()) values = flag.value("enum").toArray();
            QStringList modes;
            bool valid = true;
            for (const auto& value : values) {
                if (!value.isBool()) { valid = false; break; }
                const QString mode = value.toBool() ? "on" : "off";
                if (!modes.contains(mode)) modes.append(mode);
            }
            if (valid) result.thinkingModes = modes;
        }
    } else if (const auto types = strings(thinkingType.value("enum"))) {
        QStringList modes;
        // Only infer a simple switch if no other thinking properties are required.
        const auto required = schema.value("thinking").toObject().value("required").toArray();
        bool simple = true;
        for (const auto& field : required) if (field != "type") simple = false;
        if (simple) {
            if (types->contains("enabled")) modes.append("on");
            if (types->contains("disabled")) modes.append("off");
            if (!modes.isEmpty()) {
                result.thinkingFormat = "thinking";
                result.thinkingModes = modes;
            }
        }
    }
    if (result.parameters) {
        const bool legacy = result.parameters->contains("max_tokens");
        const bool completion = result.parameters->contains("max_completion_tokens");
        if (legacy != completion) result.outputField = completion ? "max_completion_tokens" : "max_tokens";
    }
    if (result.efforts) result.effortSource = Source::Upstream;
    if (result.thinkingModes) result.thinkingSource = Source::Upstream;
    return result;
}

std::optional<ModelCapabilities> ModelCapabilities::documentedFallback(const QString& apiBase,
                                                                        const QString& model)
{
    // The model author's GLM-5 documentation defines these levels. Restrict the
    // suggestion to the first-party GLM endpoint; a proxy may use different wire
    // semantics despite exposing the same model ID.
    const auto endpoint = providerEndpoint(apiBase, "models");
    if (endpoint != QUrl("https://open.bigmodel.cn/api/paas/v4/models")) return std::nullopt;

    ModelCapabilities result;
    const QString id = model.trimmed();
    if (id == "glm-5.3" || id == "glm-5.3-flash") {
        result.reasoning = true;
        result.efforts = QStringList{"low", "high", "max"};
        result.defaultEffort = "max";
        result.thinkingModes = QStringList{"on"};
        return result;
    }
    if (id == "glm-5.2") {
        result.efforts = QStringList{"high", "max"};
        result.defaultEffort = "max";
        return result;
    }
    return std::nullopt;
}

QJsonObject ModelCapabilities::cachedModel(const QJsonObject& cache, const QString& apiBase,
                                           const QString& model)
{
    const auto endpoint = providerEndpoint(apiBase, "models");
    if (endpoint.isEmpty()) return {};
    return cache.value(endpoint.toString()).toObject().value("models").toObject()
        .value(model.trimmed()).toObject();
}

QStringList ModelCapabilities::effortChoices() const
{
    // A negative capability is useful advice, but callers retain explicit user values.
    if (efforts) return *efforts;
    if (reasoning && !*reasoning) return {};
    return {"none", "minimal", "low", "medium", "high", "xhigh", "max"};
}

ModelCapabilities ModelCapabilities::suggestions(const QJsonObject& metadata, const QString& apiBase,
                                                const QString& model)
{
    auto result = fromMetadata(metadata);
    if (result.reasoning && !*result.reasoning) return result;
    if (const auto documented = documentedFallback(apiBase, model)) {
        if (!result.efforts && documented->efforts) {
            result.efforts = documented->efforts;
            result.effortSource = Source::Documentation;
            if (result.defaultEffort.isEmpty()) result.defaultEffort = documented->defaultEffort;
        }
        if (!result.thinkingModes && documented->thinkingModes) {
            result.thinkingModes = documented->thinkingModes;
            result.thinkingSource = Source::Documentation;
        }
    }
    return result;
}
