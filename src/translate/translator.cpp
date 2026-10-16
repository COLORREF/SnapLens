#include "translator.h"
#include "ai_client.h"
#include "providers.h"
#include "../settings.h"
#include "../ocr/ocr_engine.h"
#include "../log/log_manager.h"

#include <QImage>
#include <QJsonArray>
#include <QJsonObject>
#include <QRegularExpression>

// ============================================================================
// AiRequestOptions
// ============================================================================

AiRequestOptions AiRequestOptions::fromSettings(const Settings& s)
{
    AiRequestOptions o;
    o.provider          = s.ai_provider;
    o.apiKey            = s.ai_api_key;
    o.apiBase           = s.ai_api_base;
    o.model             = s.ai_model;
    o.timeout           = s.ai_timeout;
    o.temperature       = s.ai_temperature;
    o.maxTokens         = s.ai_max_tokens;
    o.topP              = s.ai_top_p;
    o.frequencyPenalty  = s.ai_frequency_penalty;
    o.presencePenalty   = s.ai_presence_penalty;
    o.seed              = s.ai_seed;
    o.doSample          = s.ai_do_sample;
    o.thinking          = s.ai_thinking;
    o.reasoningEffort   = s.ai_reasoning_effort;
    o.stop              = AiRequestOptions::parseStop(s.ai_stop);
    o.streamUsage       = s.ai_stream_usage;
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
        if (result.size() >= 4) break;   // 各家上限不同，按最严的 4 项截断
    }
    return result;
}

QString AiRequestOptions::joinStop(const QStringList& stop)
{
    return stop.join(QLatin1Char('\n'));
}

// ============================================================================
// TranslateService — 图片翻译
// ============================================================================

TranslateService::TranslateService(const QImage& image,
                                   const QString& targetLang,
                                   Settings* settings,
                                   QObject* parent)
    : QThread(parent)
    , m_image(image)
    , m_targetLang(targetLang)
{
    // 一次性拷贝配置快照（此处仍运行在主线程，之后 run() 只读快照，
    // 与主线程对 Settings 的修改彻底解耦，消除数据竞争）。
    if (settings) {
        m_tessdataDir       = settings->ocr_tessdata_dir;
        m_ocrLangs          = settings->ai_ocr_langs;
        m_translationPrompt = settings->ai_translation_prompt;
        m_ai                = AiRequestOptions::fromSettings(*settings);
    }
}

void TranslateService::run()
{
    try {
        // 1. OCR 提取文字
        OcrEngine engine; // 每个任务独立拥有语言配置和原生引擎。

        QString tessDir = m_tessdataDir;
        if (tessDir.isEmpty()) {
            tessDir = OcrEngine::findTessdataDir();
        }
        if (tessDir.isEmpty()) {
            emit error("未找到 Tesseract 语言包（tessdata）。\n\n"
                       "请将 .traineddata 文件放入程序目录下的 tessdata/ 目录。\n\n"
                       "语言包可在设置对话框中在线下载。");
            return;
        }

        if (!engine.init(tessDir, m_ocrLangs)) {
            emit error(QString("OCR 引擎初始化失败。语言包目录：%1").arg(tessDir));
            return;
        }

        // Image already in main-thread-safe RGB888 format (converted before service creation)
        QImage image = m_image.format() == QImage::Format_RGB888
                           ? m_image
                           : m_image.convertToFormat(QImage::Format_RGB888);
        QString rawText = engine.extractText(
            image.constBits(), image.width(), image.height(),
            3, image.bytesPerLine());

        if (!rawText.isEmpty()) {
            emit ocrText(rawText.trimmed());
        }

        // 取消检查点：窗口已关闭/服务已被替换时，OCR 阶段结束后不再发起
        // AI 请求 —— 避免"跑完翻译却丢弃结果"造成的 token 浪费。
        if (m_cancelFlag.load(std::memory_order_relaxed)) {
            SNAP_LOG_INFO("TranslateService: OCR 后检测到取消请求，跳过 AI 请求（省 token），线程退出");
            return;
        }

        if (rawText.trimmed().isEmpty()) {
            emit translated("图片中未检测到文字");
            return;
        }

        // 2. AI 翻译（使用构造时拷贝的配置快照）
        const ProviderConfig cfg = getProviderConfig(m_ai.provider);

        if (m_ai.apiKey.isEmpty() && providerNeedsApiKey(m_ai.provider)) {
            emit error("API Key 未设置，请在设置中配置");
            return;
        }

        QString apiBase = m_ai.apiBase;
        if (apiBase.isEmpty()) {
            apiBase = cfg.apiBase;
        }

        QString model = m_ai.model;
        if (model.isEmpty()) {
            model = cfg.defaultModel;
        }

        // 构建提示词
        QString prompt = m_translationPrompt;
        prompt.replace("{target_lang}", m_targetLang);

        QJsonArray messages;
        QJsonObject sysMsg;
        sysMsg["role"] = "system";
        sysMsg["content"] = prompt;
        messages.append(sysMsg);

        QJsonObject userMsg;
        userMsg["role"] = "user";
        userMsg["content"] = rawText.trimmed();
        messages.append(userMsg);

        ApiClient::ChatParams params;
        params.apiKey            = m_ai.apiKey;
        params.apiBase           = apiBase;
        params.model             = model;
        params.provider          = m_ai.provider;
        params.messages          = messages;
        params.timeoutSecs       = m_ai.timeout;
        params.temperature       = m_ai.temperature;
        params.maxTokens         = m_ai.maxTokens;
        params.topP              = m_ai.topP;
        params.frequencyPenalty  = m_ai.frequencyPenalty;
        params.presencePenalty   = m_ai.presencePenalty;
        params.seed              = m_ai.seed;
        params.doSample          = m_ai.doSample;
        params.thinking          = m_ai.thinking;
        params.reasoningEffort   = m_ai.reasoningEffort;
        params.stop              = m_ai.stop;
        params.streamUsage       = m_ai.streamUsage;
        params.cancelFlag        = &m_cancelFlag;

        ApiClient client;

        if (m_ai.streamThinking) {
            // 流式调用 — chatCreateStream 已在回调中累积完整 content
            client.chatCreateStream(
                params,
                [this](const QString& content, const QString& thinking,
                       bool isFinal, int errorCode, const QString& errorMsg) {
                    if (m_cancelFlag.load(std::memory_order_relaxed)) return;
                    if (errorCode != 0 && isFinal) {
                        emit error(errorMsg.isEmpty() ? "翻译请求失败" : errorMsg);
                        return;
                    }
                    if (!thinking.isEmpty()) {
                        emit this->thinking(thinking);
                    }
                    if (isFinal) {
                        emit translated(content);
                    }
                }
            );
        } else {
            // 非流式调用（取消标志已在 params 中，窗口关闭/替换时立即 abort 断网）
            auto result = client.chatCreate(params);

            if (result.errorCode != 0) {
                if (m_cancelFlag.load(std::memory_order_relaxed)) {
                    SNAP_LOG_INFO("TranslateService: AI 请求已取消（网络已断开），线程退出");
                    return;
                }
                emit error(result.errorMsg.isEmpty() ? "翻译请求失败" : result.errorMsg);
                return;
            }

            if (!result.thinking.isEmpty()) {
                emit thinking(result.thinking);
            }
            emit translated(result.content);
        }
    } catch (const std::exception& e) {
        emit error(QString("翻译失败：%1").arg(e.what()));
    } catch (...) {
        emit error("翻译失败：未知错误");
    }
}

// ============================================================================
// TextTranslateService — 文本翻译
// ============================================================================

TextTranslateService::TextTranslateService(const QString& sourceText,
                                           const QString& targetLang,
                                           const QString& scenario,
                                           const QString& promptTemplate,
                                           const QString& fullPrompt,
                                           const AiRequestOptions& ai,
                                           QObject* parent)
    : QThread(parent)
    , m_sourceText(sourceText)
    , m_targetLang(targetLang)
    , m_scenario(scenario)
    , m_promptTemplate(promptTemplate)
    , m_fullPrompt(fullPrompt)
    , m_ai(ai)
{
}

void TextTranslateService::run()
{
    try {
        if (m_sourceText.trimmed().isEmpty()) {
            emit error("翻译内容为空");
            return;
        }

        QString prompt;
        if (!m_fullPrompt.isEmpty()) {
            prompt = m_fullPrompt;
        } else {
            prompt = m_promptTemplate;
            prompt.replace("{target_lang}", m_targetLang);
            prompt.replace("{scenario}", m_scenario);
            prompt.replace("{source_text}", m_sourceText);
        }

        QJsonArray messages;
        QJsonObject userMsg;
        userMsg["role"] = "user";
        userMsg["content"] = prompt;
        messages.append(userMsg);

        ApiClient::ChatParams params;
        params.apiKey            = m_ai.apiKey;
        params.apiBase           = m_ai.apiBase;
        params.model             = m_ai.model;
        params.provider          = m_ai.provider;
        params.messages          = messages;
        params.timeoutSecs       = m_ai.timeout;
        params.temperature       = m_ai.temperature;
        params.maxTokens         = m_ai.maxTokens;
        params.topP              = m_ai.topP;
        params.frequencyPenalty  = m_ai.frequencyPenalty;
        params.presencePenalty   = m_ai.presencePenalty;
        params.seed              = m_ai.seed;
        params.doSample          = m_ai.doSample;
        params.thinking          = m_ai.thinking;
        params.reasoningEffort   = m_ai.reasoningEffort;
        params.stop              = m_ai.stop;
        params.streamUsage       = m_ai.streamUsage;
        params.cancelFlag        = &m_cancelFlag;

        ApiClient client;

        if (m_ai.streamThinking) {
            // 流式调用 — chatCreateStream 已在回调中累积完整 content
            // （传入取消标志：窗口关闭/替换时立即 abort 断网，省 token）
            client.chatCreateStream(
                params,
                [this](const QString& content, const QString& thinking,
                       bool isFinal, int errorCode, const QString& errorMsg) {
                    if (m_cancelFlag.load(std::memory_order_relaxed)) return;
                    if (errorCode != 0 && isFinal) {
                        emit error(errorMsg.isEmpty() ? "翻译请求失败" : errorMsg);
                        return;
                    }
                    if (!thinking.isEmpty()) {
                        emit this->thinking(thinking);
                    }
                    if (isFinal) {
                        emit translated(content);
                    }
                }
            );
        } else {
            auto result = client.chatCreate(params);
            if (result.errorCode != 0) {
                if (m_cancelFlag.load(std::memory_order_relaxed)) {
                    SNAP_LOG_INFO("TextTranslateService: AI 请求已取消（网络已断开），线程退出");
                    return;
                }
                emit error(result.errorMsg.isEmpty() ? "翻译请求失败" : result.errorMsg);
                return;
            }
            if (!result.thinking.isEmpty()) {
                emit thinking(result.thinking);
            }
            emit translated(result.content);
        }
    } catch (const std::exception& e) {
        emit error(QString("翻译失败：%1").arg(e.what()));
    } catch (...) {
        emit error("翻译失败：未知错误");
    }
}
