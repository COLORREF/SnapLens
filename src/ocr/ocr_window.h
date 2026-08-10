#ifndef SNAPLENS_OCR_OCR_WINDOW_H
#define SNAPLENS_OCR_OCR_WINDOW_H

#include <QWidget>
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

// OcrWorker runs OCR in a background thread to avoid blocking the UI
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

class OcrWindow : public QWidget {
    Q_OBJECT
public:
    explicit OcrWindow(const QPixmap& pixmap, Settings* settings,
                       NotifyManager* notifyManager = nullptr, QWidget* parent = nullptr);

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

    QPixmap        m_pixmap;
    Settings*      m_settings = nullptr;
    NotifyManager* m_notifyManager = nullptr;

    QPushButton*   m_copyBtn = nullptr;
    QPlainTextEdit* m_textEdit = nullptr;
    QLabel*        m_statusLabel = nullptr;
    ZoomableView*  m_imageView = nullptr;

    // QPointer：worker/线程对象在线程结束后会被 deleteLater 自动删除，
    // 用 QPointer 确保删除后成员自动置空，closeEvent 不会访问悬空指针。
    QPointer<QThread>   m_workerThread;
    QPointer<OcrWorker> m_worker;
};

#endif // SNAPLENS_OCR_OCR_WINDOW_H