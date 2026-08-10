#include "setup_wizard.h"
#include "../settings.h"
#include "../notify/notify_manager.h"
#include "../ocr/ocr_engine.h"
#include "../translate/providers.h"
#include "../translate/ai_client.h"
#include "../log/log_manager.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QCheckBox>
#include <QComboBox>
#include <QKeySequenceEdit>
#include <QLineEdit>
#include <QRadioButton>
#include <QButtonGroup>
#include <QPushButton>
#include <QFileDialog>
#include <QMessageBox>
#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

#include <algorithm>

// ============================================================================
// Helpers
// ============================================================================

static QString defaultSaveDir()
{
    QString pics = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    return QDir::cleanPath(
        QDir(pics.isEmpty() ? QDir::homePath() : pics).filePath("SnapLens"));
}

// ============================================================================
// WelcomePage
// ============================================================================

WelcomePage::WelcomePage(QWidget* parent) : QWizardPage(parent)
{
    setTitle("欢迎使用 SnapLens");
    setSubTitle("首次运行需要完成基本设置，只需一分钟。");

    auto* layout = new QVBoxLayout(this);
    auto* intro = new QLabel(
        "SnapLens 是一个轻量级的截图工具，支持：\n\n"
        "  - 截图 / 钉图 / 取色\n"
        "  - OCR 文字识别\n"
        "  - AI 翻译（需配置 API Key）\n\n"
        "点击\"下一步\"选择使用模式。"
    );
    intro->setWordWrap(true);
    layout->addWidget(intro);
    layout->addStretch();
}

// ============================================================================
// ModePage
// ============================================================================

ModePage::ModePage(QWidget* parent) : QWizardPage(parent)
{
    setTitle("选择使用模式");
    setSubTitle("选择默认的使用方式，后续可在设置或托盘菜单中切换。");

    auto* layout = new QVBoxLayout(this);
    m_modeGroup = new QButtonGroup(this);

    // ── 翻译模式 ──
    auto* group1 = new QGroupBox();
    auto* g1 = new QVBoxLayout(group1);

    m_translateRadio = new QRadioButton("翻译模式");
    m_translateRadio->setChecked(true);
    m_modeGroup->addButton(m_translateRadio);

    auto* transDesc = new QLabel(
        "启动后直接显示文本翻译窗口，方便随时进行翻译。\n"
        "截图功能通过快捷键触发，不影响翻译使用。\n"
        "适用于以翻译为主要使用场景的用户。"
    );
    transDesc->setWordWrap(true);
    transDesc->setStyleSheet("color: gray; margin-left: 20px;");

    g1->addWidget(m_translateRadio);
    g1->addWidget(transDesc);
    layout->addWidget(group1);

    // ── 截图模式 ──
    auto* group2 = new QGroupBox();
    auto* g2 = new QVBoxLayout(group2);

    m_screenshotRadio = new QRadioButton("截图模式");
    m_modeGroup->addButton(m_screenshotRadio);

    auto* shotDesc = new QLabel(
        "启动后静默在后台运行，仅显示托盘图标。\n"
        "截图通过快捷键触发，翻译功能可通过托盘菜单或截图工具条打开。\n"
        "适用于以截图为主要使用场景、追求简洁体验的用户。"
    );
    shotDesc->setWordWrap(true);
    shotDesc->setStyleSheet("color: gray; margin-left: 20px;");

    g2->addWidget(m_screenshotRadio);
    g2->addWidget(shotDesc);
    layout->addWidget(group2);
    layout->addStretch();
}

QString ModePage::getMode() const
{
    return m_screenshotRadio->isChecked() ? "screenshot" : "translate";
}

// ============================================================================
// HotkeyPage
// ============================================================================

HotkeyPage::HotkeyPage(QWidget* parent) : QWizardPage(parent)
{
    setTitle("截图快捷键");
    setSubTitle("设置触发截图的全局快捷键。");

    auto* f = new QFormLayout(this);
    m_hotkeyEdit = new QKeySequenceEdit(QKeySequence("Ctrl+Shift+Z"));
    f->addRow("快捷键：", m_hotkeyEdit);

    auto* hint = new QLabel("默认 Ctrl+Shift+Z，可按需修改。");
    f->addRow(hint);
}

QString HotkeyPage::getHotkey() const
{
    return m_hotkeyEdit->keySequence().toString(QKeySequence::PortableText);
}

// ============================================================================
// AiTranslationPage
// ============================================================================

AiTranslationPage::AiTranslationPage(QWidget* parent) : QWizardPage(parent)
{
    setTitle("AI 翻译设置");
    setSubTitle("配置 AI 翻译服务。选择服务商并填入对应的 API Key 即可使用。");

    auto* f = new QFormLayout(this);

    // ── 服务商 ──
    m_providerCombo = new QComboBox();
    const auto& configs = PROVIDER_CONFIGS;
    for (const auto& pid : configs.keys()) {
        m_providerCombo->addItem(configs[pid].label, pid);
    }
    connect(m_providerCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &AiTranslationPage::onProviderChanged);
    f->addRow("服务商：", m_providerCombo);

    m_currentProvider = m_providerCombo->currentData().toString();

    // ── API Key ──
    m_apiKeyEdit = new QLineEdit();
    m_apiKeyEdit->setPlaceholderText("请输入 API Key（sk-...）");
    f->addRow("API Key：", m_apiKeyEdit);

    // ── API 地址 ──
    m_apiBaseEdit = new QLineEdit();
    // Trigger initial provider setup
    onProviderChanged(0);
    f->addRow("API 地址：", m_apiBaseEdit);

    // ── 模型 ──
    auto* modelRow = new QHBoxLayout();
    m_modelCombo = new QComboBox();
    m_modelCombo->setEditable(true);
    m_modelCombo->setPlaceholderText("输入模型名称或点击右侧按钮自动获取");
    modelRow->addWidget(m_modelCombo);

    m_fetchModelsBtn = new QPushButton("获取模型列表");
    m_fetchModelsBtn->setFixedWidth(100);
    m_fetchModelsBtn->setFocusPolicy(Qt::NoFocus);
    connect(m_fetchModelsBtn, &QPushButton::clicked, this, &AiTranslationPage::onFetchModels);
    modelRow->addWidget(m_fetchModelsBtn);
    f->addRow("模型：", modelRow);

    // ── 目标语言 ──
    m_targetLangCombo = new QComboBox();
    m_targetLangCombo->setEditable(true);
    m_targetLangCombo->addItems({
        "简体中文", "繁体中文（台湾）", "繁体中文（香港）",
        "英语", "日语", "韩语", "法语", "德语",
        "西班牙语", "葡萄牙语", "俄语", "阿拉伯语",
        "泰语", "越南语",
    });
    m_targetLangCombo->setCurrentText("简体中文");
    f->addRow("默认目标语言：", m_targetLangCombo);

    // ── 错误提示 ──
    m_errorLabel = new QLabel();
    m_errorLabel->setWordWrap(true);
    m_errorLabel->setVisible(false);
    m_errorLabel->setStyleSheet("color: red;");
    f->addRow(m_errorLabel);
}

AiTranslationPage::~AiTranslationPage() = default;  // unique_ptr<ApiClient> 在此处析构（完整类型）

void AiTranslationPage::onProviderChanged(int index)
{
    Q_UNUSED(index)
    QString newPid = m_providerCombo->currentData().toString();
    auto cfg = getProviderConfig(newPid);

    // Save current provider's key
    if (!m_currentProvider.isEmpty() && m_currentProvider != newPid) {
        m_providerKeys[m_currentProvider] = m_apiKeyEdit->text().trimmed();
    }

    // Update API base
    m_apiBaseEdit->setText(cfg.apiBase);

    // Restore saved key for new provider
    if (newPid != m_currentProvider) {
        m_apiKeyEdit->setText(m_providerKeys.value(newPid));
    }

    m_currentProvider = newPid;
}

void AiTranslationPage::onFetchModels()
{
    QString apiKey  = m_apiKeyEdit->text().trimmed();
    QString apiBase = m_apiBaseEdit->text().trimmed();
    m_errorLabel->setVisible(false);

    if (apiKey.isEmpty()) {
        m_errorLabel->setText("请先填写 API Key 后再获取模型列表。");
        m_errorLabel->setVisible(true);
        m_apiKeyEdit->setFocus();
        return;
    }
    if (apiBase.isEmpty()) {
        m_errorLabel->setText("请先填写 API 地址后再获取模型列表。");
        m_errorLabel->setVisible(true);
        m_apiBaseEdit->setFocus();
        return;
    }

    m_fetchModelsBtn->setEnabled(false);
    m_fetchModelsBtn->setText("获取中…");
    m_modelCombo->clear();
    const int gen = ++m_fetchGen;

    // L11 异步化：发起请求后立即返回，向导 UI 不阻塞；结果通过回调回填。
    // 回调先恢复按钮，再校验代次与页面可见性（向导关闭/跳页时丢弃结果）。
    if (!m_aiClient) m_aiClient = std::make_unique<ApiClient>();
    m_aiClient->listModelsAsync(apiKey, apiBase, 10,
        [this, gen, apiBase](const ApiClient::ModelsResult& result) {
            // 无论结果是否被采用，先恢复按钮，保证 UI 状态一致
            m_fetchModelsBtn->setEnabled(true);
            m_fetchModelsBtn->setText("获取模型列表");

            // 过期结果丢弃：期间用户已再次发起获取
            if (gen != m_fetchGen) {
                SNAP_LOG_DEBUG("AiTranslationPage: 丢弃过期的模型列表结果（代次不匹配）");
                return;
            }
            // 页面不可见（向导关闭/已跳转到其它页）：不再回填
            if (!isVisible()) {
                SNAP_LOG_DEBUG("AiTranslationPage: 页面不可见，丢弃模型列表结果");
                return;
            }

            if (result.errorCode == 0 && !result.modelIds.isEmpty()) {
                m_modelCombo->addItems(result.modelIds);
                m_modelCombo->setCurrentIndex(0);
            } else if (result.errorCode == -2) {
                m_modelCombo->addItem("（请求超时）");
                m_errorLabel->setText(
                    QString("请求超时，请稍后重试。\n地址：%1").arg(apiBase));
                m_errorLabel->setVisible(true);
            } else if (result.errorCode == -1) {
                QString msg = result.errorMsg;
                QString hint;
                if (msg.contains("鉴权") || msg.contains("401") || msg.contains("403"))
                    hint = "API Key 鉴权失败，请确认 Key 是否正确";
                else if (msg.contains("额度") || msg.contains("429"))
                    hint = "API 额度不足或受到频率限制";
                else
                    hint = "该服务商可能不支持自动获取模型列表，请手动输入模型名称";
                m_modelCombo->addItem("（获取失败）");
                m_errorLabel->setText(
                    QString("%1\n服务商：%2\n地址：%3\n错误：%4")
                        .arg(hint, m_providerCombo->currentText(), apiBase, msg));
                m_errorLabel->setVisible(true);
            } else {
                m_modelCombo->addItem("（服务商未返回模型列表）");
            }
        });
}

QString AiTranslationPage::getProvider()   const { return m_providerCombo->currentData().toString(); }
QString AiTranslationPage::getApiKey()     const { return m_apiKeyEdit->text().trimmed(); }
QString AiTranslationPage::getApiBase()    const { return m_apiBaseEdit->text().trimmed(); }
QString AiTranslationPage::getModel()      const { return m_modelCombo->currentText().trimmed(); }
QString AiTranslationPage::getTargetLang() const { return m_targetLangCombo->currentText(); }

// ============================================================================
// OcrPage
// ============================================================================

const QVector<OcrPage::LangEntry> OcrPage::LANG_MAP = {
    {"chi_sim", "中文简体"}, {"chi_tra", "中文繁体"},
    {"eng",     "英文"},     {"jpn",     "日语"},
    {"kor",     "韩语"},     {"fra",     "法语"},
    {"deu",     "德语"},     {"spa",     "西班牙语"},
    {"rus",     "俄语"},
};

QSet<QString> OcrPage::scanInstalled(const QString& tessdataDir)
{
    QSet<QString> installed;
    QDir dir(tessdataDir);
    if (!dir.exists()) return installed;
    for (const auto& fi : dir.entryInfoList({"*.traineddata"}, QDir::Files)) {
        QString code = fi.completeBaseName();
        if (code != "osd") installed.insert(code);
    }
    return installed;
}

OcrPage::OcrPage(QWidget* parent) : QWizardPage(parent)
{
    setTitle("OCR 文字识别");
    setSubTitle("选择 OCR 识别支持的语言。");

    auto* layout = new QVBoxLayout(this);

    // Scan installed languages
    QString tessdata = OcrEngine::findTessdataDir();
    QSet<QString> installed = scanInstalled(tessdata);

    layout->addWidget(new QLabel("勾选需要识别的语言："));

    for (const auto& entry : LANG_MAP) {
        bool isDefault = (entry.code == "chi_sim" || entry.code == "eng");
        auto* cb = new QCheckBox(QString("%1  (%2)").arg(entry.name, entry.code));
        cb->setChecked(isDefault && installed.contains(entry.code));
        cb->setEnabled(installed.contains(entry.code));
        layout->addWidget(cb);
        m_checks[entry.code] = cb;
    }

    // Show missing languages
    QStringList missing;
    for (const auto& entry : LANG_MAP) {
        if (!installed.contains(entry.code))
            missing.append(entry.name);
    }
    if (!missing.isEmpty()) {
        auto* missingLabel = new QLabel(
            QString("未安装的语言包（可在\"设置 > OCR 识别\"中下载）：%1")
                .arg(missing.join(", ")));
        missingLabel->setWordWrap(true);
        layout->addWidget(missingLabel);
    }

    if (installed.isEmpty()) {
        auto* hint = new QLabel(
            "语言包下载后自动保存到 sdk/tesseract/tessdata/。\n"
            "语言包目录可在后续 \"设置 > OCR 识别\" 中手动指定。"
        );
        hint->setWordWrap(true);
        layout->addWidget(hint);
    }

    layout->addStretch();
}

QString OcrPage::getSelectedLangs() const
{
    QStringList selected;
    for (auto it = m_checks.begin(); it != m_checks.end(); ++it) {
        if (it.value()->isChecked())
            selected.append(it.key());
    }
    return selected.isEmpty() ? "eng" : selected.join("+");
}

// ============================================================================
// SavePage
// ============================================================================

SavePage::SavePage(QWidget* parent) : QWizardPage(parent)
{
    setTitle("截图保存");
    setSubTitle("设置截图文件的默认保存位置和格式。");

    auto* f = new QFormLayout(this);

    // 保存目录
    auto* dirRow = new QHBoxLayout();
    m_saveDirEdit = new QLineEdit(defaultSaveDir());
    auto* browseBtn = new QPushButton("浏览…");
    connect(browseBtn, &QPushButton::clicked, this, &SavePage::browse);
    dirRow->addWidget(m_saveDirEdit);
    dirRow->addWidget(browseBtn);
    f->addRow("保存目录：", dirRow);

    // 保存格式
    m_formatCombo = new QComboBox();
    m_formatCombo->addItems({"png", "jpg", "bmp"});
    f->addRow("保存格式：", m_formatCombo);
}

void SavePage::browse()
{
    QString dir = QFileDialog::getExistingDirectory(this, "选择截图保存目录",
                                                     m_saveDirEdit->text());
    if (!dir.isEmpty()) m_saveDirEdit->setText(dir);
}

QString SavePage::getSaveDir() const
{
    QString d = m_saveDirEdit->text().trimmed();
    return d.isEmpty() ? defaultSaveDir() : d;
}

QString SavePage::getFormat() const { return m_formatCombo->currentText(); }

// ============================================================================
// FinishPage
// ============================================================================

FinishPage::FinishPage(QWidget* parent) : QWizardPage(parent)
{
    setTitle("设置完成");
    setSubTitle("以下是您的配置汇总，点击\"完成\"保存并启动。");

    auto* layout = new QVBoxLayout(this);
    m_summary = new QLabel();
    m_summary->setWordWrap(true);
    layout->addWidget(m_summary);
    layout->addStretch();
}

void FinishPage::setSummary(const QString& text) { m_summary->setText(text); }

// ============================================================================
// SetupWizard
// ============================================================================

SetupWizard::SetupWizard(NotifyManager* notifyManager, QWidget* parent)
    : QWizard(parent)
    , m_notifyManager(notifyManager)
{
    setWindowTitle("SnapLens 首次设置");
    setMinimumSize(520, 420);
    setWizardStyle(QWizard::ClassicStyle);

    m_pageWelcome = new WelcomePage();
    m_pageMode    = new ModePage();
    m_pageHotkey  = new HotkeyPage();
    m_pageAi      = new AiTranslationPage();
    m_pageOcr     = new OcrPage();
    m_pageSave    = new SavePage();
    m_pageFinish  = new FinishPage();

    addPage(m_pageWelcome);
    addPage(m_pageMode);
    addPage(m_pageHotkey);
    addPage(m_pageAi);
    addPage(m_pageOcr);
    addPage(m_pageSave);
    addPage(m_pageFinish);

    connect(this, &QWizard::currentIdChanged, this, &SetupWizard::onPageChanged);
}

void SetupWizard::onPageChanged(int pageId)
{
    QWizardPage* current = page(pageId);

    // Hide cancel on finish page
    if (current == m_pageFinish) {
        button(QWizard::CancelButton)->hide();

        // Build summary
        QString summary = QString(
            "<b>使用模式：</b>%1<br>"
            "<b>截图快捷键：</b>%2<br>"
            "<b>AI 服务商：</b>%3<br>"
            "<b>AI 模型：</b>%4<br>"
            "<b>目标语言：</b>%5<br>"
            "<b>OCR 语言：</b>%6<br>"
            "<b>保存目录：</b>%7<br>"
            "<b>保存格式：</b>%8"
        ).arg(m_pageMode->getMode() == "translate" ? "翻译模式" : "截图模式",
              m_pageHotkey->getHotkey(),
              m_pageAi->getProvider(),
              m_pageAi->getModel().isEmpty() ? "（默认）" : m_pageAi->getModel(),
              m_pageAi->getTargetLang(),
              m_pageOcr->getSelectedLangs(),
              m_pageSave->getSaveDir(),
              m_pageSave->getFormat());
        m_pageFinish->setSummary(summary);
    } else {
        button(QWizard::CancelButton)->show();
    }
}

void SetupWizard::reject()
{
    bool confirmed = false;
    if (m_notifyManager) {
        confirmed = m_notifyManager->confirm(
            "确认取消",
            "取消引导设置将无法进入程序，确定要退出吗？",
            this);
    } else {
        auto reply = QMessageBox::question(
            this, "确认取消",
            "取消引导设置将无法进入程序，确定要退出吗？",
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        confirmed = (reply == QMessageBox::Yes);
    }
    if (confirmed) {
        QWizard::reject();
    }
}

Settings SetupWizard::buildSettings() const
{
    QJsonObject defaults = Settings::defaultsDict();
    Settings s(defaults);

    s.app_mode   = m_pageMode->getMode();
    s.hotkey     = m_pageHotkey->getHotkey();
    s.save_dir   = m_pageSave->getSaveDir();
    s.save_format = m_pageSave->getFormat();

    s.ai_provider = m_pageAi->getProvider();
    if (!m_pageAi->getApiKey().isEmpty())
        s.ai_api_key = m_pageAi->getApiKey();
    if (!m_pageAi->getApiBase().isEmpty())
        s.ai_api_base = m_pageAi->getApiBase();
    if (!m_pageAi->getModel().isEmpty())
        s.ai_model = m_pageAi->getModel();
    s.ai_target_lang = m_pageAi->getTargetLang();

    s.ai_ocr_langs = m_pageOcr->getSelectedLangs();

    return s;
}
