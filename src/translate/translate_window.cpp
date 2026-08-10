#include "translate_window.h"
#include "translator.h"
#include "../settings.h"
#include "../notify/notify_manager.h"
#include "../ui/zoomable_view.h"
#include "../log/log_manager.h"

#include <QComboBox>
#include <QPushButton>
#include <QLabel>
#include <QTabWidget>
#include <QPlainTextEdit>
#include <QSplitter>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QCloseEvent>
#include <QGuiApplication>
#include <QClipboard>

static const QStringList TARGET_LANGUAGES = {
    "简体中文", "繁體中文", "English", "日本語", "한국어",
    "Français", "Deutsch", "Español", "Português", "Русский",
    "Italiano", "العربية", "हिन्दी", "ไทย", "Tiếng Việt"
};

TranslateWindow::TranslateWindow(const QPixmap& pixmap, Settings* settings,
                                 bool autoTranslate, NotifyManager* notifyManager,
                                 QWidget* parent)
    : QWidget(parent)
    , m_pixmap(pixmap)
    , m_settings(settings)
    , m_autoTranslate(autoTranslate)
    , m_notifyManager(notifyManager)
{
    setWindowTitle("AI 翻译");
    resize(1100, 640);
    setMinimumSize(800, 480);
    setWindowFlags(Qt::Window | Qt::WindowStaysOnTopHint);
    setAttribute(Qt::WA_DeleteOnClose);

    buildUi();

    if (m_autoTranslate) {
        launchService();
    } else {
        m_transEdit->setPlaceholderText("请选择目标语言后点击「开始翻译」");
        m_translateBtn->setText("开始翻译");
        m_translateBtn->setEnabled(true);
        m_statusLabel->setText("就绪 - 请选择翻译目标语言");
    }
}

bool TranslateWindow::isBusy() const
{
    return m_service && m_service->isRunning();
}

void TranslateWindow::buildUi()
{
    QVBoxLayout* root = new QVBoxLayout(this);
    root->setContentsMargins(12, 12, 12, 12);
    root->setSpacing(8);

    // Top toolbar
    QHBoxLayout* toolbar = new QHBoxLayout();
    toolbar->addWidget(new QLabel("目标语言："));

    m_langCombo = new QComboBox();
    m_langCombo->setEditable(true);
    m_langCombo->addItems(TARGET_LANGUAGES);
    QString targetLang = m_settings ? m_settings->ai_target_lang : "简体中文";
    int idx = m_langCombo->findText(targetLang);
    if (idx >= 0) m_langCombo->setCurrentIndex(idx);
    else m_langCombo->setCurrentText(targetLang);
    m_langCombo->setMinimumWidth(100);
    // Auto-translate when language changes (matching Python behavior)
    connect(m_langCombo, &QComboBox::currentTextChanged, this, [this](const QString&) {
        if (m_autoTranslate && !m_translating) {
            startTranslation();
        }
    });
    toolbar->addWidget(m_langCombo);

    m_translateBtn = new QPushButton("重新翻译");
    m_translateBtn->setEnabled(false);
    connect(m_translateBtn, &QPushButton::clicked, this, &TranslateWindow::startTranslation);
    toolbar->addWidget(m_translateBtn);

    m_copyTransBtn = new QPushButton("复制翻译结果");
    m_copyTransBtn->setEnabled(false);
    connect(m_copyTransBtn, &QPushButton::clicked, this, &TranslateWindow::copyTranslated);
    toolbar->addWidget(m_copyTransBtn);

    m_copyOcrBtn = new QPushButton("复制 OCR 原文");
    m_copyOcrBtn->setEnabled(false);
    connect(m_copyOcrBtn, &QPushButton::clicked, this, &TranslateWindow::copyOcrText);
    toolbar->addWidget(m_copyOcrBtn);

    toolbar->addStretch();

    m_closeBtn = new QPushButton("关闭");
    connect(m_closeBtn, &QPushButton::clicked, this, &QWidget::close);
    toolbar->addWidget(m_closeBtn);

    root->addLayout(toolbar);

    // Main: splitter with image + tabs
    QSplitter* splitter = new QSplitter(Qt::Horizontal);

    m_imageView = new ZoomableView();
    m_imageView->setPixmap(m_pixmap);
    splitter->addWidget(m_imageView);

    // Right: tab widget
    m_tabWidget = new QTabWidget();

    m_transEdit = new QPlainTextEdit();
    m_transEdit->setReadOnly(true);
    m_transEdit->setPlaceholderText("正在翻译中，请稍候...");
    m_tabWidget->addTab(m_transEdit, "翻 译");

    m_ocrEdit = new QPlainTextEdit();
    m_ocrEdit->setReadOnly(true);
    m_tabWidget->addTab(m_ocrEdit, "OCR 原文");

    m_thinkEdit = new QPlainTextEdit();
    m_thinkEdit->setReadOnly(true);
    m_thinkEdit->setPlaceholderText("AI 思考过程将在此处显示...");
    m_tabWidget->addTab(m_thinkEdit, "AI 思考");

    splitter->addWidget(m_tabWidget);
    splitter->setSizes({500, 560});
    root->addWidget(splitter);

    // Status bar
    m_statusLabel = new QLabel("正在翻译...");
    root->addWidget(m_statusLabel);
}

void TranslateWindow::launchService()
{
    QString lang = m_langCombo->currentText().trimmed();
    if (lang.isEmpty()) lang = "简体中文";

    m_translating = true;
    m_translateBtn->setText(QString("翻译为「%1」").arg(lang));
    m_translateBtn->setEnabled(false);
    m_langCombo->setEnabled(false);

    // Convert QPixmap to QImage on the main thread (QPixmap is NOT thread-safe)
    QImage image = m_pixmap.toImage().convertToFormat(QImage::Format_RGB888);

    m_service = new TranslateService(image, lang, m_settings);
    connect(m_service, &TranslateService::translated, this, &TranslateWindow::onTranslated);
    connect(m_service, &TranslateService::ocrText, this, &TranslateWindow::onOcrText);
    connect(m_service, &TranslateService::thinking, this, &TranslateWindow::onThinking);
    connect(m_service, &TranslateService::error, this, &TranslateWindow::onError);
    connect(m_service, &QThread::finished, m_service, &QObject::deleteLater);
    m_service->start();
}

void TranslateWindow::startTranslation()
{
    // 安全停止旧服务（m_service 为 QPointer，删除后自动置空）：
    // 1. requestCancel() 置位取消标志 —— ApiClient 每 100ms 轮询该标志，
    //    置位后立即 abort 底层请求并断开网络，旧请求不再继续生成（省 token）；
    // 2. 断开信号，防止运行中的线程把结果回写 UI；
    // 3. 不 wait：线程在取消后 ≤几百 ms 内自然结束，由 finished → deleteLater
    //    自清理（launchService 已建立该连接），主线程零卡顿。
    if (m_service) {
        SNAP_LOG_INFO("TranslateWindow: 重新翻译 —— 取消旧翻译服务，立即断开网络连接");
        m_service->requestCancel();
        disconnect(m_service, nullptr, this, nullptr);
        if (!m_service->isRunning()) {
            m_service->deleteLater();
        }
        m_service = nullptr;
    }

    m_transEdit->clear();
    m_ocrEdit->clear();
    m_thinkEdit->clear();
    m_transEdit->setPlaceholderText("正在翻译中，请稍候...");
    m_translateBtn->setEnabled(false);
    m_statusLabel->setText("正在翻译...");

    launchService();
}

void TranslateWindow::onTranslated(const QString& text)
{
    m_translating = false;
    m_transEdit->setPlainText(text);
    m_copyTransBtn->setEnabled(!text.isEmpty());
    m_translateBtn->setEnabled(true);
    m_translateBtn->setText("重新翻译");
    m_langCombo->setEnabled(true);
    m_statusLabel->setText(
        QString("翻译完成 - 目标语言：%1").arg(m_langCombo->currentText().trimmed()));
    if (m_notifyManager) {
        m_notifyManager->notify("translate_success", "AI 翻译",
            QString("翻译完成（%1）").arg(m_langCombo->currentText().trimmed()));
    }
}

void TranslateWindow::onOcrText(const QString& text)
{
    m_ocrText = text;
    m_ocrEdit->setPlainText(text);
    m_copyOcrBtn->setEnabled(!text.isEmpty());
}

void TranslateWindow::onThinking(const QString& text)
{
    m_thinkEdit->setPlainText(text);
}

void TranslateWindow::onError(const QString& msg)
{
    m_translating = false;
    m_transEdit->setPlainText(QString("[翻译失败]\n%1").arg(msg));
    m_translateBtn->setEnabled(true);
    m_translateBtn->setText("重新翻译");
    m_langCombo->setEnabled(true);
    m_statusLabel->setText("翻译失败");
    if (m_notifyManager) {
        m_notifyManager->notify("translate_fail", "AI 翻译失败", msg);
    }
}

void TranslateWindow::copyTranslated()
{
    QString text = m_transEdit->toPlainText();
    if (!text.isEmpty() && !text.startsWith("[翻译失败]")) {
        QGuiApplication::clipboard()->setText(text);
        m_statusLabel->setText("已复制到剪贴板");
    }
}

void TranslateWindow::copyOcrText()
{
    if (!m_ocrText.isEmpty()) {
        QGuiApplication::clipboard()->setText(m_ocrText);
        m_statusLabel->setText("已复制 OCR 原文");
    }
}

void TranslateWindow::closeEvent(QCloseEvent* event)
{
    // 关闭窗口 = 用户不再关心本次翻译结果：立即取消请求、断开网络，
    // 避免服务端继续生成内容造成 token 浪费（Python 原版与旧实现均为
    // "跑完丢弃"，此处修复）。
    // requestCancel() 置位后 ApiClient 每 100ms 轮询 → abort 断网 → 线程
    // ≤几百 ms 内自然结束 → finished → deleteLater 自清理（m_service 为
    // QPointer，窗口删除后也不会访问悬空指针）。
    if (m_service) {
        SNAP_LOG_INFO("TranslateWindow: 窗口关闭 —— 立即取消翻译并断开网络连接（省 token）");
        m_service->requestCancel();
        disconnect(m_service, nullptr, this, nullptr);
        if (!m_service->isRunning()) {
            m_service->deleteLater();
        }
        m_service = nullptr;
    }
    emit closed();
    QWidget::closeEvent(event);
}