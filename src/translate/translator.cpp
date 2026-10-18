#include "translator.h"
#include "providers.h"
#include "../settings.h"
#include "../ocr/ocr_engine.h"
#include "../log/log_manager.h"

#include <QImage>
#include <QJsonArray>
#include <QJsonObject>

TranslateService::TranslateService(const QImage& image,
                                   const QString& targetLang,
                                   Settings* settings,
                                   QObject* parent, const AiRequestOptions* aiOverride)
    : QThread(parent)
    , m_image(image)
    , m_targetLang(targetLang)
{
    // 在调用线程复制设置；工作线程不持有 Settings。
    if (settings) {
        m_tessdataDir       = settings->ocr_tessdata_dir;
        m_ocrLangs          = settings->ai_ocr_langs;
        m_translationPrompt = settings->ai_translation_prompt;
        m_ai                = aiOverride ? *aiOverride : AiRequestOptions::fromSettings(*settings);
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

        // OCR 不能中途打断；取消后不再进入网络阶段。
        if (m_cancelFlag.load(std::memory_order_relaxed)) {
            SNAP_LOG_INFO("TranslateService: OCR 后检测到取消请求，跳过 AI 请求（省 token），线程退出");
            return;
        }

        if (rawText.trimmed().isEmpty()) {
            emit translated("图片中未检测到文字");
            return;
        }

        // 2. AI 翻译（使用构造时拷贝的配置快照）
        if (m_ai.apiKey.isEmpty() && providerNeedsApiKey(m_ai.provider)) {
            emit error("API Key 未设置，请在设置中配置");
            return;
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

        m_ai.execute(messages, &m_cancelFlag,
            [this](const QString& content, const QString& reasoning,
                   bool final, int code, const QString& message) {
                if (m_cancelFlag.load(std::memory_order_relaxed)) return;
                if (final && code != 0) {
                    emit error(message.isEmpty() ? "翻译请求失败" : message);
                    return;
                }
                if (!reasoning.isEmpty()) emit thinking(reasoning);
                if (final) emit translated(content);
            });
    } catch (const std::exception& e) {
        emit error(QString("翻译失败：%1").arg(e.what()));
    } catch (...) {
        emit error("翻译失败：未知错误");
    }
}

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

        m_ai.execute(messages, &m_cancelFlag,
            [this](const QString& content, const QString& reasoning,
                   bool final, int code, const QString& message) {
                if (m_cancelFlag.load(std::memory_order_relaxed)) return;
                if (final && code != 0) {
                    emit error(message.isEmpty() ? "翻译请求失败" : message);
                    return;
                }
                if (!reasoning.isEmpty()) emit thinking(reasoning);
                if (final) emit translated(content);
            });
    } catch (const std::exception& e) {
        emit error(QString("翻译失败：%1").arg(e.what()));
    } catch (...) {
        emit error("翻译失败：未知错误");
    }
}
