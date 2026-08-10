#include "main_window.h"
#include "translator.h"
#include "../settings.h"
#include "../notify/notify_manager.h"
#include "../log/log_manager.h"

#include <QComboBox>
#include <QPushButton>
#include <QCheckBox>
#include <QPlainTextEdit>
#include <QDialog>
#include <QSplitter>
#include <QTabWidget>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QLabel>
#include <QCloseEvent>
#include <QMessageBox>
#include <QApplication>

// ============================================================================
// Scenario and language lists (aligned with Python version)
// ============================================================================

static const QStringList TRANSLATION_SCENARIOS = {
    "通用场景",
    "计算机 / IT",
    "医学",
    "金融经济",
    "法律",
    "学术论文",
    "文学",
};

static const QStringList TARGET_LANGUAGES = {
    "简体中文", "繁体中文",
    "英语", "日语", "韩语",
    "法语", "德语", "西班牙语",
    "葡萄牙语", "俄语", "阿拉伯语",
    "泰语", "越南语", "意大利语",
    "荷兰语", "波兰语", "土耳其语",
};

// ============================================================================
// CloseDialog
// ============================================================================

CloseDialog::CloseDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle("SnapLens");
    setMinimumWidth(320);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(new QLabel("关闭主窗口后："));

    m_btnTray = new QPushButton("后台运行（最小化到系统托盘）");
    connect(m_btnTray, &QPushButton::clicked, this, [this] { done(true); });
    layout->addWidget(m_btnTray);

    m_btnExit = new QPushButton("退出程序");
    connect(m_btnExit, &QPushButton::clicked, this, [this] { done(false); });
    layout->addWidget(m_btnExit);

    m_remember = new QCheckBox("记住我的选择，下次不再询问");
    layout->addWidget(m_remember);
}

void CloseDialog::done(bool toTray)
{
    m_decided = true;
    m_result = toTray;
    accept();
}

std::pair<QVariant, bool> CloseDialog::ask(QWidget* parent)
{
    CloseDialog dlg(parent);
    dlg.exec();
    if (!dlg.m_decided)
        return {QVariant(), false};
    return {QVariant(dlg.m_result), dlg.m_remember->isChecked()};
}

// ============================================================================
// SplitTabPanel — QTabWidget based, matching Python implementation
// ============================================================================

SplitTabPanel::SplitTabPanel(const QString& title1, const QString& title2,
                             QPlainTextEdit* edit1, QPlainTextEdit* edit2,
                             bool defaultMerged, QWidget* parent)
    : QWidget(parent)
    , m_edit1(edit1)
    , m_edit2(edit2)
    , m_title1(title1)
    , m_title2(title2)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    splitter = new QSplitter(Qt::Vertical);
    layout->addWidget(splitter);

    m_tabA = new QTabWidget();
    m_tabA->addTab(m_edit1, m_title1);
    splitter->addWidget(m_tabA);

    m_tabB = new QTabWidget();
    m_tabB->addTab(m_edit2, m_title2);
    splitter->addWidget(m_tabB);

    splitter->setCollapsible(0, false);
    splitter->setCollapsible(1, false);

    connect(m_tabA->tabBar(), &QTabBar::tabBarDoubleClicked,
            this, [this](int) { onDoubleClicked(true); });
    connect(m_tabB->tabBar(), &QTabBar::tabBarDoubleClicked,
            this, [this](int) { onDoubleClicked(false); });

    connect(splitter, &QSplitter::splitterMoved, this, [this](int, int) {
        auto sizes = splitter->sizes();
        if (sizes.size() >= 2 && sizes[0] > 0 && sizes[1] > 0) {
            int total = sizes[0] + sizes[1];
            m_savedRatio = total > 0 ? double(sizes[0]) / total : 0.7;
        }
    });

    if (defaultMerged) {
        mergeBintoA();
        splitter->setSizes({1, 0});
    } else {
        splitter->setSizes({400, 200});
    }
}

void SplitTabPanel::onDoubleClicked(bool toA)
{
    if (m_docked || m_reverse) {
        split();
    } else if (toA) {
        mergeBintoA();
    } else {
        mergeAintoB();
    }
}

void SplitTabPanel::mergeBintoA()
{
    m_tabA->addTab(m_edit2, m_title2);
    m_docked = true;
    m_tabB->hide();
    emit stateChanged();
}

void SplitTabPanel::mergeAintoB()
{
    m_tabB->insertTab(0, m_edit1, m_title1);
    m_reverse = true;
    m_tabA->hide();
    emit stateChanged();
}

void SplitTabPanel::split()
{
    if (m_reverse) {
        int idx = m_tabB->indexOf(m_edit1);
        if (idx >= 0) m_tabB->removeTab(idx);
        m_edit1->setParent(m_tabA);
        m_tabA->addTab(m_edit1, m_title1);
        m_reverse = false;
        m_tabA->show();
    }
    if (m_docked) {
        int idx = m_tabA->indexOf(m_edit2);
        if (idx >= 0) m_tabA->removeTab(idx);
        m_edit2->setParent(m_tabB);
        m_tabB->addTab(m_edit2, m_title2);
        m_docked = false;
        m_tabB->show();
    }
    int total = splitter->sizes().value(0) + splitter->sizes().value(1);
    if (total <= 0) total = 570;
    int sizeA = int(total * m_savedRatio);
    splitter->setSizes({sizeA, total - sizeA});
    emit stateChanged();
}

void SplitTabPanel::applyState(bool merged, double ratio)
{
    m_savedRatio = qBound(0.1, ratio, 0.9);
    if (merged && !isMerged()) {
        mergeBintoA();
        splitter->setSizes({1, 0});
    } else if (!merged && isMerged()) {
        split();
    } else {
        // Already in desired state, just apply ratio
        auto sizes = splitter->sizes();
        int total = sizes.value(0) + sizes.value(1);
        if (total <= 0) total = 570;
        int sizeA = int(total * m_savedRatio);
        splitter->setSizes({sizeA, total - sizeA});
    }
}

bool SplitTabPanel::isMerged() const { return m_docked || m_reverse; }

double SplitTabPanel::ratio() const { return m_savedRatio; }

// ============================================================================
// MainWindow
// ============================================================================

MainWindow::MainWindow(Settings* settings, NotifyManager* notifyManager,
                       const QString& appMode, QWidget* parent)
    : QMainWindow(parent)
    , m_settings(settings)
    , m_notifyManager(notifyManager)
    , m_appMode(appMode)
{
    setWindowTitle("SnapLens 翻译");
    resize(900, 600);
    setMinimumSize(640, 400);
    buildUi();
    refreshPromptPreview();
}

bool MainWindow::isBusy() const
{
    return m_service && m_service->isRunning();
}

void MainWindow::buildUi()
{
    QWidget* central = new QWidget();
    setCentralWidget(central);
    QVBoxLayout* root = new QVBoxLayout(central);
    root->setContentsMargins(12, 12, 12, 12);

    // ── Top bar ──
    QHBoxLayout* topBar = new QHBoxLayout();

    topBar->addWidget(new QLabel("场景："));
    m_scenarioCombo = new QComboBox();
    m_scenarioCombo->addItems(TRANSLATION_SCENARIOS);
    connect(m_scenarioCombo, &QComboBox::currentTextChanged,
            this, &MainWindow::refreshPromptPreview);
    topBar->addWidget(m_scenarioCombo);

    topBar->addWidget(new QLabel("目标语言："));
    m_targetLangCombo = new QComboBox();
    m_targetLangCombo->addItems(TARGET_LANGUAGES);
    int engIdx = m_targetLangCombo->findText("英语");
    if (engIdx >= 0) m_targetLangCombo->setCurrentIndex(engIdx);
    m_targetLangCombo->setMinimumWidth(100);
    connect(m_targetLangCombo, &QComboBox::currentTextChanged,
            this, &MainWindow::refreshPromptPreview);
    topBar->addWidget(m_targetLangCombo);

    m_settingsBtn = new QPushButton("设置");
    m_settingsBtn->setFixedWidth(60);
    connect(m_settingsBtn, &QPushButton::clicked, this, &MainWindow::settingsRequested);
    topBar->addWidget(m_settingsBtn);

    topBar->addStretch();

    m_layoutBtn = new QPushButton("上下布局");
    m_layoutBtn->setFixedWidth(80);
    connect(m_layoutBtn, &QPushButton::clicked, this, &MainWindow::toggleLayout);
    topBar->addWidget(m_layoutBtn);

    m_resetLayoutBtn = new QPushButton("还原布局");
    m_resetLayoutBtn->setFixedWidth(80);
    connect(m_resetLayoutBtn, &QPushButton::clicked, this, &MainWindow::resetLayout);
    topBar->addWidget(m_resetLayoutBtn);

    root->addLayout(topBar);

    // ── Main splitter ──
    m_splitter = new QSplitter(Qt::Horizontal);

    m_sourceEdit = new QPlainTextEdit();
    m_sourceEdit->setPlaceholderText("在此输入或粘贴要翻译的文本...");
    connect(m_sourceEdit, &QPlainTextEdit::textChanged,
            this, &MainWindow::refreshPromptPreview);

    m_promptEdit = new QPlainTextEdit();
    m_promptEdit->setPlaceholderText("提示词模板...");

    m_leftPanel = new SplitTabPanel("原文", "提示词", m_sourceEdit, m_promptEdit, true);
    m_splitter->addWidget(m_leftPanel);

    m_targetEdit = new QPlainTextEdit();
    m_targetEdit->setReadOnly(true);
    m_targetEdit->setPlaceholderText("翻译结果将显示在此处...");

    m_thinkEdit = new QPlainTextEdit();
    m_thinkEdit->setReadOnly(true);
    m_thinkEdit->setPlaceholderText("AI思考过程将在此处显示...");

    m_rightPanel = new SplitTabPanel("译文", "AI 思考", m_targetEdit, m_thinkEdit, false);
    m_splitter->addWidget(m_rightPanel);

    // Restore layout state from settings
    if (m_settings) {
        m_leftPanel->applyState(m_settings->left_panel_merged, m_settings->left_panel_ratio);
        m_rightPanel->applyState(m_settings->right_panel_merged, m_settings->right_panel_ratio);
        int total = m_splitter->sizes().value(0) + m_splitter->sizes().value(1);
        if (total <= 0) total = 860;
        double r = m_settings->main_split_ratio;
        m_splitter->setSizes({int(total * r), int(total * (1 - r))});

        // Restore layout orientation
        if (m_settings->layout_orientation) {
            m_splitter->setOrientation(Qt::Horizontal);
            m_leftPanel->splitter->setOrientation(Qt::Vertical);
            m_rightPanel->splitter->setOrientation(Qt::Vertical);
            m_layoutBtn->setText("上下布局");
        } else {
            m_splitter->setOrientation(Qt::Vertical);
            m_leftPanel->splitter->setOrientation(Qt::Horizontal);
            m_rightPanel->splitter->setOrientation(Qt::Horizontal);
            m_layoutBtn->setText("左右布局");
        }
    }

    root->addWidget(m_splitter);

    // ── Bottom bar ──
    QHBoxLayout* bottomBar = new QHBoxLayout();
    bottomBar->addStretch();

    m_translateBtn = new QPushButton("翻译");
    m_translateBtn->setMinimumWidth(100);
    connect(m_translateBtn, &QPushButton::clicked, this, &MainWindow::onTranslate);
    bottomBar->addWidget(m_translateBtn);

    root->addLayout(bottomBar);
}

void MainWindow::refreshPromptPreview()
{
    if (!m_settings) return;
    QString prompt = m_settings->text_translation_prompt;
    prompt.replace("{target_lang}", m_targetLangCombo->currentText());
    prompt.replace("{scenario}", m_scenarioCombo->currentText());
    prompt.replace("{source_text}", m_sourceEdit->toPlainText());
    m_promptEdit->setPlainText(prompt);
}

void MainWindow::onTranslate()
{
    QString sourceText = m_sourceEdit->toPlainText().trimmed();
    if (sourceText.isEmpty()) return;

    if (!m_settings || m_settings->ai_api_key.isEmpty()) {
        m_targetEdit->setPlainText("[错误] API Key 未设置，请在设置中配置。");
        return;
    }

    // 安全停止旧服务（m_service 为 QPointer，删除后自动置空）：
    // 1. requestCancel() 置位取消标志 —— ApiClient 每 100ms 轮询该标志，
    //    置位后立即 abort 底层请求并断开网络，旧请求不再继续生成（省 token）；
    // 2. 断开信号，防止运行中的线程把结果回写 UI；
    // 3. 不 wait：线程在取消后 ≤几百 ms 内自然结束，由 finished → deleteLater
    //    自清理（下方已建立该连接），主线程零卡顿（旧实现 wait(3000) 会白等）。
    if (m_service) {
        SNAP_LOG_INFO("MainWindow: 重新翻译 —— 取消旧文本翻译服务，立即断开网络连接");
        m_service->requestCancel();
        disconnect(m_service, nullptr, this, nullptr);
        if (!m_service->isRunning()) {
            m_service->deleteLater();
        }
        m_service = nullptr;
    }

    m_targetEdit->clear();
    m_thinkEdit->clear();
    m_translateBtn->setEnabled(false);
    m_translateBtn->setText("翻译中...");

    // Use prompt from prompt tab (user may have edited it)
    QString fullPrompt = m_promptEdit->toPlainText().trimmed();

    m_service = new TextTranslateService(
        sourceText,
        m_targetLangCombo->currentText(),
        m_scenarioCombo->currentText(),
        m_settings->text_translation_prompt,
        fullPrompt,
        m_settings->ai_api_key,
        m_settings->ai_api_base,
        m_settings->ai_model,
        m_settings->ai_timeout,
        m_settings->ai_temperature,
        m_settings->ai_max_tokens,
        m_settings->ai_top_p,
        m_settings->ai_frequency_penalty,
        m_settings->ai_presence_penalty,
        m_settings->ai_seed,
        m_settings->ai_stream_thinking);

    connect(m_service, &TextTranslateService::translated, this, &MainWindow::onTranslated);
    connect(m_service, &TextTranslateService::thinking, this, &MainWindow::onThinking);
    connect(m_service, &TextTranslateService::error, this, &MainWindow::onError);
    connect(m_service, &QThread::finished, m_service, &QObject::deleteLater);
    m_service->start();
}

void MainWindow::onTranslated(const QString& text)
{
    m_targetEdit->setPlainText(text);
    m_translateBtn->setEnabled(true);
    m_translateBtn->setText("翻译");
    if (m_notifyManager) {
        m_notifyManager->notify("translate_success", "文本翻译", "翻译完成");
    }
}

void MainWindow::onThinking(const QString& text)
{
    m_thinkEdit->setPlainText(text);
}

void MainWindow::onError(const QString& msg)
{
    m_targetEdit->setPlainText(QString("[错误] %1").arg(msg));
    m_translateBtn->setEnabled(true);
    m_translateBtn->setText("翻译");
    if (m_notifyManager) {
        m_notifyManager->notify("translate_fail", "翻译失败", msg);
    }
}

void MainWindow::toggleLayout()
{
    Qt::Orientation orient = m_splitter->orientation();
    if (orient == Qt::Horizontal) {
        m_splitter->setOrientation(Qt::Vertical);
        m_leftPanel->splitter->setOrientation(Qt::Horizontal);
        m_rightPanel->splitter->setOrientation(Qt::Horizontal);
        m_layoutBtn->setText("左右布局");
        if (m_settings) m_settings->layout_orientation = false;
    } else {
        m_splitter->setOrientation(Qt::Horizontal);
        m_leftPanel->splitter->setOrientation(Qt::Vertical);
        m_rightPanel->splitter->setOrientation(Qt::Vertical);
        m_layoutBtn->setText("上下布局");
        if (m_settings) m_settings->layout_orientation = true;
    }
    saveLayoutState();
}

void MainWindow::resetLayout()
{
    m_leftPanel->applyState(true, 0.7);
    m_rightPanel->applyState(false, 0.7);
    m_splitter->setOrientation(Qt::Horizontal);
    m_leftPanel->splitter->setOrientation(Qt::Vertical);
    m_rightPanel->splitter->setOrientation(Qt::Vertical);
    m_layoutBtn->setText("上下布局");
    int total = m_splitter->sizes().value(0) + m_splitter->sizes().value(1);
    if (total <= 0) total = 860;
    m_splitter->setSizes({int(total * 0.5), int(total * 0.5)});
    saveLayoutState();
}

void MainWindow::saveLayoutState()
{
    if (!m_settings) return;
    m_settings->layout_orientation = (m_splitter->orientation() == Qt::Horizontal);
    m_settings->left_panel_merged = m_leftPanel->isMerged();
    m_settings->right_panel_merged = m_rightPanel->isMerged();
    m_settings->left_panel_ratio = m_leftPanel->ratio();
    m_settings->right_panel_ratio = m_rightPanel->ratio();
    auto sizes = m_splitter->sizes();
    int total = sizes.value(0) + sizes.value(1);
    m_settings->main_split_ratio = total > 0 ? double(sizes.value(0)) / total : 0.5;
    m_settings->save();
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    saveLayoutState();

    // 安全停止翻译服务（同 onTranslate 的清理策略）：
    // 关闭窗口 = 用户不再关心本次翻译结果 → requestCancel() 置位取消标志，
    // ApiClient 每 100ms 轮询后立即 abort 断网，避免服务端继续生成造成 token
    // 浪费；不 wait（线程 ≤几百 ms 内自然结束，finished → deleteLater 自清理），
    // 主线程零卡顿。m_service 为 QPointer，窗口删除后也不会访问悬空指针。
    if (m_service) {
        SNAP_LOG_INFO("MainWindow: 窗口关闭 —— 立即取消文本翻译并断开网络连接（省 token）");
        m_service->requestCancel();
        disconnect(m_service, nullptr, this, nullptr);
        if (!m_service->isRunning()) {
            m_service->deleteLater();
        }
        m_service = nullptr;
    }

    // 截图模式：关闭窗口 = 隐藏
    if (m_appMode == "screenshot") {
        hide();
        event->ignore();
        return;
    }

    // 翻译模式：按 close_to_tray 设置决定行为
    QVariant toTray = m_settings ? m_settings->close_to_tray : QVariant();
    if (toTray.isNull()) {
        auto [result, remember] = CloseDialog::ask(this);
        if (!result.isValid()) {
            event->ignore();
            return;
        }
        toTray = result;
        if (remember && m_settings) {
            m_settings->close_to_tray = toTray;
            m_settings->save();
        }
    }

    if (toTray.toBool()) {
        hide();
        event->ignore();
    } else {
        QApplication::quit();
    }
}
