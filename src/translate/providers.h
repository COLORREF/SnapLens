#ifndef SNAPLENS_TRANSLATE_PROVIDERS_H
#define SNAPLENS_TRANSLATE_PROVIDERS_H

#include <QString>
#include <QMap>

struct ProviderConfig {
    QString apiBase;
    QString defaultModel;
    QString label;
};

// Predefined AI providers with their default API base URLs
inline QMap<QString, ProviderConfig> PROVIDER_CONFIGS = {
    {"deepseek", {"https://api.deepseek.com/v1", "deepseek-chat", "DeepSeek (深度求索)"}},
    {"openai",   {"https://api.openai.com/v1",       "gpt-4o",         "OpenAI"}},
    {"qwen",     {"https://dashscope.aliyuncs.com/compatible-mode/v1", "qwen-vl-plus", "通义千问 (Qwen)"}},
    {"kimi",     {"https://api.moonshot.cn/v1",      "moonshot-v1-8k", "Kimi (月之暗面)"}},
    {"glm",      {"https://open.bigmodel.cn/api/paas/v4", "glm-4v",   "智谱 GLM (ZhipuAI)"}},
    {"hunyuan",  {"https://api.hunyuan.cloud.tencent.com/v1", "hunyuan-vision", "腾讯混元 (HunYuan)"}},
    {"doubao",   {"https://ark.cn-beijing.volces.com/api/v3", "doubao-vision-pro-32k", "豆包 (火山方舟)"}},
    {"qianfan",  {"https://qianfan.baidubce.com/v2",  "ernie-4.0-turbo-8k", "文心一言 (百度千帆)"}},
};

inline QStringList listProviders() {
    return PROVIDER_CONFIGS.keys();
}

inline ProviderConfig getProviderConfig(const QString& provider) {
    return PROVIDER_CONFIGS.value(provider.toLower());
}

#endif