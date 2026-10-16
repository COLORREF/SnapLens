#include "setup_wizard.h"
#include "snapkit.h"
#include "../settings.h"
#include "../notify/notify_manager.h"
#include "../ocr/ocr_engine.h"
#include "../translate/providers.h"
#include "../translate/ai_client.h"
#include "../log/log_manager.h"

#include <QButtonGroup>
#include <QComboBox>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QVBoxLayout>
#include <QDir>

#include <algorithm>

using namespace SnapUi;

// ============================================================================
// Helpers
// ============================================================================

namespace {

QString defaultSaveDir()
{
    QString pics = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    return QDir::cleanPath(
        QDir(pics.isEmpty() ? QDir::homePath() : pics).filePath("SnapLens"));
}

/// QKeySequenceEdit 无法跟随 ElaTheme，套一层 Fluent 外壳；out 返回内层控件。
QWidget* makeHotkeyField(const QString& sequence, QKeySequenceEdit** out, QWidget* parent)
{
    auto* edit = new QKeySequenceEdit(QKeySequence(sequence));
    edit->setClearButtonEnabled(true);
    if (out) *out = edit;
    auto* host = inputShell(edit, parent);
    host->setMinimumWidth(240);
    return host;
}

QColor errorColor()
{
    return eTheme->getThemeMode() == ElaThemeType::Dark
               ? QColor(QStringLiteral("#FF99A4"))
               : QColor(QStringLiteral("#C42B1C"));
}

} // namespace

// ============================================================================
// SetupStep
// ============================================================================

SetupStep::SetupStep(const QString& title, const QString& subtitle,
                     const QString& intro, QWidget* parent)
    : QWidget(parent)
{
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    // 步骤内容自身可滚动，避免长内容（如 AI 参数）被裁切
    auto* scroll = new ElaScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setIsGrabGesture(true, 0);
    scroll->setIsOverShoot(Qt::Vertical, true);

    auto* content = new QWidget();
    content->setObjectName(QStringLiteral("SnapWizardStepContent"));
    content->setStyleSheet(QStringLiteral(
        "QWidget#SnapWizardStepContent { background: transparent; }"));

    m_body = new QVBoxLayout(content);
    m_body->setContentsMargins(Dim::PageHMargin, 4, Dim::PageHMargin, 24);
    m_body->setSpacing(Dim::RowGap);

    m_body->addWidget(pageHeader(title, subtitle, content));
    if (!intro.isEmpty()) {
        m_body->addSpacing(4);
        m_body->addWidget(detailText(intro, content));
    }
    m_body->addSpacing(10);

    scroll->setWidget(content);
    outer->addWidget(scroll);
}

// ============================================================================
// WelcomeStep
// ============================================================================

WelcomeStep::WelcomeStep(QWidget* parent)
    : SetupStep("欢迎使用 SnapLens", "首次运行需要完成基本设置，大约一分钟。", {}, parent)
{
    auto* introCard = new Card(this);
    auto* introLayout = new QVBoxLayout(introCard);
    introLayout->setContentsMargins(24, 20, 24, 20);
    introLayout->setSpacing(10);
    introLayout->addWidget(iconGlyph(ElaIconType::CameraViewfinder, 28, introCard));
    introLayout->addWidget(sectionHeader("截图 · 识别 · 翻译，一个窗口搞定", introCard));
    for (const auto& line : QStringList{
             "截图  —  全局热键唤出，支持窗口点选与像素放大镜",
             "识别  —  进程内 OCR，中英日韩等多语言可选",
             "翻译  —  接入 OpenAI 兼容服务，截图即可翻译",
         }) {
        introLayout->addWidget(detailText(line, introCard));
    }
    body()->addWidget(introCard);
    body()->addStretch();
}

// ============================================================================
// ModeStep
// ============================================================================

ModeStep::ModeStep(QWidget* parent)
    : SetupStep("选择使用模式", "选择默认的使用方式，后续可在设置或托盘菜单中切换。", {}, parent)
{
    auto* group = new QButtonGroup(this);

    auto addOption = [this, group](ElaRadioButton*& radio, const QString& title,
                                   const QString& description, bool checked) {
        auto* card = new Card(this);
        auto* column = new QVBoxLayout(card);
        column->setContentsMargins(18, 14, 18, 14);
        column->setSpacing(6);

        radio = new ElaRadioButton(title, card);
        radio->setChecked(checked);
        group->addButton(radio);
        column->addWidget(radio);
        column->addWidget(detailText(description, card));

        body()->addWidget(card);
    };

    addOption(m_translateRadio, "翻译模式",
              "启动后直接显示文本翻译窗口；截图功能仍可用快捷键随时唤出。", true);
    addOption(m_screenshotRadio, "截图模式",
              "启动后静默驻留托盘，界面保持干净，需要时再从托盘打开。", false);

    body()->addStretch();
}

QString ModeStep::getMode() const
{
    return m_screenshotRadio->isChecked() ? "screenshot" : "translate";
}

// ============================================================================
// HotkeyStep
// ============================================================================

HotkeyStep::HotkeyStep(QWidget* parent)
    : SetupStep("截图快捷键", "设置触发截图的全局快捷键。",
                "该快捷键在任意程序前台时都有效，建议使用不易被占用的组合。", parent)
{
    body()->addWidget(row("快捷键", "点击输入框后直接按下想要的组合键",
                          makeHotkeyField("Ctrl+Shift+Z", &m_hotkeyEdit, this), this));
    body()->addWidget(detailText("默认 Ctrl+Shift+Z，可按需修改。", this));
    body()->addStretch();
}

QString HotkeyStep::getHotkey() const
{
    return m_hotkeyEdit->keySequence().toString(QKeySequence::PortableText);
}

// ============================================================================
// AiStep
// ============================================================================

AiStep::AiStep(QWidget* parent)
    : SetupStep("AI 翻译设置",
                "选择服务商并填入 API Key 即可开始翻译；稍后也可在设置中修改。", {}, parent)
{
    body()->setSpacing(Dim::RowGap);

    // ── 服务商 ──
    auto* providerCard = new SettingsCard("服务商", {}, this);
    m_providerCombo = new ElaComboBox(providerCard);
    const auto& configs = PROVIDER_CONFIGS;
    for (const auto& pid : configs.keys())
        m_providerCombo->addItem(configs[pid].label, pid);
    m_providerCombo->setMinimumWidth(190);
    providerCard->addControl(m_providerCombo);
    connect(m_providerCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &AiStep::onProviderChanged);
    body()->addWidget(providerCard);

    m_currentProvider = m_providerCombo->currentData().toString();

    // ── API Key ──
    auto* keyCard = new SettingsCard("API Key", "仅保存在本机配置文件中", this);
    keyCard->setControlStretch(1);
    m_apiKeyEdit = new ElaLineEdit(keyCard);
    m_apiKeyEdit->setPlaceholderText("sk-...");
    m_apiKeyEdit->setEchoMode(QLineEdit::PasswordEchoOnEdit);
    m_apiKeyEdit->setMinimumWidth(240);
    keyCard->addControl(m_apiKeyEdit);
    body()->addWidget(keyCard);

    // ── API 地址 ──
    auto* baseCard = new SettingsCard("API 地址", {}, this);
    baseCard->setControlStretch(1);
    m_apiBaseEdit = new ElaLineEdit(baseCard);
    onProviderChanged(0);
    m_apiBaseEdit->setMinimumWidth(240);
    baseCard->addControl(m_apiBaseEdit);
    body()->addWidget(baseCard);

    // ── 模型 ──
    auto* modelCard = new SettingsCard("模型", "可直接输入，也可自动获取可用列表", this);
    modelCard->setControlStretch(1);
    auto* modelHost = new QWidget(modelCard);
    auto* modelRow = new QHBoxLayout(modelHost);
    modelRow->setContentsMargins(0, 0, 0, 0);
    modelRow->setSpacing(8);
    m_modelCombo = new ElaComboBox(modelHost);
    m_modelCombo->setEditable(true);
    m_modelCombo->setPlaceholderText("输入模型名称或点击右侧按钮自动获取");
    m_modelCombo->setMinimumWidth(200);
    modelRow->addWidget(m_modelCombo, 1);
    m_fetchModelsBtn = secondaryButton("获取模型列表", modelHost);
    m_fetchModelsBtn->setMinimumWidth(120);
    m_fetchModelsBtn->setFocusPolicy(Qt::NoFocus);
    connect(m_fetchModelsBtn, &QPushButton::clicked, this, &AiStep::onFetchModels);
    modelRow->addWidget(m_fetchModelsBtn);
    modelCard->addControl(modelHost);
    body()->addWidget(modelCard);

    // ── 目标语言 ──
    auto* langCard = new SettingsCard("默认目标语言", {}, this);
    m_targetLangCombo = new ElaComboBox(langCard);
    m_targetLangCombo->setEditable(true);
    m_targetLangCombo->addItems({
        "简体中文", "繁体中文（台湾）", "繁体中文（香港）",
        "英语", "日语", "韩语", "法语", "德语",
        "西班牙语", "葡萄牙语", "俄语", "阿拉伯语",
        "泰语", "越南语",
    });
    m_targetLangCombo->setCurrentText("简体中文");
    m_targetLangCombo->setMinimumWidth(190);
    langCard->addControl(m_targetLangCombo);
    body()->addWidget(langCard);

    // ── 错误提示 ──
    m_errorRow = new QWidget(this);
    auto* errLayout = new QHBoxLayout(m_errorRow);
    errLayout->setContentsMargins(12, 8, 12, 8);
    m_errorLabel = new ElaText(m_errorRow);
    m_errorLabel->setTextPixelSize(12);
    m_errorLabel->setIsWrapAnywhere(true);
    bindTheme(m_errorLabel, [this] {
        m_errorLabel->setStyleSheet(
            QStringLiteral("background: transparent; color: %1;")
                .arg(errorColor().name(QColor::HexArgb)));
    });
    errLayout->addWidget(m_errorLabel);
    m_errorRow->setVisible(false);
    body()->addWidget(m_errorRow);

    body()->addStretch();
}

AiStep::~AiStep() = default;  // unique_ptr<ApiClient> 在此处析构（完整类型）

void AiStep::onProviderChanged(int index)
{
    Q_UNUSED(index)
    QString newPid = m_providerCombo->currentData().toString();
    auto cfg = getProviderConfig(newPid);

    if (!m_currentProvider.isEmpty() && m_currentProvider != newPid) {
        m_providerKeys[m_currentProvider] = m_apiKeyEdit->text().trimmed();
    }
    m_apiBaseEdit->setText(cfg.apiBase);
    if (newPid != m_currentProvider) {
        m_apiKeyEdit->setText(m_providerKeys.value(newPid));
    }
    m_currentProvider = newPid;
}

void AiStep::onFetchModels()
{
    QString apiKey  = m_apiKeyEdit->text().trimmed();
    QString apiBase = m_apiBaseEdit->text().trimmed();
    m_errorRow->setVisible(false);

    if (apiKey.isEmpty() && providerNeedsApiKey(m_providerCombo->currentData().toString())) {
        m_errorLabel->setText("请先填写 API Key 后再获取模型列表。");
        m_errorRow->setVisible(true);
        m_apiKeyEdit->setFocus();
        return;
    }
    if (apiBase.isEmpty()) {
        m_errorLabel->setText("请先填写 API 地址后再获取模型列表。");
        m_errorRow->setVisible(true);
        m_apiBaseEdit->setFocus();
        return;
    }

    m_fetchModelsBtn->setEnabled(false);
    m_fetchModelsBtn->setText("获取中…");
    m_modelCombo->clear();
    const int gen = ++m_fetchGen;

    // 异步拉取：发起后立即返回，UI 不阻塞；回调先恢复按钮，再校验代次与可见性。
    if (!m_aiClient) m_aiClient = std::make_unique<ApiClient>();
    m_aiClient->listModelsAsync(apiKey, apiBase, 10,
        [this, gen, apiBase](const ApiClient::ModelsResult& result) {
            m_fetchModelsBtn->setEnabled(true);
            m_fetchModelsBtn->setText("获取模型列表");

            if (gen != m_fetchGen) {
                SNAP_LOG_DEBUG("AiStep: 丢弃过期的模型列表结果（代次不匹配）");
                return;
            }
            if (!isVisible()) {
                SNAP_LOG_DEBUG("AiStep: 页面不可见，丢弃模型列表结果");
                return;
            }

            if (result.errorCode == 0 && !result.modelIds.isEmpty()) {
                m_modelCombo->addItems(result.modelIds);
                m_modelCombo->setCurrentIndex(0);
            } else if (result.errorCode == -2) {
                m_modelCombo->addItem("（请求超时）");
                m_errorLabel->setText(
                    QString("请求超时，请稍后重试。\n地址：%1").arg(apiBase));
                m_errorRow->setVisible(true);
            } else {
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
                m_errorRow->setVisible(true);
            }
        });
}

QString AiStep::getProvider()   const { return m_providerCombo->currentData().toString(); }
QString AiStep::getApiKey()     const { return m_apiKeyEdit->text().trimmed(); }
QString AiStep::getApiBase()    const { return m_apiBaseEdit->text().trimmed(); }
QString AiStep::getModel()      const { return m_modelCombo->currentText().trimmed(); }
QString AiStep::getTargetLang() const { return m_targetLangCombo->currentText(); }

// ============================================================================
// OcrStep
// ============================================================================

const QVector<OcrStep::LangEntry> OcrStep::LANG_MAP = {
    {"chi_sim", "中文简体"}, {"chi_tra", "中文繁体"},
    {"eng",     "英文"},     {"jpn",     "日语"},
    {"kor",     "韩语"},     {"fra",     "法语"},
    {"deu",     "德语"},     {"spa",     "西班牙语"},
    {"rus",     "俄语"},
};

QSet<QString> OcrStep::scanInstalled(const QString& tessdataDir)
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

OcrStep::OcrStep(QWidget* parent)
    : SetupStep("OCR 文字识别",
                "选择 OCR 需要支持的语言；未安装的语言可稍后在设置中下载。", {}, parent)
{
    const QString tessdata = OcrEngine::findTessdataDir();
    const QSet<QString> installed = scanInstalled(tessdata);

    auto* panel = new PanelCard("识别语言",
                                "勾选的语言会组合使用，语言越多识别越慢。", this);
    QStringList missing;
    for (const auto& entry : LANG_MAP) {
        const bool has = installed.contains(entry.code);
        if (!has) {
            // 未安装的语言不渲染成禁用的复选框：禁用态下 ElaCheckBox 的
            // 指示器在浅色背景上几乎不可见，看起来像控件丢失。
            missing.append(entry.name);
            panel->addContentWidget(detailText(
                QString("%1（%2） · 未安装").arg(entry.name, entry.code), panel));
            continue;
        }
        auto* cb = new ElaCheckBox(
            QString("%1  (%2)").arg(entry.name, entry.code), panel);
        const QString code = QString::fromUtf8(entry.code);
        cb->setChecked(code == QLatin1String("chi_sim") || code == QLatin1String("eng"));
        panel->addContentWidget(cb);
        m_checks[entry.code] = cb;
    }
    body()->addWidget(panel);

    if (!missing.isEmpty()) {
        body()->addWidget(detailText(
            QString("未安装：%1\n可在「设置 → OCR 识别」页在线下载语言包。")
                .arg(missing.join("、")), this));
    }
    if (installed.isEmpty()) {
        body()->addWidget(detailText(
            "当前未检测到任何语言包。语言包默认保存在 sdk/tesseract/tessdata/，"
            "也可在设置中指定自定义目录；先跳过此步也不影响后续安装。", this));
    }

    body()->addStretch();
}

QString OcrStep::getSelectedLangs() const
{
    QStringList selected;
    for (auto it = m_checks.begin(); it != m_checks.end(); ++it) {
        if (it.value()->isChecked())
            selected.append(it.key());
    }
    return selected.isEmpty() ? "eng" : selected.join("+");
}

// ============================================================================
// SaveStep
// ============================================================================

SaveStep::SaveStep(QWidget* parent)
    : SetupStep("截图保存", "设置截图文件的默认保存位置和格式。", {}, parent)
{
    m_saveDirEdit = new ElaLineEdit(this);
    m_saveDirEdit->setText(defaultSaveDir());
    auto* browseBtn = secondaryButton("浏览…", this);
    connect(browseBtn, &QPushButton::clicked, this, &SaveStep::browse);

    auto* dirHost = new QWidget(this);
    auto* dirRow = new QHBoxLayout(dirHost);
    dirRow->setContentsMargins(0, 0, 0, 0);
    dirRow->setSpacing(8);
    dirRow->addWidget(m_saveDirEdit, 1);
    dirRow->addWidget(browseBtn);

    auto* dirCard = new SettingsCard("保存目录", "点击工具条的「保存」时使用的目录", this);
    dirCard->setControlStretch(1);
    dirCard->addControl(dirHost);
    body()->addWidget(dirCard);

    auto* formatCard = new SettingsCard("保存格式", "PNG 无损体积大，JPG 有损体积小", this);
    m_formatCombo = new ElaComboBox(formatCard);
    m_formatCombo->addItems({"png", "jpg", "bmp"});
    m_formatCombo->setMinimumWidth(190);
    formatCard->addControl(m_formatCombo);
    body()->addWidget(formatCard);

    body()->addStretch();
}

void SaveStep::browse()
{
    QString dir = QFileDialog::getExistingDirectory(this, "选择截图保存目录",
                                                     m_saveDirEdit->text());
    if (!dir.isEmpty()) m_saveDirEdit->setText(dir);
}

QString SaveStep::getSaveDir() const
{
    QString d = m_saveDirEdit->text().trimmed();
    return d.isEmpty() ? defaultSaveDir() : d;
}

QString SaveStep::getFormat() const { return m_formatCombo->currentText(); }

// ============================================================================
// FinishStep
// ============================================================================

FinishStep::FinishStep(QWidget* parent)
    : SetupStep("确认设置", "检查以下配置，点击「完成」保存并启动 SnapLens。", {}, parent)
{
    auto* panel = new PanelCard("配置汇总",
                                "所有项稍后都可以在「设置」中修改。", this);
    m_summary = new ElaText(panel);
    m_summary->setTextPixelSize(13);
    m_summary->setIsWrapAnywhere(true);
    m_summary->setTextInteractionFlags(Qt::TextSelectableByMouse);
    panel->setContent(m_summary);
    body()->addWidget(panel);
    body()->addStretch();
}

void FinishStep::setSummary(const QString& text) { m_summary->setText(text); }

// ============================================================================
// StepRail —— 步骤导轨（编号圆点 + 连接线）
// ============================================================================

namespace {
constexpr int kDotSize  = 22;   // 圆点直径
constexpr int kTextPad  = 12;   // 圆点与文字的间距
constexpr int kItemPad  = 10;   // 紧凑步骤轨，避免窗口被导轨撑高
constexpr int kDescH    = 14;   // 说明行高度
} // namespace

StepRail::StepRail(QWidget* parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    setMouseTracking(true);
    bindTheme(this, [this] { update(); });
}

void StepRail::relayout()
{
    int y = 0;
    for (Item& item : m_items) {
        const int h = kDotSize + kItemPad + (item.description.isEmpty() ? 0 : kDescH);
        item.rect = QRect(0, y, width(), h);
        y += h;
    }
    setFixedHeight(qMax(y, 1));
}

void StepRail::addStep(const QString& title, const QString& description)
{
    m_items.append({title, description, QRect()});
    relayout();
    update();
}

void StepRail::setCurrent(int index)
{
    if (index < 0 || index >= m_items.size()) return;
    m_current = index;
    update();
}

int StepRail::indexAt(const QPoint& pos) const
{
    for (int i = 0; i < m_items.size(); ++i) {
        if (m_items.at(i).rect.contains(pos)) return i;
    }
    return -1;
}

void StepRail::mouseMoveEvent(QMouseEvent* event)
{
    const int idx = indexAt(event->pos());
    if (idx != m_hover) {
        m_hover = idx;
        update();
    }
    QWidget::mouseMoveEvent(event);
}

void StepRail::leaveEvent(QEvent* event)
{
    if (m_hover != -1) {
        m_hover = -1;
        update();
    }
    QWidget::leaveEvent(event);
}

void StepRail::mouseReleaseEvent(QMouseEvent* event)
{
    const int idx = indexAt(event->pos());
    if (idx >= 0 && idx != m_current) {
        emit stepClicked(idx);
    }
    QWidget::mouseReleaseEvent(event);
}

void StepRail::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event)
    QPainter painter(this);
    painter.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing);

    const QColor accent = colorPrimary();
    const QColor textColor = colorText();
    const QColor subColor = colorTextSecondary();
    const QColor lineColor = colorBorder();

    for (int i = 0; i < m_items.size(); ++i) {
        const Item& item = m_items.at(i);
        const bool isCurrent = (i == m_current);
        const bool isDone = (i < m_current);

        // 连接线：当前步骤之前的线段用强调色，之后保持中性
        if (i + 1 < m_items.size()) {
            painter.setPen(QPen(isDone ? accent : lineColor, 1.5));
            const int x = kDotSize / 2;
            painter.drawLine(x, item.rect.top() + kDotSize + 4,
                             x, item.rect.bottom() + 4);
        }

        // 圆点
        const QRect dot(item.rect.left(), item.rect.top() + 2, kDotSize, kDotSize);
        if (isCurrent) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(accent);
            painter.drawEllipse(dot);
        } else {
            painter.setPen(QPen(isDone ? accent : lineColor, 1.5));
            painter.setBrush(Qt::NoBrush);
            painter.drawEllipse(dot.adjusted(1, 1, -1, -1));
        }

        // 圆点内的字符：走过的步骤打勾，未到的显示序号
        QFont dotFont = font();
        dotFont.setPixelSize(12);
        dotFont.setWeight(QFont::DemiBold);
        painter.setFont(dotFont);
        painter.setPen(isCurrent ? colorAccentText()
                                 : (isDone ? accent : subColor));
        painter.drawText(dot, Qt::AlignCenter,
                         i <= m_current ? QStringLiteral("\u2713")
                                        : QString::number(i + 1));

        // 标题
        QFont titleFont = font();
        titleFont.setPixelSize(14);
        titleFont.setWeight(isCurrent ? QFont::DemiBold : QFont::Normal);
        painter.setFont(titleFont);
        const bool highlight = isCurrent || (m_hover == i);
        painter.setPen(highlight ? textColor : subColor);
        painter.drawText(QRect(dot.right() + kTextPad, item.rect.top() + 4,
                               width() - dot.right() - kTextPad, 20),
                         Qt::AlignLeft | Qt::AlignVCenter, item.title);

        // 说明
        if (!item.description.isEmpty()) {
            QFont descFont = font();
            descFont.setPixelSize(11);
            painter.setFont(descFont);
            painter.setPen(subColor);
            painter.drawText(QRect(dot.right() + kTextPad, item.rect.top() + 24,
                                   width() - dot.right() - kTextPad, kDescH),
                             Qt::AlignLeft | Qt::AlignVCenter, item.description);
        }
    }
}

// ============================================================================
// SetupWizard
// ============================================================================

SetupWizard::SetupWizard(NotifyManager* notifyManager, QWidget* parent)
    : ElaDialog(parent)
    , m_notifyManager(notifyManager)
{
    setWindowTitle("SnapLens 首次设置");
    resize(900, 560);
    setMinimumSize(800, 500);
    // 标题栏按钮与主窗口保持一致：主题切换按钮就在标题栏里，不再另设独立控件。
    setWindowButtonFlags(ElaAppBarType::CloseButtonHint
                         | ElaAppBarType::ThemeChangeButtonHint);
    setIsDefaultClosed(false);
    setIsStayTop(false);

    buildUi();

    connect(this, &ElaDialog::closeButtonClicked, this, [this] { reject(); });
    connect(this, &ElaDialog::themeChangeButtonClicked, this, [] {
        applyThemeMode(eTheme->getThemeMode() == ElaThemeType::Dark
                           ? ThemeMode::Light : ThemeMode::Dark);
    });
    goToStep(0);
    polishPopup(this);
}

void SetupWizard::buildUi()
{
    // 版式：左侧品牌 + 步骤导轨（分层底色），右侧步骤内容 + 底部操作栏。
    // 步骤导轨与设置页的分类栏刻意采用不同视觉语言（编号圆点 vs 图标列表），
    // 让「按顺序走完的流程」和「并列的分类」一眼可辨。
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    auto* columns = new QHBoxLayout();
    columns->setContentsMargins(0, 0, 0, 0);
    columns->setSpacing(0);

    // ── 左侧：品牌 + 步骤 ──
    auto* railPanel = new QWidget(this);
    railPanel->setObjectName(QStringLiteral("SnapWizardRail"));
    railPanel->setAttribute(Qt::WA_StyledBackground, true);
    railPanel->setFixedWidth(244);
    bindTheme(railPanel, [railPanel] {
        railPanel->setStyleSheet(QStringLiteral(
            "QWidget#SnapWizardRail { background: %1; border-right: 1px solid %2; }")
            .arg(colorRail().name(QColor::HexArgb),
                 colorBorder().name(QColor::HexArgb)));
    });

    auto* railLayout = new QVBoxLayout(railPanel);
    railLayout->setContentsMargins(20, 52, 16, 14);
    railLayout->setSpacing(0);

    auto* brand = new QHBoxLayout();
    brand->setSpacing(12);
    auto* logo = new QLabel(railPanel);
    logo->setPixmap(makeAppIcon(44));
    logo->setFixedSize(44, 44);
    logo->setStyleSheet(QStringLiteral("background: transparent;"));
    brand->addWidget(logo);
    auto* brandText = new QVBoxLayout();
    brandText->setContentsMargins(0, 2, 0, 0);
    brandText->setSpacing(1);
    auto* brandTitle = cardTitle("SnapLens", railPanel);
    brandTitle->setTextPixelSize(16);
    QFont brandFont = brandTitle->font();
    brandFont.setWeight(QFont::DemiBold);
    brandTitle->setFont(brandFont);
    brandText->addWidget(brandTitle);
    brandText->addWidget(caption("首次设置 · 约 1 分钟", railPanel));
    brand->addLayout(brandText);
    brand->addStretch();
    railLayout->addLayout(brand);
    railLayout->addSpacing(16);

    m_rail = new StepRail(railPanel);
    railLayout->addWidget(m_rail);
    railLayout->addStretch();

    columns->addWidget(railPanel);

    // ── 右侧：步骤内容 + 操作栏 ──
    auto* main = new QWidget(this);
    auto* mainLayout = new QVBoxLayout(main);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    m_stack = new QStackedWidget(main);
    m_stack->setStyleSheet(QStringLiteral("QStackedWidget { background: transparent; }"));
    mainLayout->addWidget(m_stack, 1);
    mainLayout->addWidget(separator(Qt::Horizontal, main));

    auto* footer = new QWidget(main);
    footer->setFixedHeight(56);
    auto* footerLayout = new QHBoxLayout(footer);
    footerLayout->setContentsMargins(Dim::PageHMargin, 0, Dim::PageHMargin, 0);
    footerLayout->setSpacing(8);

    m_progressLabel = caption(QString(), footer);
    footerLayout->addWidget(m_progressLabel);
    footerLayout->addStretch();

    m_cancelBtn = secondaryButton("取消", footer);
    connect(m_cancelBtn, &QPushButton::clicked, this, [this] { reject(); });
    footerLayout->addWidget(m_cancelBtn);

    m_backBtn = secondaryButton("上一步", footer);
    connect(m_backBtn, &QPushButton::clicked, this, [this] {
        goToStep(qMax(0, m_stack->currentIndex() - 1));
    });
    footerLayout->addWidget(m_backBtn);

    m_nextBtn = primaryButton("下一步", footer);
    m_nextBtn->setMinimumWidth(104);
    connect(m_nextBtn, &QPushButton::clicked, this, [this] {
        const int next = m_stack->currentIndex() + 1;
        if (next >= m_stack->count()) {
            accept();   // 完成
        } else {
            goToStep(next);
        }
    });
    footerLayout->addWidget(m_nextBtn);
    mainLayout->addWidget(footer);

    columns->addWidget(main, 1);
    root->addLayout(columns, 1);

    // ── 步骤注册 ──
    m_stepWelcome = new WelcomeStep();
    m_stepMode    = new ModeStep();
    m_stepHotkey  = new HotkeyStep();
    m_stepAi      = new AiStep();
    m_stepOcr     = new OcrStep();
    m_stepSave    = new SaveStep();
    m_stepFinish  = new FinishStep();

    struct StepDef { const char* title; const char* desc; SetupStep* step; };
    const QList<StepDef> steps = {
        {"欢迎",       "SnapLens 能做什么",   m_stepWelcome},
        {"使用模式",   "翻译优先 / 截图优先", m_stepMode},
        {"截图快捷键", "全局热键",            m_stepHotkey},
        {"AI 翻译",    "服务商与 API Key",    m_stepAi},
        {"OCR 识别",   "识别语言与语言包",    m_stepOcr},
        {"截图保存",   "目录与格式",          m_stepSave},
        {"完成",       "确认并保存",          m_stepFinish},
    };
    for (const StepDef& def : steps) {
        m_rail->addStep(QString::fromUtf8(def.title), QString::fromUtf8(def.desc));
        m_stack->addWidget(def.step);
    }
    connect(m_rail, &StepRail::stepClicked, this, &SetupWizard::goToStep);
}

void SetupWizard::goToStep(int index)
{
    if (index < 0 || index >= m_stack->count()) return;
    m_stack->setCurrentIndex(index);
    m_rail->setCurrent(index);
    if (auto* step = qobject_cast<SetupStep*>(m_stack->widget(index)))
        step->onEnter();
    if (m_stack->widget(index) == m_stepFinish)
        refreshSummary();
    updateFooter();
}

void SetupWizard::selectStep(int index) { goToStep(index); }
int  SetupWizard::stepCount() const     { return m_stack ? m_stack->count() : 0; }

void SetupWizard::updateFooter()
{
    const int index = m_stack->currentIndex();
    const int total = m_stack->count();
    m_backBtn->setEnabled(index > 0);

    const bool last = (index == total - 1);
    m_nextBtn->setText(last ? "完成" : "下一步");
    m_cancelBtn->setVisible(!last);
    if (m_progressLabel) {
        m_progressLabel->setText(
            index == 0 ? QStringLiteral("共 %1 步").arg(total)
                       : QStringLiteral("第 %1 步，共 %2 步").arg(index + 1).arg(total));
    }
}

void SetupWizard::refreshSummary()
{
    const QString themeText = (currentThemeMode() == ThemeMode::System)
                                  ? QStringLiteral("跟随系统")
                                  : (eTheme->getThemeMode() == ElaThemeType::Dark
                                         ? QStringLiteral("深色")
                                         : QStringLiteral("浅色"));

    const QString summary = QString(
        "使用模式　　　%1\n"
        "截图快捷键　　%2\n"
        "外观主题　　　%3\n"
        "AI 服务商　　　%4\n"
        "AI 模型　　　　%5\n"
        "默认目标语言　%6\n"
        "OCR 语言　　　%7\n"
        "保存目录　　　%8\n"
        "保存格式　　　%9"
    ).arg(m_stepMode->getMode() == "translate" ? "翻译模式" : "截图模式",
          m_stepHotkey->getHotkey(),
          themeText,
          m_stepAi->getProvider(),
          m_stepAi->getModel().isEmpty() ? "（默认）" : m_stepAi->getModel(),
          m_stepAi->getTargetLang(),
          m_stepOcr->getSelectedLangs(),
          m_stepSave->getSaveDir(),
          m_stepSave->getFormat());
    m_stepFinish->setSummary(summary);
}

void SetupWizard::reject()
{
    if (m_rejecting) return;
    m_rejecting = true;
    NotifyManager fallback;
    NotifyManager* notify = m_notifyManager ? m_notifyManager : &fallback;
    const bool confirmed = notify->confirm(
        "确认取消",
        "取消引导设置将无法进入程序，确定要退出吗？",
        this);
    if (confirmed) {
        QDialog::reject();
    }
    m_rejecting = false;
}

Settings SetupWizard::buildSettings() const
{
    Settings s(Settings::defaultsDict());

    s.app_mode    = m_stepMode->getMode();
    s.hotkey      = m_stepHotkey->getHotkey();
    s.save_dir    = m_stepSave->getSaveDir();
    s.save_format = m_stepSave->getFormat();
    // 外观主题：跟随系统时留 system；用户在标题栏手动切过则记为显式模式
    s.theme_mode  = themeModeToString(currentThemeMode());

    s.ai_provider = m_stepAi->getProvider();
    if (!m_stepAi->getApiKey().isEmpty())
        s.ai_api_key = m_stepAi->getApiKey();
    if (!m_stepAi->getApiBase().isEmpty())
        s.ai_api_base = m_stepAi->getApiBase();
    if (!m_stepAi->getModel().isEmpty())
        s.ai_model = m_stepAi->getModel();
    s.ai_target_lang = m_stepAi->getTargetLang();

    s.ai_ocr_langs = m_stepOcr->getSelectedLangs();

    return s;
}
