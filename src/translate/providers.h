#ifndef SNAPLENS_TRANSLATE_PROVIDERS_H
#define SNAPLENS_TRANSLATE_PROVIDERS_H

#include <QString>
#include <QMap>
#include <QUrl>

struct ProviderConfig {
    QString apiBase;
    QString defaultModel;
    QString label;
};

// Predefined AI providers with their default API base URLs
inline QMap<QString, ProviderConfig> PROVIDER_CONFIGS = {
    {"custom",   {"", "", "自定义 OpenAI 兼容服务"}},
    {"deepseek", {"https://api.deepseek.com/v1", "deepseek-chat", "DeepSeek (深度求索)"}},
    {"openai",   {"https://api.openai.com/v1",       "gpt-4o",         "OpenAI"}},
    {"qwen",     {"https://dashscope.aliyuncs.com/compatible-mode/v1", "qwen-vl-plus", "通义千问 (Qwen)"}},
    {"kimi",     {"https://api.moonshot.cn/v1",      "moonshot-v1-8k", "Kimi (月之暗面)"}},
    {"glm",      {"https://open.bigmodel.cn/api/paas/v4", "glm-4v",   "智谱 GLM (ZhipuAI)"}},
    {"hunyuan",  {"https://api.hunyuan.cloud.tencent.com/v1", "hunyuan-vision", "腾讯混元 (HunYuan)"}},
    {"doubao",   {"https://ark.cn-beijing.volces.com/api/v3", "doubao-vision-pro-32k", "豆包 (火山方舟)"}},
    {"qianfan",  {"https://qianfan.baidubce.com/v2",  "ernie-4.0-turbo-8k", "文心一言 (百度千帆)"}},
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

inline QStringList listProviders() {
    return PROVIDER_CONFIGS.keys();
}

inline ProviderConfig getProviderConfig(const QString& provider) {
    return PROVIDER_CONFIGS.value(provider.toLower());
}

#endif
