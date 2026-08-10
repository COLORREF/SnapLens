#include "ocr_window.h"
#include "ocr_engine.h"
#include "../settings.h"
#include "../notify/notify_manager.h"
#include "../ui/zoomable_view.h"
#include "../log/log_manager.h"

#include <QClipboard>
#include <QCloseEvent>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSplitter>
#include <QThread>
#include <QVBoxLayout>
#include <QDir>

// ============================================================================
// OcrWorker
// ============================================================================

OcrWorker::OcrWorker(const QImage& image, const QString& ocrLangs,
                     const QString& tessdataDir,
                     QObject* parent)
    : QObject(parent)
    , m_image(image)
    , m_ocrLangs(ocrLangs)
    , m_tessdataDir(tessdataDir)
{
}

void OcrWorker::process()
{
    OcrEngine& engine = OcrEngine::instance();

    // Determine tessdata directory
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

    // Initialize engine (init() handles language change internally, no-op if already correct)
    if (!engine.init(tessDir, m_ocrLangs)) {
        emit error(QString("OCR 引擎初始化失败。\n语言包目录：%1\n语言：%2")
            .arg(tessDir, m_ocrLangs));
        return;
    }

    // Image already in main-thread-safe RGB888 format (converted before worker creation)
    QImage image = m_image.format() == QImage::Format_RGB888
                       ? m_image
                       : m_image.convertToFormat(QImage::Format_RGB888);
    QString text = engine.extractText(
        image.constBits(),
        image.width(),
        image.height(),
        3,  // bytesPerPixel (RGB888)
        image.bytesPerLine()
    );

    if (text.isEmpty()) {
        emit finished("图片中未检测到文字");
    } else {
        emit finished(text.trimmed());
    }
}

// ============================================================================
// OcrWindow
// ============================================================================

OcrWindow::OcrWindow(const QPixmap& pixmap, Settings* settings,
                     NotifyManager* notifyManager, QWidget* parent)
    : QWidget(parent)
    , m_pixmap(pixmap)
    , m_settings(settings)
    , m_notifyManager(notifyManager)
{
    setWindowTitle("OCR 识别");
    resize(900, 560);
    setMinimumSize(640, 400);
    setWindowFlags(Qt::Window | Qt::WindowStaysOnTopHint);
    setAttribute(Qt::WA_DeleteOnClose);

    buildUi();
    startOcr();
}

bool OcrWindow::isBusy() const
{
    return m_workerThread && m_workerThread->isRunning();
}

void OcrWindow::buildUi()
{
    QVBoxLayout* root = new QVBoxLayout(this);
    root->setContentsMargins(12, 12, 12, 12);
    root->setSpacing(10);

    // Top toolbar
    QHBoxLayout* toolbar = new QHBoxLayout();
    toolbar->setSpacing(8);
    toolbar->addStretch();

    m_copyBtn = new QPushButton("复制 OCR 原文");
    m_copyBtn->setEnabled(false);
    connect(m_copyBtn, &QPushButton::clicked, this, &OcrWindow::copyResult);
    toolbar->addWidget(m_copyBtn);

    root->addLayout(toolbar);

    // Main: left-right split
    QSplitter* splitter = new QSplitter(Qt::Horizontal);

    // Left: zoomable image
    m_imageView = new ZoomableView();
    m_imageView->setPixmap(m_pixmap);
    splitter->addWidget(m_imageView);

    // Right: OCR text
    QWidget* rightPanel = new QWidget();
    QVBoxLayout* rightLayout = new QVBoxLayout(rightPanel);
    rightLayout->setContentsMargins(0, 0, 0, 0);

    m_textEdit = new QPlainTextEdit();
    m_textEdit->setReadOnly(true);
    m_textEdit->setPlaceholderText("正在识别中，请稍候...");
    rightLayout->addWidget(m_textEdit);
    splitter->addWidget(rightPanel);

    splitter->setSizes({400, 460});
    root->addWidget(splitter);

    // Status bar
    m_statusLabel = new QLabel("正在识别...");
    root->addWidget(m_statusLabel);
}

void OcrWindow::startOcr()
{
    QString tessdata = m_settings ? m_settings->ocr_tessdata_dir : "";
    QString langs = m_settings ? m_settings->ai_ocr_langs : "chi_sim+eng+jpn+kor";

    // Convert QPixmap to QImage on the main thread (QPixmap is NOT thread-safe)
    QImage image = m_pixmap.toImage().convertToFormat(QImage::Format_RGB888);

    m_workerThread = new QThread(this);
    m_worker = new OcrWorker(image, langs, tessdata);
    m_worker->moveToThread(m_workerThread);

    connect(m_workerThread, &QThread::started, m_worker, &OcrWorker::process);
    connect(m_worker, &OcrWorker::finished, this, &OcrWindow::onResult);
    connect(m_worker, &OcrWorker::error, this, &OcrWindow::onError);
    connect(m_worker, &OcrWorker::finished, m_workerThread, &QThread::quit);
    connect(m_worker, &OcrWorker::error, m_workerThread, &QThread::quit);
    connect(m_workerThread, &QThread::finished, m_worker, &QObject::deleteLater);

    m_workerThread->start();
}

void OcrWindow::onResult(const QString& text)
{
    m_textEdit->setPlainText(text);
    m_copyBtn->setEnabled(!text.isEmpty());
    m_statusLabel->setText("OCR 识别完成");
}

void OcrWindow::onError(const QString& msg)
{
    m_textEdit->setPlainText(QString("[识别失败]\n%1").arg(msg));
    m_statusLabel->setText("识别失败");
    if (m_notifyManager) {
        m_notifyManager->notify("ocr_fail", "OCR 识别失败", msg);
    }
}

void OcrWindow::copyResult()
{
    QString text = m_textEdit->toPlainText();
    if (!text.isEmpty() && !text.startsWith("[识别失败]")) {
        QGuiApplication::clipboard()->setText(text);
        m_statusLabel->setText("已复制到剪贴板");
    }
}

void OcrWindow::closeEvent(QCloseEvent* event)
{
    // UX 原则：关闭窗口必须立即生效，绝不阻塞主线程、绝不强杀线程
    // （QThread::terminate 若落在 OcrEngine::extractText 持锁阶段，会损坏进程级
    //  单例的互斥锁，导致之后所有 OCR/翻译永久死锁）。
    // 处理方式：断开回写信号，让 worker 在后台自然跑完；线程结束后对象自清理。
    if (m_workerThread) {
        if (m_worker) {
            // 无条件断开回写连接：即使线程已结束但 finished 事件仍在队列，
            // 窗口销毁后也不能再触发 onResult/onError（否则 use-after-free）。
            disconnect(m_worker, nullptr, this, nullptr);
        }
        if (m_workerThread->isRunning()) {
            SNAP_LOG_INFO("OcrWindow: 关闭时识别仍在进行，已转后台完成，结果将被丢弃");
            m_workerThread->quit();
            // 分离线程对象：窗口销毁时不再拥有它（避免 "QThread: Destroyed while
            // thread is still running" 崩溃）；线程结束后由 finished → deleteLater 自清理。
            m_workerThread->setParent(nullptr);
            connect(m_workerThread, &QThread::finished,
                    m_workerThread, &QObject::deleteLater);
        }
        m_workerThread = nullptr;
        m_worker = nullptr;
    }

    emit closed();
    QWidget::closeEvent(event);
}