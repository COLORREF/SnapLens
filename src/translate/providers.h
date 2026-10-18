#ifndef SNAPLENS_TRANSLATE_PROVIDERS_H
#define SNAPLENS_TRANSLATE_PROVIDERS_H

#include <QString>
#include <QStringList>
#include <QMap>
#include <QUrl>
#include <QtGlobal>

#include <QJsonArray>
#include <QJsonDocument>

// Chat Completions 接口预设。模型支持情况不能从服务商名称推断。
enum class ThinkingStyle {
    None,               // 无默认思考开关形式
    ThinkingObject,     // {"thinking": {"type": "enabled" | "disabled"}}
    EnableThinkingFlag  // {"enable_thinking": true | false}（通义千问）
};

// 预设只提供地址和字段形式，不代表某服务商的所有模型能力相同。
// 模型能力以端点返回的信息及对应模型文档为准；未知信息不猜测。
struct ProviderConfig {
    QString apiBase;
    QString defaultModel;
    QString label;
    QString docUrl;
    ThinkingStyle thinkingStyle = ThinkingStyle::None;
    QString maxTokensField = QStringLiteral("max_tokens");
};

inline const QMap<QString, ProviderConfig> PROVIDER_CONFIGS = {
    {"custom", {"", "", "自定义 OpenAI 兼容服务", ""}},
    {"deepseek", {"https://api.deepseek.com/v1", "deepseek-flash", "DeepSeek (深度求索)",
        "https://api-docs.deepseek.com/zh-cn/api/create-chat-completion", ThinkingStyle::ThinkingObject}},
    {"openai", {"https://api.openai.com/v1", "gpt-4o-mini", "OpenAI",
        "https://platform.openai.com/docs/api-reference/chat/create", ThinkingStyle::None, "max_completion_tokens"}},
    {"qwen", {"https://dashscope.aliyuncs.com/compatible-mode/v1", "qwen-plus", "通义千问 (Qwen)",
        "https://help.aliyun.com/zh/model-studio/compatibility-of-openai-with-dashscope", ThinkingStyle::EnableThinkingFlag}},
    {"kimi", {"https://api.moonshot.cn/v1", "kimi-k2.6", "Kimi (月之暗面)",
        "https://platform.kimi.com/docs/api/chat", ThinkingStyle::ThinkingObject, "max_completion_tokens"}},
    {"glm", {"https://open.bigmodel.cn/api/paas/v4", "glm-5.3", "智谱 GLM (ZhipuAI)",
        "https://docs.bigmodel.cn", ThinkingStyle::ThinkingObject}},
    {"hunyuan", {"https://api.hunyuan.cloud.tencent.com/v1", "hunyuan-turbos-latest", "腾讯混元 (HunYuan)",
        "https://cloud.tencent.com/document/product/1729/111007"}},
    {"doubao", {"https://ark.cn-beijing.volces.com/api/v3", "doubao-seed-2-0-lite-260428", "豆包 (火山方舟)",
        "https://www.volcengine.com/docs/82379/1494384", ThinkingStyle::ThinkingObject}},
    {"qianfan", {"https://qianfan.baidubce.com/v2", "ernie-4.0-turbo-8k", "文心一言 (百度千帆)",
        "https://cloud.baidu.com/doc/WENXINWORKSHOP/index.html"}}
};

inline bool providerNeedsApiKey(const QString& provider) {
    return provider.compare(QStringLiteral("custom"), Qt::CaseInsensitive) != 0;
}

// 用户填写的是 /v1 等接口根地址；统一处理末尾斜杠，避免生成 /v1//models。
inline QUrl providerEndpoint(const QString& apiBase, const QString& route) {
    QUrl url(apiBase.trimmed(), QUrl::StrictMode);
    if (!url.isValid() || url.isRelative() || url.host().isEmpty()
        || (url.scheme() != QStringLiteral("http") && url.scheme() != QStringLiteral("https"))
        || !url.query().isEmpty() || !url.fragment().isEmpty() || !url.userInfo().isEmpty())
        return {};
    QString path = url.path();
    while (path.endsWith('/')) path.chop(1);
    url.setPath(path + '/' + route);
    return url;
}

// 同一端点/模型使用同一配置；协议路径是键的一部分。
inline QString aiModelProfileKey(const QString& apiBase, const QString& model)
{
    return QString::fromUtf8(QJsonDocument(QJsonArray{
        providerEndpoint(apiBase.trimmed(), QStringLiteral("chat/completions")).toString(),
        model.trimmed()}).toJson(QJsonDocument::Compact));
}

inline QStringList listProviders() {
    return PROVIDER_CONFIGS.keys();
}

inline ProviderConfig getProviderConfig(const QString& provider) {
    // 未知服务商使用通用 Chat Completions 地址/字段预设。
    return PROVIDER_CONFIGS.value(provider.toLower(), PROVIDER_CONFIGS.value(QStringLiteral("custom")));
}

#endif // SNAPLENS_TRANSLATE_PROVIDERS_H
