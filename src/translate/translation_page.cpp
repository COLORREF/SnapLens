#include "translation_page.h"
#include "translator.h"
#include "translation_task.h"
#include "providers.h"
#include "../settings.h"
#include "../notify/notify_manager.h"
#include "../log/log_manager.h"
#include "../ui/snapkit.h"

#include <QComboBox>
#include <QPushButton>
#include <QCheckBox>
#include <QPlainTextEdit>
#include <QDialog>
#include <QSplitter>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QLabel>
#include <QCloseEvent>
#include <QApplication>
#include <QShortcut>
#include <QTimer>
#include <QSystemTrayIcon>

using namespace SnapUi;

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

PivotPanel::PivotPanel(const QString& title1, const QString& title2,
                       QPlainTextEdit* edit1, QPlainTextEdit* edit2,
                       bool defaultMerged, QWidget* parent)
    : QWidget(parent)
    , m_edit1(edit1)
    , m_edit2(edit2)
    , m_title1(title1)
    , m_title2(title2)
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    m_splitter = new QSplitter(Qt::Vertical, this);
    m_splitter->setChildrenCollapsible(false);
    splitter = m_splitter;   // 对外暴露同一个分隔条（供上层切换方向）
    root->addWidget(m_splitter);

    auto makeBox = [this](ElaTabWidget*& tabs) {
        auto* box = new Card(m_splitter);
        auto* layout = new QVBoxLayout(box);
        layout->setContentsMargins(6, 4, 6, 6);
        layout->setSpacing(0);
        tabs = new ElaTabWidget(box);
        tabs->setTabSize(QSize(112, 30));
        tabs->setIsTabTransparent(true);
        tabs->setTabsClosable(false);
        layout->addWidget(tabs, 1);
        connect(tabs, &QTabWidget::tabBarDoubleClicked, this, [this](int index) {
            if (index < 0) return;
            rebuild(!m_merged);
            emit stateChanged();
        });
        m_splitter->addWidget(box);
        return box;
    };

    m_boxA = makeBox(m_tabA);
    m_boxB = makeBox(m_tabB);

    connect(m_splitter, &QSplitter::splitterMoved, this, [this](int, int) {
        captureRatio();
        emit stateChanged();
    });

    rebuild(defaultMerged);
}

void PivotPanel::captureRatio()
{
    if (m_rebuilding || m_merged) return;
    const auto sizes = m_splitter->sizes();
    if (sizes.size() < 2) return;
    const int total = sizes.at(0) + sizes.at(1);
    if (total > 0 && sizes.at(0) > 0 && sizes.at(1) > 0)
        m_savedRatio = double(sizes.at(0)) / total;
}

void PivotPanel::rebuild(bool merged)
{
    m_rebuilding = true;
    m_merged = merged;

    // removeTab 不销毁编辑器；随后 addTab 将其迁移到目标面板。
    while (m_tabA->count()) m_tabA->removeTab(0);
    while (m_tabB->count()) m_tabB->removeTab(0);

    if (merged) {
        m_tabA->addTab(m_edit1, m_title1);
        m_tabA->addTab(m_edit2, m_title2);
        m_boxB->setVisible(false);
        // 让上面那块占满全部高度：QSplitter 用 {1, 0} 表达"另一块收起"
        m_splitter->setSizes({1, 0});
    } else {
        m_tabA->addTab(m_edit1, m_title1);
        m_tabB->addTab(m_edit2, m_title2);
        m_boxB->setVisible(true);

        const int total = qMax(200, m_splitter->height() > 0
                                        ? m_splitter->height()
                                        : 560);
        const int first = qBound(60, int(total * m_savedRatio), total - 60);
        m_splitter->setSizes({first, total - first});
    }

    m_tabA->setCurrentIndex(0);
    m_rebuilding = false;
}

void PivotPanel::applyState(bool merged, double ratio)
{
    m_savedRatio = qBound(0.15, ratio, 0.85);
    if (merged != m_merged)
        rebuild(merged);
    else if (!merged) {
        const int total = qMax(200, m_splitter->height() > 0 ? m_splitter->height() : 560);
        const int first = qBound(60, int(total * m_savedRatio), total - 60);
        m_splitter->setSizes({first, total - first});
    }
}

TranslationPage::TranslationPage(Settings* settings, NotifyManager* notify, QWidget* parent)
    : QWidget(parent), m_settings(settings), m_notifyManager(notify)
{
    setWindowTitle("文本翻译");
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(buildTranslatePage());
    refreshPromptPreview();
}

void TranslationPage::prepareForQuit()
{
    saveLayoutState();
    if (m_service) m_service->requestCancel();
}

TranslationPage::~TranslationPage()
{
    releaseTranslationTask(m_service, this);
}

bool TranslationPage::isBusy() const
{
    return m_service && m_service->isRunning();
}

QWidget* TranslationPage::buildTranslatePage()
{
    auto* page = new QWidget(this);
    page->setWindowTitle("文本翻译");
    auto* root = new QVBoxLayout(page);
    root->setContentsMargins(Dim::PageHMargin, 18, Dim::PageHMargin, 16);
    root->setSpacing(14);

    root->addWidget(pageHeader(
        "文本翻译",
        "选择领域与目标语言，粘贴文本后按 Ctrl+Enter 开始翻译。", page));

    // ── 命令区：左侧是翻译参数，右侧是布局与执行 ──
    auto* commandCard = new Card(page);
    auto* commandRoot = new QVBoxLayout(commandCard);
    commandRoot->setContentsMargins(16, 12, 16, 12);
    commandRoot->setSpacing(10);
    auto* command = new QHBoxLayout();
    command->setSpacing(10);
    commandRoot->addLayout(command);

    command->addWidget(detailText("翻译场景", commandCard));
    m_scenarioCombo = new ElaComboBox(commandCard);
    m_scenarioCombo->addItems(TRANSLATION_SCENARIOS);
    m_scenarioCombo->setMinimumWidth(140);
    m_scenarioCombo->setToolTip("决定提示词中的领域语气与术语倾向");
    connect(m_scenarioCombo, &QComboBox::currentTextChanged,
            this, &TranslationPage::refreshPromptPreview);
    command->addWidget(m_scenarioCombo);

    command->addSpacing(8);
    command->addWidget(detailText("目标语言", commandCard));
    m_targetLangCombo = new ElaComboBox(commandCard);
    m_targetLangCombo->addItems(TARGET_LANGUAGES);
    const int engIdx = m_targetLangCombo->findText("英语");
    if (engIdx >= 0) m_targetLangCombo->setCurrentIndex(engIdx);
    m_targetLangCombo->setMinimumWidth(108);
    connect(m_targetLangCombo, &QComboBox::currentTextChanged,
            this, &TranslationPage::refreshPromptPreview);
    command->addWidget(m_targetLangCombo);

    command->addStretch();
    auto* actions = new QHBoxLayout();
    actions->setSpacing(10);
    actions->addStretch();
    commandRoot->addLayout(actions);

    m_layoutBtn = secondaryButton("上下布局", commandCard);
    m_layoutBtn->setToolTip("在「左右分栏」与「上下分栏」之间切换");
    connect(m_layoutBtn, &QPushButton::clicked, this, &TranslationPage::toggleLayout);
    actions->addWidget(m_layoutBtn);

    m_resetLayoutBtn = secondaryButton("还原布局", commandCard);
    m_resetLayoutBtn->setToolTip("恢复默认的分栏比例与页签合并状态");
    connect(m_resetLayoutBtn, &QPushButton::clicked, this, &TranslationPage::resetLayout);
    actions->addWidget(m_resetLayoutBtn);

    m_translateBtn = primaryButton("开始翻译", commandCard);
    m_translateBtn->setToolTip("快捷键 Ctrl+Enter");
    connect(m_translateBtn, &QPushButton::clicked, this, &TranslationPage::onTranslate);
    actions->addWidget(m_translateBtn);
    root->addWidget(commandCard);

    // ── 工作区 ──
    m_splitter = new QSplitter(Qt::Horizontal, page);
    m_splitter->setChildrenCollapsible(false);

    m_sourceEdit = new ElaPlainTextEdit();
    m_sourceEdit->setPlaceholderText("输入或粘贴待翻译文本…");
    connect(m_sourceEdit, &QPlainTextEdit::textChanged,
            this, &TranslationPage::refreshPromptPreview);

    m_promptEdit = new ElaPlainTextEdit();
    m_promptEdit->setPlaceholderText("可在此微调本次使用的提示词…");

    m_leftPanel = new PivotPanel("原文", "提示词", m_sourceEdit, m_promptEdit, true);
    m_splitter->addWidget(m_leftPanel);

    m_targetEdit = new ElaPlainTextEdit();
    m_targetEdit->setReadOnly(true);
    m_targetEdit->setPlaceholderText("翻译结果将显示在此处…");

    m_thinkEdit = new ElaPlainTextEdit();
    m_thinkEdit->setReadOnly(true);
    m_thinkEdit->setPlaceholderText("AI 思考过程将在此处显示…");

    m_rightPanel = new PivotPanel("译文", "AI 思考", m_targetEdit, m_thinkEdit, false);
    m_splitter->addWidget(m_rightPanel);
    connect(m_leftPanel, &PivotPanel::stateChanged, this, &TranslationPage::saveLayoutState);
    connect(m_rightPanel, &PivotPanel::stateChanged, this, &TranslationPage::saveLayoutState);

    // Restore layout state from settings
    if (m_settings) {
        const bool horizontal = m_settings->layout_orientation;
        applyOrientation(horizontal);
        m_leftPanel->applyState(m_settings->left_panel_merged, m_settings->left_panel_ratio);
        m_rightPanel->applyState(m_settings->right_panel_merged, m_settings->right_panel_ratio);
        int total = m_splitter->sizes().value(0) + m_splitter->sizes().value(1);
        if (total <= 0) total = 860;
        const double r = qBound(0.2, m_settings->main_split_ratio, 0.8);
        m_splitter->setSizes({int(total * r), int(total * (1 - r))});
    }

    root->addWidget(m_splitter, 1);

    // ── 状态栏 ──
    auto* bottomBar = new QHBoxLayout();
    bottomBar->setSpacing(10);
    m_statusLabel = caption("就绪", page);
    bottomBar->addWidget(m_statusLabel);
    bottomBar->addStretch();
    m_clearBtn = secondaryButton("清空", page);
    m_clearBtn->setToolTip("清空原文、译文与思考过程");
    connect(m_clearBtn, &QPushButton::clicked, this, &TranslationPage::clearWorkspace);
    bottomBar->addWidget(m_clearBtn);
    m_copyBtn = secondaryButton("复制译文", page);
    m_copyBtn->setEnabled(false);
    connect(m_copyBtn, &QPushButton::clicked, this, &TranslationPage::copyTranslation);
    bottomBar->addWidget(m_copyBtn);
    root->addLayout(bottomBar);

    auto* translateShortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Return), page);
    connect(translateShortcut, &QShortcut::activated, this, &TranslationPage::onTranslate);
    return page;
}

void TranslationPage::applyOrientation(bool horizontal)
{
    if (horizontal) {
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

void TranslationPage::refreshPromptPreview()
{
    if (!m_settings) return;
    QString prompt = m_settings->text_translation_prompt;
    prompt.replace("{target_lang}", m_targetLangCombo->currentText());
    prompt.replace("{scenario}", m_scenarioCombo->currentText());
    prompt.replace("{source_text}", m_sourceEdit->toPlainText());
    m_promptEdit->setPlainText(prompt);
}

void TranslationPage::onTranslate()
{
    QString sourceText = m_sourceEdit->toPlainText().trimmed();
    if (sourceText.isEmpty()) {
        m_statusLabel->setText("请输入需要翻译的文本");
        m_sourceEdit->setFocus();
        ElaMessageBar::information(ElaMessageBarType::TopRight,
                                   "还没有原文", "输入或粘贴文本后再开始翻译。",
                                   2500, this);
        return;
    }

    if (!m_settings || (providerNeedsApiKey(m_settings->ai_provider)
                        && m_settings->ai_api_key.trimmed().isEmpty())
                    || m_settings->ai_api_base.trimmed().isEmpty()
                    || m_settings->ai_model.trimmed().isEmpty()) {
        emit settingsRequested("ai");
        m_statusLabel->setText("需要配置 AI 服务");
        // 提示属于整个主窗口，切换到设置页后仍需保持可见。
        ElaMessageBar::warning(ElaMessageBarType::TopRight,
                               "AI 服务尚未配置", "请先在设置中填写接口地址和模型；预设服务还需要 API Key。",
                               5000, window());
        return;
    }

    releaseTranslationTask(m_service, this);

    m_targetEdit->clear();
    m_thinkEdit->clear();
    m_copyBtn->setEnabled(false);
    m_translateBtn->setEnabled(false);
    m_translateBtn->setText("翻译中…");
    m_statusLabel->setText("正在连接 AI 服务…");

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

    connect(m_service, &TextTranslateService::translated, this, &TranslationPage::onTranslated);
    connect(m_service, &TextTranslateService::thinking, this, &TranslationPage::onThinking);
    connect(m_service, &TextTranslateService::error, this, &TranslationPage::onError);
    connect(m_service, &QThread::finished, m_service, &QObject::deleteLater);
    m_service->start();
}

void TranslationPage::onTranslated(const QString& text)
{
    // disconnect 不能撤销已排队的信号，丢弃旧任务的回写。
    if (!m_service || sender() != m_service.data()) return;
    m_targetEdit->setPlainText(text);
    m_translateBtn->setEnabled(true);
    m_translateBtn->setText("重新翻译");
    m_copyBtn->setEnabled(!text.isEmpty());
    m_statusLabel->setText(QString("翻译完成 · %1").arg(m_targetLangCombo->currentText()));
    if (m_notifyManager) {
        m_notifyManager->notify("translate_success", "文本翻译", "翻译完成");
    }
}

void TranslationPage::onThinking(const QString& text)
{
    // disconnect 不能撤销已排队的信号，丢弃旧任务的回写。
    if (!m_service || sender() != m_service.data()) return;
    m_thinkEdit->setPlainText(text);
    if (!text.isEmpty()) m_statusLabel->setText("正在生成译文…");
}

void TranslationPage::onError(const QString& msg)
{
    // disconnect 不能撤销已排队的信号，丢弃旧任务的回写。
    if (!m_service || sender() != m_service.data()) return;
    m_targetEdit->setPlainText(QString("[错误] %1").arg(msg));
    m_translateBtn->setEnabled(true);
    m_translateBtn->setText("重试");
    m_copyBtn->setEnabled(false);
    m_statusLabel->setText("翻译失败");
    ElaMessageBar::error(ElaMessageBarType::TopRight,
                         "翻译失败", msg, 4200, this);
    if (m_notifyManager) {
        m_notifyManager->notify("translate_fail", "翻译失败", msg);
    }
}

void TranslationPage::copyTranslation()
{
    const QString text = m_targetEdit->toPlainText();
    if (text.isEmpty() || text.startsWith("[错误]")) return;
    QApplication::clipboard()->setText(text);
    m_statusLabel->setText("译文已复制到剪贴板");
    ElaMessageBar::success(ElaMessageBarType::TopRight,
                           "已复制", "译文已放入剪贴板。", 1800, this);
}

void TranslationPage::clearWorkspace()
{
    releaseTranslationTask(m_service, this);
    m_sourceEdit->clear();
    m_targetEdit->clear();
    m_thinkEdit->clear();
    m_copyBtn->setEnabled(false);
    m_translateBtn->setEnabled(true);
    m_translateBtn->setText("开始翻译");
    m_statusLabel->setText("工作区已清空");
    m_sourceEdit->setFocus();
}

void TranslationPage::toggleLayout()
{
    const bool horizontal = (m_splitter->orientation() != Qt::Horizontal);
    applyOrientation(horizontal);
    if (m_settings) m_settings->layout_orientation = horizontal;
    saveLayoutState();
}

void TranslationPage::resetLayout()
{
    applyOrientation(true);
    m_leftPanel->applyState(true, 0.62);
    m_rightPanel->applyState(false, 0.62);
    int total = m_splitter->sizes().value(0) + m_splitter->sizes().value(1);
    if (total <= 0) total = 860;
    m_splitter->setSizes({int(total * 0.5), int(total * 0.5)});
    saveLayoutState();
}

void TranslationPage::saveLayoutState()
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
