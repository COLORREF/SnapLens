#ifndef SNAPLENS_TRANSLATE_PROVIDERS_H
#define SNAPLENS_TRANSLATE_PROVIDERS_H

#include <QString>
#include <QStringList>
#include <QMap>
#include <QUrl>
#include <QtGlobal>

#include <climits>
#include <cmath>

// ============================================================================
// 服务商能力描述
//
// 各家的 "OpenAI 兼容" 接口差异很大，不能一套字段发给所有服务商：
//   · Kimi K 系列：temperature / top_p / frequency_penalty / presence_penalty
//     全部固定不可改，传入自定义值会直接返回 invalid_request_error；
//     输出长度字段是 max_completion_tokens，max_tokens 已弃用。
//   · DeepSeek：frequency_penalty / presence_penalty 已废弃，传了不生效；
//     思考模式用 thinking + reasoning_effort，且思考模式下 temperature 不生效。
//   · 智谱 GLM：temperature 上限是 1（非 2），另有 do_sample 总开关；
//     翻译这类一致性任务官方建议 do_sample=false。
//   · 通义千问：思考开关是顶层 enable_thinking，不是 thinking 对象。
//   · 混元 / OpenAI：没有 thinking 对象，OpenAI 只有 reasoning_effort。
//
// 因此每个服务商在此声明真实支持情况（依据各厂商官方 API 文档，见 docUrl），
// 请求构建层据此裁剪字段；未声明的参数一律不发送，交给服务商默认值。
// ============================================================================

/// 思维链参数的请求形式。
enum class ThinkingStyle {
    None,               // 不支持思考参数（混元 / OpenAI / 自定义 / 千帆）
    ThinkingObject,     // {"thinking": {"type": "enabled" | "disabled"}}
    EnableThinkingFlag  // {"enable_thinking": true | false}（通义千问）
};

struct ProviderConfig {
    QString apiBase;
    QString defaultModel;
    QString label;
    QString docUrl;                 // 参数依据来源，便于核对与更新

    // —— 采样参数支持情况 ——
    bool temperature = true;         // false = 服务商固定值，发送会被拒绝
    bool topP = true;
    bool frequencyPenalty = false;
    bool presencePenalty = false;
    bool seed = false;
    bool stop = true;
    bool samplingToggle = false;     // GLM do_sample：false 时忽略 temperature/top_p

    // 温度 / 核采样可调区间（取自官方文档；GLM 温度上限为 1）
    double temperatureMin = 0.0;
    double temperatureMax = 2.0;
    double topPMin = 0.0;
    double topPMax = 1.0;

    // —— 思考（推理）——
    ThinkingStyle thinkingStyle = ThinkingStyle::None;
    bool thinkingCanDisable = true;  // GLM-5.3 / Kimi K3 等强制思考时为 false
    bool reasoningEffort = false;
    QStringList effortOptions;       // 允许档位；空 = 不限制

    // —— 输出长度 ——
    QString maxTokensField = QStringLiteral("max_tokens");
    int maxTokensLimit = 8192;       // 官方上限，仅用于界面提示与校验

    // —— 流式用量 ——
    bool usageInStream = true;       // 支持 stream_options.include_usage
};

// 预设 AI 服务商及其默认接口地址。docUrl 指向参数依据的官方文档。
inline QMap<QString, ProviderConfig> PROVIDER_CONFIGS = {
    {"custom", {
        "", "", "自定义 OpenAI 兼容服务", "",
        true, true, true, true, true, true, false,
        0.0, 2.0, 0.0, 1.0,
        // 自定义端点可能是任意兼容实现，默认不发送思考字段（auto），
        // 但允许用户显式开启，便于接入本地推理模型。
        ThinkingStyle::ThinkingObject, true, true, {},
        QStringLiteral("max_tokens"), 8192, true}},

    {"deepseek", {
        "https://api.deepseek.com/v1", "deepseek-flash", "DeepSeek (深度求索)",
        "https://api-docs.deepseek.com/zh-cn/api/create-chat-completion",
        true, true, false, false, false, true, false,
        0.0, 2.0, 0.0, 1.0,
        ThinkingStyle::ThinkingObject, true, true,
        {"none", "low", "high", "max"},
        QStringLiteral("max_tokens"), 393216, true}},

    {"openai", {
        "https://api.openai.com/v1", "gpt-4o-mini", "OpenAI",
        "https://platform.openai.com/docs/api-reference/chat/create",
        true, true, true, true, true, true, false,
        0.0, 2.0, 0.0, 1.0,
        ThinkingStyle::None, true, true, {"none", "minimal", "low", "medium", "high"},
        QStringLiteral("max_completion_tokens"), 16384, true}},

    {"qwen", {
        "https://dashscope.aliyuncs.com/compatible-mode/v1", "qwen-plus", "通义千问 (Qwen)",
        "https://help.aliyun.com/zh/model-studio/compatibility-of-openai-with-dashscope",
        true, true, false, true, true, true, false,
        0.0, 2.0, 0.0, 1.0,
        ThinkingStyle::EnableThinkingFlag, true, false, {},
        QStringLiteral("max_tokens"), 32768, true}},

    {"kimi", {
        "https://api.moonshot.cn/v1", "kimi-k2.6", "Kimi (月之暗面)",
        "https://platform.kimi.com/docs/api/chat",
        // temperature / top_p / 惩罚项在 Kimi K 系列固定不可改，一律不发送
        false, false, false, false, false, true, false,
        0.0, 1.0, 0.0, 1.0,
        ThinkingStyle::ThinkingObject, true, false, {},
        QStringLiteral("max_completion_tokens"), 1048576, true}},

    {"glm", {
        "https://open.bigmodel.cn/api/paas/v4", "glm-5.3", "智谱 GLM (ZhipuAI)",
        "https://docs.bigmodel.cn/api-reference/%E6%A8%A1%E5%9E%8B-api/%E5%AF%B9%E8%AF%9D%E8%A1%A5%E5%85%A8",
        true, true, false, false, false, true, true,
        0.0, 1.0, 0.01, 1.0,
        ThinkingStyle::ThinkingObject, true, true, {"low", "high", "max"},
        QStringLiteral("max_tokens"), 131072, true}},

    {"hunyuan", {
        "https://api.hunyuan.cloud.tencent.com/v1", "hunyuan-turbos-latest", "腾讯混元 (HunYuan)",
        "https://cloud.tencent.com/document/product/1729/111007",
        true, true, false, false, true, true, false,
        0.0, 2.0, 0.0, 1.0,
        ThinkingStyle::None, true, false, {},
        QStringLiteral("max_tokens"), 8192, true}},

    {"doubao", {
        "https://ark.cn-beijing.volces.com/api/v3", "doubao-seed-2-0-lite-260428", "豆包 (火山方舟)",
        "https://www.volcengine.com/docs/82379/1494384",
        true, true, true, true, false, true, false,
        0.0, 2.0, 0.0, 1.0,
        ThinkingStyle::ThinkingObject, true, true,
        {"none", "minimal", "low", "medium", "high", "xhigh", "max"},
        QStringLiteral("max_tokens"), 65536, true}},

    {"qianfan", {
        "https://qianfan.baidubce.com/v2", "ernie-4.0-turbo-8k", "文心一言 (百度千帆)",
        "https://cloud.baidu.com/doc/WENXINWORKSHOP/index.html",
        true, true, false, false, false, true, false,
        0.0, 2.0, 0.0, 1.0,
        ThinkingStyle::None, true, false, {},
        QStringLiteral("max_tokens"), 8192, true}},
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
    // 未知服务商按自定义（OpenAI 兼容超集）处理，避免拿到全 false 的空配置。
    return PROVIDER_CONFIGS.value(provider.toLower(), PROVIDER_CONFIGS.value(QStringLiteral("custom")));
}

// 推理强度档位排序：用户选的档位若不在服务商允许列表内，就近映射到允许档位，
// 避免直接发送不支持的取值导致 400（例如 GLM-5.3 只接受 low/high/max）。
inline QString normalizeReasoningEffort(const QString& requested, const QStringList& allowed)
{
    if (allowed.isEmpty()) return requested;
    static const QStringList ladder = {
        QStringLiteral("none"), QStringLiteral("minimal"), QStringLiteral("low"),
        QStringLiteral("medium"), QStringLiteral("high"), QStringLiteral("xhigh"),
        QStringLiteral("max")};

    const QString want = requested.trimmed().toLower();
    if (want.isEmpty() || want == QStringLiteral("auto")) return {};
    if (allowed.contains(want)) return want;

    const int wantRank = ladder.indexOf(want);
    if (wantRank < 0) return {};

    int best = -1;
    int bestDist = INT_MAX;
    for (const QString& option : allowed) {
        const int rank = ladder.indexOf(option.trimmed().toLower());
        if (rank < 0) continue;
        const int dist = std::abs(rank - wantRank);
        if (dist < bestDist) { bestDist = dist; best = rank; }
    }
    return best >= 0 ? ladder.at(best) : QString();
}

#endif // SNAPLENS_TRANSLATE_PROVIDERS_H
