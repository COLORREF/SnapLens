#include "ocr_window.h"
#include "ocr_engine.h"
#include "../settings.h"
#include "../notify/notify_manager.h"
#include "../ui/zoomable_view.h"
#include "../ui/snapkit.h"
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

using namespace SnapUi;

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
    OcrEngine engine; // 每个任务拥有独立的原生引擎和语言配置。

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
    : ElaWindow(parent)
    , m_pixmap(pixmap)
    , m_settings(settings)
    , m_notifyManager(notifyManager)
{
    setWindowTitle("SnapLens · OCR 识别");
    resize(1120, 700);
    setMinimumSize(820, 520);
    setIsNavigationBarEnable(false);
    setIsStayTop(true);
    setAttribute(Qt::WA_DeleteOnClose);
    setIsDefaultClosed(false);
    connect(this, &ElaWindow::closeButtonClicked, this, [this] { close(); });

    buildUi();
    startOcr();
    polishPopup(this);
}

bool OcrWindow::isBusy() const
{
    return m_workerThread && m_workerThread->isRunning();
}

void OcrWindow::buildUi()
{
    auto* central = new QWidget(this);
    central->setWindowTitle("OCR 识别");
    auto* root = new QVBoxLayout(central);
    root->setContentsMargins(Dim::PageHMargin, 16, Dim::PageHMargin, 16);
    root->setSpacing(14);

    auto* headerRow = new QHBoxLayout();
    headerRow->setSpacing(16);
    headerRow->addWidget(pageHeader(
        "OCR 文字识别",
        "左侧核对截图（滚轮缩放、拖动平移），右侧为识别结果，可整段复制。",
        central), 1);
    headerRow->addStretch();

    m_copyBtn = primaryButton("复制识别结果", central);
    m_copyBtn->setEnabled(false);
    connect(m_copyBtn, &QPushButton::clicked, this, &OcrWindow::copyResult);
    headerRow->addWidget(m_copyBtn, 0, Qt::AlignBottom);
    root->addLayout(headerRow);

    auto* workspace = new Card(central);
    auto* workspaceLayout = new QVBoxLayout(workspace);
    workspaceLayout->setContentsMargins(10, 10, 10, 10);
    workspaceLayout->setSpacing(0);

    auto* splitter = new QSplitter(Qt::Horizontal, workspace);
    splitter->setChildrenCollapsible(false);

    auto* imagePanel = new PanelCard("截图预览",
                                     "滚轮缩放 · 按住拖动平移", workspace);
    m_imageView = new ZoomableView();
    m_imageView->setPixmap(m_pixmap);
    imagePanel->setContent(m_imageView);
    splitter->addWidget(imagePanel);

    auto* textPanel = new PanelCard("识别结果", {}, workspace);
    m_textEdit = new ElaPlainTextEdit();
    m_textEdit->setReadOnly(true);
    m_textEdit->setPlaceholderText("正在识别中，请稍候…");
    textPanel->setContent(m_textEdit);
    splitter->addWidget(textPanel);

    splitter->setSizes({460, 560});
    workspaceLayout->addWidget(splitter);
    root->addWidget(workspace, 1);

    auto* statusRow = new QHBoxLayout();
    statusRow->setSpacing(8);
    m_progressRing = new ElaProgressRing(central);
    m_progressRing->setFixedSize(18, 18);
    m_progressRing->setBusyingWidth(3);
    m_progressRing->setIsBusying(true);
    statusRow->addWidget(m_progressRing);
    m_statusLabel = caption("正在识别图片中的文字…", central);
    statusRow->addWidget(m_statusLabel);
    statusRow->addStretch();
    root->addLayout(statusRow);
    addCentralWidget(central);
}

void OcrWindow::startOcr()
{
    QString tessdata = m_settings ? m_settings->ocr_tessdata_dir : "";
    QString langs = m_settings ? m_settings->ai_ocr_langs : "chi_sim+eng+jpn+kor";

    // Convert QPixmap to QImage on the main thread (QPixmap is NOT thread-safe)
    QImage image = m_pixmap.toImage().convertToFormat(QImage::Format_RGB888);

    m_workerThread = new QThread(this);
    auto* worker = new OcrWorker(image, langs, tessdata);
    worker->moveToThread(m_workerThread);

    connect(m_workerThread, &QThread::started, worker, &OcrWorker::process);
    m_resultConnection = connect(worker, &OcrWorker::finished, this, &OcrWindow::onResult);
    m_errorConnection = connect(worker, &OcrWorker::error, this, &OcrWindow::onError);
    // quit() 线程安全，直接调用使收尾不依赖主线程事件循环仍在运行。
    connect(worker, &OcrWorker::finished, m_workerThread, &QThread::quit, Qt::DirectConnection);
    connect(worker, &OcrWorker::error, m_workerThread, &QThread::quit, Qt::DirectConnection);
    connect(m_workerThread, &QThread::finished, worker, &QObject::deleteLater);

    m_workerThread->start();
}

void OcrWindow::onResult(const QString& text)
{
    m_textEdit->setPlainText(text);
    m_copyBtn->setEnabled(!text.isEmpty());
    m_statusLabel->setText("识别完成 · 可直接复制或继续截图");
    m_progressRing->setVisible(false);
}

void OcrWindow::onError(const QString& msg)
{
    m_textEdit->setPlainText(QString("[识别失败]\n%1").arg(msg));
    m_statusLabel->setText("识别失败");
    m_progressRing->setVisible(false);
    ElaMessageBar::error(ElaMessageBarType::TopRight,
                         "OCR 识别失败", msg, 4200, this);
    if (m_notifyManager) {
        m_notifyManager->notify("ocr_fail", "OCR 识别失败", msg);
    }
}

void OcrWindow::copyResult()
{
    QString text = m_textEdit->toPlainText();
    if (!text.isEmpty() && !text.startsWith("[识别失败]")) {
        QGuiApplication::clipboard()->setText(text);
        m_statusLabel->setText("识别结果已复制到剪贴板");
        ElaMessageBar::success(ElaMessageBarType::TopRight,
                               "已复制", "识别结果已放入剪贴板。", 1800, this);
    }
}

OcrWindow::~OcrWindow()
{
    releaseWorker();
}

void OcrWindow::releaseWorker()
{
    // Tesseract 识别不能安全强杀；丢弃结果，由任务自然结束后释放引擎。
    if (m_workerThread) {
        disconnect(m_resultConnection);
        disconnect(m_errorConnection);
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
    }

}

void OcrWindow::closeEvent(QCloseEvent* event)
{
    releaseWorker();
    emit closed();
    ElaWindow::closeEvent(event);
}
