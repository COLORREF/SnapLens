#ifndef SNAPLENS_TRANSLATE_TRANSLATOR_H
#define SNAPLENS_TRANSLATE_TRANSLATOR_H

#include <QThread>
#include <QImage>
#include <QPixmap>
#include <QString>
#include <QStringList>
#include <atomic>

class Settings;

/** @brief 一次 AI 翻译请求的不可变参数快照。
 * @details 在 GUI 线程构造，随后只由工作线程读取，避免与主线程并发访问
 * Settings（QString 隐式共享的并发读写为未定义行为）。参数按服务商能力
 * 在 ApiClient 中裁剪，这里只负责原样携带用户配置。
 */
struct AiRequestOptions {
    QString provider;
    QString apiKey;
    QString apiBase;
    QString model;
    int     timeout = 30;
    double  temperature = 0.1;
    int     maxTokens = 4096;
    double  topP = 1.0;
    double  frequencyPenalty = 0.0;
    double  presencePenalty = 0.0;
    int     seed = 0;
    QString doSample = QStringLiteral("auto");
    QString thinking = QStringLiteral("auto");
    QString reasoningEffort = QStringLiteral("auto");
    QStringList stop;
    bool    streamUsage = true;      // 流式请求附带 stream_options.include_usage
    bool    streamThinking = false;  // 流式输出 + 思考过程实时推送

    /// 从配置快照提取 AI 请求参数（仅构造线程调用）。
    static AiRequestOptions fromSettings(const Settings& s);
    /// 停止序列在配置中以换行分隔存储；解析时按最严的 4 项上限截断。
    static QStringList parseStop(const QString& raw);
    static QString     joinStop(const QStringList& stop);
};

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
    AiRequestOptions m_ai;

    // 取消标志（主线程 requestCancel 写入，run() 线程读取；跨线程必须原子）
    std::atomic<int> m_cancelFlag{0};
};

#endif // SNAPLENS_TRANSLATE_TRANSLATOR_H
