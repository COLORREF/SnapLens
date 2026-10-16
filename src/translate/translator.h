#ifndef SNAPLENS_TRANSLATE_TRANSLATOR_H
#define SNAPLENS_TRANSLATE_TRANSLATOR_H

#include <QThread>
#include <QImage>
#include <QPixmap>
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
                     QObject* parent = nullptr);

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

    // 配置快照：构造时从 Settings 一次性拷贝，run() 只读这些成员。
    // 避免工作线程与主线程（模态设置对话框）并发访问同一 Settings 对象
    // 造成数据竞争（QString 隐式共享的并发读写为未定义行为）。
    QString  m_provider;
    QString  m_tessdataDir;
    QString  m_ocrLangs;
    QString  m_apiKey;
    QString  m_apiBase;
    QString  m_model;
    QString  m_translationPrompt;
    bool     m_streamThinking = false;
    int      m_timeout = 30;
    double   m_temperature = 0.1;
    int      m_maxTokens = 4096;
    double   m_topP = 1.0;
    double   m_frequencyPenalty = 0.0;
    double   m_presencePenalty = 0.0;
    int      m_seed = 0;

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
                         QObject* parent = nullptr);

    // 请求取消：置位取消标志。AI 请求阶段由 ApiClient 的 cancelTimer 每 100ms
    // 轮询该标志，置位后立即 abort 底层请求并断开网络（关闭窗口/替换时调用，
    // 避免"跑完翻译却丢弃结果"造成的 token 浪费）。
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
    QString m_apiKey;
    QString m_apiBase;
    QString m_model;
    int     m_timeout;
    double  m_temperature;
    int     m_maxTokens;
    double  m_topP;
    double  m_frequencyPenalty;
    double  m_presencePenalty;
    int     m_seed;
    bool    m_streamThinking;

    // 取消标志（主线程 requestCancel 写入，run() 线程读取；跨线程必须原子）
    std::atomic<int> m_cancelFlag{0};
};

#endif // SNAPLENS_TRANSLATE_TRANSLATOR_H
