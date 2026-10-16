#ifndef SNAPLENS_OCR_OCR_WINDOW_H
#define SNAPLENS_OCR_OCR_WINDOW_H

#include "ElaWidgetTools.hpp"

#include <QImage>
#include <QPixmap>
#include <QString>
#include <QPointer>

class QPlainTextEdit;
class QPushButton;
class QLabel;
class QSplitter;
class Settings;
class NotifyManager;
class ZoomableView;

/** @brief 后台识别任务，持有图像与配置快照，不读取界面状态。 */
class OcrWorker : public QObject {
    Q_OBJECT
public:
    explicit OcrWorker(const QImage& image, const QString& ocrLangs,
                       const QString& tessdataDir,
                       QObject* parent = nullptr);

public slots:
    void process();

signals:
    void finished(const QString& text);
    void error(const QString& message);

private:
    QImage  m_image;
    QString m_ocrLangs;
    QString m_tessdataDir;
};

/** @brief OCR 结果窗口；销毁时分离尚未完成的任务，保持界面响应。 */
class OcrWindow : public ElaWindow {
    Q_OBJECT
public:
    explicit OcrWindow(const QPixmap& pixmap, Settings* settings,
                       NotifyManager* notifyManager = nullptr, QWidget* parent = nullptr);
    ~OcrWindow() override;

    // 是否有 OCR 任务正在运行（供设置保存时"下次生效"提示检测）
    bool isBusy() const;

signals:
    void closed();

protected:
    void closeEvent(QCloseEvent* event) override;

private slots:
    void onResult(const QString& text);
    void onError(const QString& msg);
    void copyResult();

private:
    void buildUi();
    void startOcr();
    /** @brief 断开结果回写并转移运行中线程的清理责任；允许重复调用。 */
    void releaseWorker();

    QPixmap        m_pixmap;
    Settings*      m_settings = nullptr;
    NotifyManager* m_notifyManager = nullptr;

    QPushButton*   m_copyBtn = nullptr;
    QPlainTextEdit* m_textEdit = nullptr;
    QLabel*        m_statusLabel = nullptr;
    ZoomableView*  m_imageView = nullptr;
    ElaProgressRing* m_progressRing = nullptr;

    // 线程对象在 GUI 线程删除。worker 在后台删除，仅保留连接句柄以安全断开。
    QPointer<QThread>   m_workerThread;
    QMetaObject::Connection m_resultConnection;
    QMetaObject::Connection m_errorConnection;
};

#endif // SNAPLENS_OCR_OCR_WINDOW_H
