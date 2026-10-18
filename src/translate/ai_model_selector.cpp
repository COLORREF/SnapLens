#include "ai_model_selector.h"
#include "model_capabilities.h"
#include "providers.h"
#include "../settings.h"
#include "../ui/snapkit.h"

#include <QComboBox>
#include <QEvent>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QLabel>
#include <QRegularExpression>
#include <QShowEvent>
#include <QVBoxLayout>

AiModelSelector::AiModelSelector(Settings* settings, QWidget* parent)
    : QWidget(parent), m_settings(settings)
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(4);
    auto* row = new QHBoxLayout;
    row->setSpacing(8);
    root->addLayout(row);
    row->addWidget(SnapUi::detailText("模型", this));
    m_model = new ElaComboBox(this);
    m_model->setObjectName("translationModel");
    m_model->setEditable(true);
    m_model->setMinimumWidth(160);
    m_model->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_model->setToolTip("选择已获取的模型，或输入当前服务商的模型 ID；获取列表请到 AI 设置");
    row->addWidget(m_model, 2);
    row->addWidget(SnapUi::detailText("思考", this));
    m_thinking = new ElaComboBox(this);
    m_thinking->setObjectName("translationThinking");
    row->addWidget(m_thinking, 1);
    row->addWidget(SnapUi::detailText("档位", this));
    m_effort = new ElaComboBox(this);
    m_effort->setObjectName("translationEffort");
    m_effort->setEditable(true);
    m_effort->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    row->addWidget(m_effort, 1);
    m_hint = SnapUi::caption({}, this);
    m_hint->setWordWrap(true);
    root->addWidget(m_hint);
    for (auto* combo : {m_model, m_thinking, m_effort}) combo->installEventFilter(this);
    connect(m_model, &QComboBox::currentTextChanged, this, [this] {
        if (!m_refreshing) refreshChoices();
    });
    connect(m_thinking, &QComboBox::currentIndexChanged, this, [this] { rememberChoice(); });
    connect(m_effort, &QComboBox::currentTextChanged, this, [this] { rememberChoice(); });
    refresh();
}

void AiModelSelector::refresh()
{
    if (!m_settings || m_refreshing) return;
    const QString defaults = aiModelProfileKey(m_settings->ai_api_base, m_settings->ai_model)
        + m_settings->ai_provider;
    const bool changed = defaults != m_defaults;
    const QString selected = changed ? m_settings->ai_model : m_model->currentText();
    if (changed) m_choices.clear();
    m_defaults = defaults;
    m_refreshing = true;
    const auto cache = QJsonDocument::fromJson(m_settings->ai_model_metadata.toUtf8()).object();
    QStringList models = cache.value(providerEndpoint(m_settings->ai_api_base, "models").toString())
        .toObject().value("models").toObject().keys();
    if (m_settings->ai_provider == "custom")
        models += m_settings->ai_custom_models.split(QRegularExpression("[,，;；\\r\\n]+"), Qt::SkipEmptyParts);
    models += QStringList{m_settings->ai_model, selected};
    m_model->clear();
    for (const auto& value : models) {
        const auto id = value.trimmed();
        if (!id.isEmpty() && m_model->findText(id) < 0) m_model->addItem(id);
    }
    m_model->setCurrentText(selected);
    m_refreshing = false;
    refreshChoices();
}

void AiModelSelector::refreshChoices()
{
    if (!m_settings) return;
    m_refreshing = true;
    m_activeModel = m_model->currentText().trimmed();
    const auto options = AiRequestOptions::fromSettings(*m_settings, m_activeModel);
    const auto caps = ModelCapabilities::suggestions(options.modelMetadata, options.apiBase, m_activeModel);
    const auto choice = m_choices.value(m_activeModel);
    m_thinking->clear();
    m_thinking->addItem("按模型设置", "profile");
    m_thinking->addItem("自动（不发送）", "auto");
    for (const auto& mode : caps.thinkingModes.value_or(
             caps.reasoning && !*caps.reasoning ? QStringList{} : QStringList{"on", "off"}))
        m_thinking->addItem(mode == "on" ? "开启" : "关闭", mode);
    if (m_thinking->findData(choice.thinking) < 0)
        m_thinking->addItem(choice.thinking == "off" ? "关闭（已选）" : "开启（已选）", choice.thinking);
    m_thinking->setCurrentIndex(m_thinking->findData(choice.thinking));
    m_effort->clear();
    m_effort->addItem("按模型设置", "profile");
    m_effort->addItem("auto");
    for (const auto& effort : caps.effortChoices())
        if (m_effort->findText(effort) < 0) m_effort->addItem(effort);
    m_effort->setCurrentText(choice.effort == "profile" ? QStringLiteral("按模型设置") : choice.effort);
    const QString source = caps.effortSource == ModelCapabilities::Source::Upstream ? "接口声明"
        : caps.effortSource == ModelCapabilities::Source::Documentation ? "模型文档候选" : "通用候选，支持情况未知";
    m_hint->setText(QString("%1 · 档位：%2 · 选择仅用于此窗口的下一次翻译")
        .arg(getProviderConfig(options.provider).label, source));
    m_effort->setToolTip(QString("模型配置档位：%1。按模型设置保留高级 JSON；其他选择覆盖其 reasoning_effort。auto 不发送。")
        .arg(options.reasoningEffort));
    m_thinking->setToolTip("按模型设置保留思考配置（含高级 JSON）；其他选择覆盖本次请求的思考字段。低档位不等于关闭思考。");
    m_refreshing = false;
}

QString AiModelSelector::effortSelection() const
{
    return m_effort->currentIndex() == 0 && m_effort->currentText() == m_effort->itemText(0)
        ? QStringLiteral("profile") : m_effort->currentText().trimmed();
}

void AiModelSelector::rememberChoice()
{
    if (!m_refreshing) m_choices[m_activeModel] = {m_thinking->currentData().toString(), effortSelection()};
}

AiRequestOptions AiModelSelector::snapshot()
{
    refresh(); // Settings may have been saved while this window remained open.
    if (!m_settings) return {};
    auto options = AiRequestOptions::fromSettings(*m_settings, m_model->currentText());
    options.model = m_model->currentText().trimmed(); // Empty user input remains a validation error.
    const auto choice = m_choices.value(m_activeModel);
    QJsonParseError parseError;
    const auto doc = QJsonDocument::fromJson(options.extraBody.toUtf8(), &parseError);
    auto extra = doc.object();
    if (choice.thinking != "profile") {
        options.thinking = choice.thinking;
        extra.remove("thinking");
        extra.remove("enable_thinking");
    }
    if (choice.effort != "profile") {
        options.reasoningEffort = choice.effort;
        extra.remove("reasoning_effort");
    }
    // Preserve invalid JSON so normal request validation still reports it.
    if (doc.isObject() && parseError.error == QJsonParseError::NoError)
        options.extraBody = QString::fromUtf8(QJsonDocument(extra).toJson(QJsonDocument::Compact));
    return options;
}

void AiModelSelector::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    refresh();
}

bool AiModelSelector::eventFilter(QObject* watched, QEvent* event)
{
    if (event->type() == QEvent::FocusIn) refresh();
    return QWidget::eventFilter(watched, event);
}
