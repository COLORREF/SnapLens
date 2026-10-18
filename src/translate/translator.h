#ifndef SNAPLENS_TRANSLATE_TRANSLATOR_H
#define SNAPLENS_TRANSLATE_TRANSLATOR_H

#include "ai_request_options.h"
#include <QThread>
#include <QImage>
#include <QString>
#include <atomic>

class Settings;

/** @brief 后台图片翻译任务：独立 OCR 引擎、配置快照和可取消的 AI 请求。 */
class TranslateService : public QThread
{
    Q_OBJECT

public:
    TranslateService(const QImage& image,
                     const QString& targetLang,
                     Settings* settings,
                     QObject* parent = nullptr,
                     const AiRequestOptions* aiOverride = nullptr);

    /** @brief 请求取消；OCR 自然结束后检查，网络阶段以 100ms 间隔检查并中止请求。 */
    void requestCancel() { m_cancelFlag.store(1, std::memory_order_relaxed); }

signals:
    void translated(const QString& text);
    void ocrText(const QString& text);
    void thinking(const QString& text);
    void error(const QString& msg);

protected:
    void run() override;

private:
    QImage   m_image;
    QString  m_targetLang;

    // 工作线程只读构造时的快照。
    QString  m_tessdataDir;
    QString  m_ocrLangs;
    QString  m_translationPrompt;
    AiRequestOptions m_ai;

    // 取消标志（主线程 requestCancel 写入，run() 线程读取；跨线程必须原子）
    std::atomic<int> m_cancelFlag{0};
};

/** @brief 后台文本翻译任务，使用构造时传入的不可变配置快照。 */
class TextTranslateService : public QThread
{
    Q_OBJECT

public:
    TextTranslateService(const QString& sourceText,
                         const QString& targetLang,
                         const QString& scenario,
                         const QString& promptTemplate,
                         const QString& fullPrompt,
                         const AiRequestOptions& ai,
                         QObject* parent = nullptr);

    /// 网络阶段在 100ms 内检查取消标志。
    void requestCancel() { m_cancelFlag.store(1, std::memory_order_relaxed); }

signals:
    void translated(const QString& text);
    void thinking(const QString& text);
    void error(const QString& msg);

protected:
    void run() override;

private:
    QString m_sourceText;
    QString m_targetLang;
    QString m_scenario;
    QString m_promptTemplate;
    QString m_fullPrompt;
    AiRequestOptions m_ai;

    // 取消标志（主线程 requestCancel 写入，run() 线程读取；跨线程必须原子）
    std::atomic<int> m_cancelFlag{0};
};

#endif // SNAPLENS_TRANSLATE_TRANSLATOR_H
