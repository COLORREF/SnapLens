#include "translate_window.h"
#include "translator.h"
#include "../settings.h"
#include "../notify/notify_manager.h"
#include "../ui/zoomable_view.h"
#include "../ui/snapkit.h"
#include "../log/log_manager.h"

#include <QComboBox>
#include <QPushButton>
#include <QLabel>
#include <QPlainTextEdit>
#include <QSplitter>
#include <QStackedWidget>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QCloseEvent>
#include <QGuiApplication>
#include <QClipboard>

using namespace SnapUi;

static const QStringList TARGET_LANGUAGES = {
    "简体中文", "繁體中文", "English", "日本語", "한국어",
    "Français", "Deutsch", "Español", "Português", "Русский",
    "Italiano", "العربية", "हिन्दी", "ไทย", "Tiếng Việt"
};

TranslateWindow::TranslateWindow(const QPixmap& pixmap, Settings* settings,
                                 bool autoTranslate, NotifyManager* notifyManager,
                                 QWidget* parent)
    : ElaWindow(parent)
    , m_pixmap(pixmap)
    , m_settings(settings)
    , m_autoTranslate(autoTranslate)
    , m_notifyManager(notifyManager)
{
    setWindowTitle("SnapLens · 截图翻译");
    resize(1200, 760);
    setMinimumSize(900, 560);
    setIsNavigationBarEnable(false);
    setIsStayTop(true);
    setAttribute(Qt::WA_DeleteOnClose);
    setIsDefaultClosed(false);
    connect(this, &ElaWindow::closeButtonClicked, this, [this] { close(); });

    buildUi();

    if (m_autoTranslate) {
        launchService();
    } else {
        m_transEdit->setPlaceholderText("请选择目标语言后点击「开始翻译」");
        m_translateBtn->setText("开始翻译");
        m_translateBtn->setEnabled(true);
        m_statusLabel->setText("就绪 · 请选择目标语言后开始");
        m_progressRing->setVisible(false);
    }
    polishPopup(this);
}

bool TranslateWindow::isBusy() const
{
    return m_service && m_service->isRunning();
}

void TranslateWindow::buildUi()
{
    auto* central = new QWidget(this);
    central->setWindowTitle("截图翻译");
    auto* root = new QVBoxLayout(central);
    root->setContentsMargins(Dim::PageHMargin, 16, Dim::PageHMargin, 16);
    root->setSpacing(14);

    // ── 页头 ──
    auto* headerRow = new QHBoxLayout();
    headerRow->setSpacing(16);
    headerRow->addWidget(pageHeader(
        "截图翻译",
        "左侧核对截图（滚轮缩放、拖动平移），右侧查看译文、OCR 原文与 AI 思考过程。",
        central), 1);
    headerRow->addStretch();

    // 复制动作放在页头右侧，与"查看结果"这一主线动作区分开
    m_copyOcrBtn = secondaryButton("复制原文", central);
    m_copyOcrBtn->setEnabled(false);
    connect(m_copyOcrBtn, &QPushButton::clicked, this, &TranslateWindow::copyOcrText);
    headerRow->addWidget(m_copyOcrBtn, 0, Qt::AlignBottom);

    m_copyTransBtn = primaryButton("复制译文", central);
    m_copyTransBtn->setEnabled(false);
    connect(m_copyTransBtn, &QPushButton::clicked, this, &TranslateWindow::copyTranslated);
    headerRow->addWidget(m_copyTransBtn, 0, Qt::AlignBottom);
    root->addLayout(headerRow);

    // ── 命令区 ──
    auto* commandCard = new Card(central);
    auto* toolbar = new QHBoxLayout(commandCard);
    toolbar->setContentsMargins(16, 12, 16, 12);
    toolbar->setSpacing(10);
    toolbar->addWidget(detailText("目标语言", commandCard));

    m_langCombo = new ElaComboBox(commandCard);
    m_langCombo->setEditable(true);
    m_langCombo->addItems(TARGET_LANGUAGES);
    const QString targetLang = m_settings ? m_settings->ai_target_lang : QStringLiteral("简体中文");
    const int idx = m_langCombo->findText(targetLang);
    if (idx >= 0) m_langCombo->setCurrentIndex(idx);
    else m_langCombo->setCurrentText(targetLang);
    m_langCombo->setMinimumWidth(140);
    m_langCombo->setToolTip("切换语言会立即重新翻译当前截图");
    // Auto-translate when language changes (matching Python behavior)
    connect(m_langCombo, &QComboBox::currentTextChanged, this, [this](const QString&) {
        if (m_autoTranslate && !m_translating) {
            startTranslation();
        }
    });
    toolbar->addWidget(m_langCombo);
    toolbar->addStretch();

    m_translateBtn = primaryButton("重新翻译", commandCard);
    m_translateBtn->setEnabled(false);
    connect(m_translateBtn, &QPushButton::clicked, this, &TranslateWindow::startTranslation);
    toolbar->addWidget(m_translateBtn);
    root->addWidget(commandCard);

    // ── 工作区：左图右文 ──
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

    auto* textPanel = new PanelCard("翻译结果", {}, workspace);
    m_pivot = new ElaPivot(textPanel);
    m_pivot->setTextPixelSize(15);
    m_pivot->setPivotSpacing(18);
    m_pivot->setMarkWidth(52);
    m_pivot->appendPivot("译文");
    m_pivot->appendPivot("OCR 原文");
    m_pivot->appendPivot("AI 思考");
    m_pivot->setCurrentIndex(0);

    m_stack = new QStackedWidget(textPanel);
    m_stack->setStyleSheet(QStringLiteral("QStackedWidget { background: transparent; }"));

    m_transEdit = new ElaPlainTextEdit();
    m_transEdit->setReadOnly(true);
    m_transEdit->setPlaceholderText("正在翻译中，请稍候…");
    m_stack->addWidget(m_transEdit);

    m_ocrEdit = new ElaPlainTextEdit();
    m_ocrEdit->setReadOnly(true);
    m_ocrEdit->setPlaceholderText("识别到的原文将显示在此处…");
    m_stack->addWidget(m_ocrEdit);

    m_thinkEdit = new ElaPlainTextEdit();
    m_thinkEdit->setReadOnly(true);
    m_thinkEdit->setPlaceholderText("AI 思考过程将在此处显示…");
    m_stack->addWidget(m_thinkEdit);

    connect(m_pivot, &ElaPivot::pivotClicked, m_stack, &QStackedWidget::setCurrentIndex);

    textPanel->addContentWidget(m_pivot);
    textPanel->setContent(m_stack);
    splitter->addWidget(textPanel);

    splitter->setSizes({520, 660});
    workspaceLayout->addWidget(splitter);
    root->addWidget(workspace, 1);

    // ── 状态栏 ──
    auto* statusRow = new QHBoxLayout();
    statusRow->setSpacing(8);
    m_progressRing = new ElaProgressRing(central);
    m_progressRing->setFixedSize(18, 18);
    m_progressRing->setBusyingWidth(3);
    m_progressRing->setIsBusying(true);
    statusRow->addWidget(m_progressRing);
    m_statusLabel = caption("正在识别截图内容…", central);
    statusRow->addWidget(m_statusLabel);
    statusRow->addStretch();
    root->addLayout(statusRow);
    addCentralWidget(central);
}

void TranslateWindow::launchService()
{
    QString lang = m_langCombo->currentText().trimmed();
    if (lang.isEmpty()) lang = "简体中文";

    m_translating = true;
    m_progressRing->setVisible(true);
    m_progressRing->setIsBusying(true);
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
    m_transEdit->setPlaceholderText("正在翻译中，请稍候…");
    m_translateBtn->setEnabled(false);
    m_statusLabel->setText("正在翻译…");

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
        QString("翻译完成 · 目标语言：%1").arg(m_langCombo->currentText().trimmed()));
    m_progressRing->setVisible(false);
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
    m_progressRing->setVisible(false);
    ElaMessageBar::error(ElaMessageBarType::TopRight,
                         "截图翻译失败", msg, 4200, this);
    if (m_notifyManager) {
        m_notifyManager->notify("translate_fail", "AI 翻译失败", msg);
    }
}

void TranslateWindow::copyTranslated()
{
    QString text = m_transEdit->toPlainText();
    if (!text.isEmpty() && !text.startsWith("[翻译失败]")) {
        QGuiApplication::clipboard()->setText(text);
        m_statusLabel->setText("译文已复制到剪贴板");
        ElaMessageBar::success(ElaMessageBarType::TopRight,
                               "已复制", "译文已放入剪贴板。", 1800, this);
    }
}

void TranslateWindow::copyOcrText()
{
    if (!m_ocrText.isEmpty()) {
        QGuiApplication::clipboard()->setText(m_ocrText);
        m_statusLabel->setText("OCR 原文已复制到剪贴板");
        ElaMessageBar::success(ElaMessageBarType::TopRight,
                               "已复制", "OCR 原文已放入剪贴板。", 1800, this);
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
    ElaWindow::closeEvent(event);
}
