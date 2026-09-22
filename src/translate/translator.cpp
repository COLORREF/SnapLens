#include "translator.h"
#include "ai_client.h"
#include "providers.h"
#include "../settings.h"
#include "../ocr/ocr_engine.h"
#include "../log/log_manager.h"

#include <QImage>
#include <QJsonArray>
#include <QJsonObject>

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
        m_provider           = settings->ai_provider;
        m_tessdataDir        = settings->ocr_tessdata_dir;
        m_ocrLangs           = settings->ai_ocr_langs;
        m_apiKey             = settings->ai_api_key;
        m_apiBase            = settings->ai_api_base;
        m_model              = settings->ai_model;
        m_translationPrompt  = settings->ai_translation_prompt;
        m_streamThinking     = settings->ai_stream_thinking;
        m_timeout            = settings->ai_timeout;
        m_temperature        = settings->ai_temperature;
        m_maxTokens          = settings->ai_max_tokens;
        m_topP               = settings->ai_top_p;
        m_frequencyPenalty   = settings->ai_frequency_penalty;
        m_presencePenalty    = settings->ai_presence_penalty;
        m_seed               = settings->ai_seed;
    }
}

void TranslateService::run()
{
    try {
        // 1. OCR 提取文字
        OcrEngine engine; // Each job owns its language configuration and native engine.

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
        QString apiKey = m_apiKey;
        if (apiKey.isEmpty() && providerNeedsApiKey(m_provider)) {
            emit error("API Key 未设置，请在设置中配置");
            return;
        }

        QString apiBase = m_apiBase;
        if (apiBase.isEmpty()) {
            ProviderConfig cfg = getProviderConfig(m_provider);
            apiBase = cfg.apiBase;
        }

        QString model = m_model;
        if (model.isEmpty()) {
            ProviderConfig cfg = getProviderConfig(m_provider);
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

        ApiClient client;

        if (m_streamThinking) {
            // 流式调用 — chatCreateStream 已在回调中累积完整 content
            int ret = client.chatCreateStream(
                apiKey, apiBase, model, messages,
                m_timeout,
                m_temperature,
                m_maxTokens,
                m_topP,
                m_frequencyPenalty,
                m_presencePenalty,
                m_seed,
                [this](const QString& content, const QString& thinking,
                       bool isFinal, int errorCode, const QString& errorMsg) {
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
                },
                &m_cancelFlag
            );

            if (ret != 0) {
                if (m_cancelFlag.load(std::memory_order_relaxed)) {
                    SNAP_LOG_INFO("TranslateService: AI 请求已取消（网络已断开），线程退出");
                    return;
                }
                emit error("翻译请求失败");
            }
        } else {
            // 非流式调用（传入取消标志：窗口关闭/替换时立即 abort 断网）
            auto result = client.chatCreate(
                apiKey, apiBase, model, messages,
                m_timeout,
                m_temperature,
                m_maxTokens,
                m_topP,
                m_frequencyPenalty,
                m_presencePenalty,
                m_seed,
                &m_cancelFlag);

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
                                           const QString& apiKey,
                                           const QString& apiBase,
                                           const QString& model,
                                           int timeout,
                                           double temperature,
                                           int maxTokens,
                                           double topP,
                                           double frequencyPenalty,
                                           double presencePenalty,
                                           int seed,
                                           bool streamThinking,
                                           QObject* parent)
    : QThread(parent)
    , m_sourceText(sourceText)
    , m_targetLang(targetLang)
    , m_scenario(scenario)
    , m_promptTemplate(promptTemplate)
    , m_fullPrompt(fullPrompt)
    , m_apiKey(apiKey)
    , m_apiBase(apiBase)
    , m_model(model)
    , m_timeout(timeout)
    , m_temperature(temperature)
    , m_maxTokens(maxTokens)
    , m_topP(topP)
    , m_frequencyPenalty(frequencyPenalty)
    , m_presencePenalty(presencePenalty)
    , m_seed(seed)
    , m_streamThinking(streamThinking)
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

        ApiClient client;

        if (m_streamThinking) {
            // 流式调用 — chatCreateStream 已在回调中累积完整 content
            // （传入取消标志：窗口关闭/替换时立即 abort 断网，省 token）
            int ret = client.chatCreateStream(
                m_apiKey, m_apiBase, m_model, messages,
                m_timeout, m_temperature, m_maxTokens,
                m_topP, m_frequencyPenalty, m_presencePenalty, m_seed,
                [this](const QString& content, const QString& thinking,
                       bool isFinal, int errorCode, const QString& errorMsg) {
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
                },
                &m_cancelFlag
            );
            if (ret != 0) {
                if (m_cancelFlag.load(std::memory_order_relaxed)) {
                    SNAP_LOG_INFO("TextTranslateService: AI 请求已取消（网络已断开），线程退出");
                    return;
                }
                emit error("翻译请求失败");
            }
        } else {
            auto result = client.chatCreate(
                m_apiKey, m_apiBase, m_model, messages,
                m_timeout, m_temperature, m_maxTokens,
                m_topP, m_frequencyPenalty, m_presencePenalty, m_seed,
                &m_cancelFlag);
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
