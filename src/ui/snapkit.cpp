#include <QResizeEvent>
#include "snapkit.h"

#include <QApplication>
#include <QBoxLayout>
#include <QComboBox>
#include <QEnterEvent>
#include <QGuiApplication>
#include <QKeySequence>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QSlider>
#include <QStyleHints>
#include <QStyle>
#include <QStyleOption>

#include <cmath>

namespace SnapUi {

// ============================================================================
// 语义颜色
// ============================================================================

static ElaThemeType::ThemeMode mode()
{
    return eTheme->getThemeMode();
}

QColor colorPrimary()      { return ElaThemeColor(mode(), PrimaryNormal); }
QColor colorPrimaryHover() { return ElaThemeColor(mode(), PrimaryHover); }
QColor colorText()         { return ElaThemeColor(mode(), BasicText); }
QColor colorTextSecondary(){ return ElaThemeColor(mode(), BasicDetailsText); }
QColor colorTextTertiary() { return ElaThemeColor(mode(), BasicTextNoFocus); }
QColor colorBorder()       { return ElaThemeColor(mode(), BasicBorder); }
QColor colorSurface()      { return ElaThemeColor(mode(), BasicBase); }
QColor colorAccentText()   { return ElaThemeColor(mode(), BasicTextInvert); }

QColor colorRail()
{
    // 侧栏用"分层色"：深色主题取弹层底色（比页面亮一档），
    // 浅色主题取窗口底色（比页面暗一档）。
    return (mode() == ElaThemeType::Dark) ? ElaThemeColor(mode(), PopupBase)
                                          : ElaThemeColor(mode(), WindowBase);
}

void bindTheme(QWidget* widget, const std::function<void()>& apply)
{
    if (!widget || !apply) return;
    apply();
    // 以 widget 作为 context 对象：widget 析构时连接自动断开，无需登记表。
    QObject::connect(eTheme, &ElaTheme::themeModeChanged, widget,
                     [apply](ElaThemeType::ThemeMode) { apply(); });
}

static void applyTextColor(ElaText* text, const std::function<QColor()>& pick)
{
    if (!text) return;
    bindTheme(text, [text, pick] {
        text->setStyleSheet(QStringLiteral("background: transparent; color: %1;")
                                .arg(pick().name(QColor::HexArgb)));
    });
}

void makeSecondaryText(ElaText* text) { applyTextColor(text, colorTextSecondary); }
void makeTertiaryText(ElaText* text)  { applyTextColor(text, colorTextTertiary); }

// ============================================================================
// 文本构件
// ============================================================================

static ElaText* makeText(const QString& value, int pixelSize, QWidget* parent,
                         bool wrap = false)
{
    auto* text = new ElaText(value, parent);
    text->setTextPixelSize(pixelSize);
    // ElaText 只有在 wordWrap() 与 isWrapAnywhere 同时为真时才换行渲染，
    // 中文长句没有空格，必须开 WrapAnywhere，否则会被裁切。
    text->setWordWrap(wrap);
    text->setIsWrapAnywhere(wrap);
    text->setTextInteractionFlags(Qt::TextSelectableByMouse);
    return text;
}

ElaText* pageTitle(const QString& text, QWidget* parent)
{
    return makeText(text, 26, parent);
}

ElaText* pageSubtitle(const QString& text, QWidget* parent)
{
    auto* label = makeText(text, 13, parent, true);
    makeSecondaryText(label);
    return label;
}

ElaText* sectionHeader(const QString& text, QWidget* parent)
{
    auto* label = makeText(text, 16, parent);
    QFont f = label->font();
    f.setWeight(QFont::DemiBold);
    label->setFont(f);
    return label;
}

ElaText* cardTitle(const QString& text, QWidget* parent)
{
    return makeText(text, 14, parent);
}

ElaText* detailText(const QString& text, QWidget* parent)
{
    auto* label = makeText(text, 12, parent, true);
    makeSecondaryText(label);
    return label;
}

ElaText* caption(const QString& text, QWidget* parent)
{
    auto* label = makeText(text, 12, parent);
    makeSecondaryText(label);
    return label;
}

ElaText* iconGlyph(ElaIconType::IconName icon, int pixelSize, QWidget* parent)
{
    auto* glyph = new ElaText(QString(), parent);
    glyph->setElaIcon(icon);
    glyph->setTextPixelSize(pixelSize);
    glyph->setAlignment(Qt::AlignCenter);
    glyph->setFixedSize(pixelSize + 12, pixelSize + 12);
    return glyph;
}

// ============================================================================
// 页头
// ============================================================================

QWidget* pageHeader(const QString& title, const QString& subtitle, QWidget* parent)
{
    auto* host = new QWidget(parent);
    auto* layout = new QVBoxLayout(host);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);
    layout->addWidget(pageTitle(title, host));
    if (!subtitle.isEmpty())
        layout->addWidget(pageSubtitle(subtitle, host));
    return host;
}

// ============================================================================
// Card — 通用圆角容器
// ============================================================================

Card::Card(QWidget* parent)
    : QFrame(parent)
{
    setAttribute(Qt::WA_StyledBackground, false);
    bindTheme(this, [this] { update(); });
}

void Card::setRadius(int radius)
{
    m_radius = radius;
    update();
}

void Card::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event)
    QPainter painter(this);
    painter.setRenderHints(QPainter::Antialiasing);
    painter.setPen(QPen(colorBorder(), 1));
    painter.setBrush(colorSurface());
    painter.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5),
                            m_radius, m_radius);
}

// ============================================================================
// SettingsCard — WinUI3 设置行
// ============================================================================

SettingsCard::SettingsCard(const QString& title, const QString& description,
                           QWidget* parent)
    : ElaScrollPageArea(parent)
{
    // ElaScrollPageArea 默认锁死 75px 高度，这里放开以便适应不同内容。
    setMinimumHeight(0);
    setMaximumHeight(QWIDGETSIZE_MAX);
    setMinimumHeight(Dim::RowMinHeight);
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
    setBorderRadius(Dim::RowRadius);

    m_row = new QHBoxLayout(this);
    m_row->setSizeConstraint(QLayout::SetNoConstraint);
    m_row->setContentsMargins(Dim::RowPadH, Dim::RowPadV, Dim::RowPadH, Dim::RowPadV);
    m_row->setSpacing(Dim::ContentGap);

    // 行首图标槽（WinUI3 SettingsCard 的 leading glyph）：固定 20px 宽，
    // 没有图标时整体隐藏，避免右侧文字错位。
    m_iconHost = new QWidget(this);
    m_iconHost->setAttribute(Qt::WA_TranslucentBackground);
    m_iconHost->setFixedWidth(Dim::RowIconSize);
    auto* iconColumn = new QVBoxLayout(m_iconHost);
    iconColumn->setContentsMargins(0, 0, 0, 0);
    iconColumn->setSpacing(0);
    m_icon = iconGlyph(ElaIconType::None, 15, m_iconHost);
    m_icon->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    iconColumn->addWidget(m_icon);
    iconColumn->addStretch();
    m_iconHost->setVisible(false);

    m_labelHost = new QWidget(this);
    m_labelHost->setAttribute(Qt::WA_TranslucentBackground);
    // 只给下限、不设上限：说明文字开了 WrapAnywhere，最小宽度很小，
    // 因此可以自由收缩，不会再把行撑出卡片边界。
    m_labelHost->setMinimumWidth(220);
    auto* labelColumn = new QVBoxLayout(m_labelHost);
    labelColumn->setContentsMargins(0, 0, 0, 0);
    labelColumn->setSpacing(2);
    m_titleText = cardTitle(title, m_labelHost);
    labelColumn->addWidget(m_titleText);
    if (!description.isEmpty()) {
        m_descText = detailText(description, m_labelHost);
        labelColumn->addWidget(m_descText);
    }
    labelColumn->addStretch();

    m_controlHost = new QWidget(this);
    m_controlHost->setAttribute(Qt::WA_TranslucentBackground);
    m_controlLayout = new QHBoxLayout(m_controlHost);
    m_controlLayout->setContentsMargins(0, 0, 0, 0);
    m_controlLayout->setSpacing(8);

    m_spacer = new QSpacerItem(0, 0, QSizePolicy::Expanding, QSizePolicy::Minimum);

    m_headerHost = new QWidget(this);
    auto* header = new QHBoxLayout(m_headerHost);
    header->setContentsMargins(0, 0, 0, 0);
    header->setSpacing(Dim::ContentGap);
    header->addWidget(m_iconHost);
    header->addWidget(m_labelHost, 1);
    m_row->addWidget(m_headerHost, 1);
    m_row->addItem(m_spacer);
    m_row->addWidget(m_controlHost);

    bindTheme(this, [this] { refreshEnabledLook(); });
}

void SettingsCard::setIcon(ElaIconType::IconName icon)
{
    if (!m_icon || !m_iconHost) return;
    m_icon->setElaIcon(icon);
    m_iconHost->setVisible(icon != ElaIconType::None);
}

void SettingsCard::refreshEnabledLook()
{
    const bool on = isEnabled();
    QColor titleColor = colorText();
    QColor descColor = colorTextSecondary();
    if (!on) {
        titleColor.setAlphaF(0.45f);
        descColor.setAlphaF(0.45f);
    }
    if (m_titleText) {
        m_titleText->setStyleSheet(
            QStringLiteral("background: transparent; color: %1;")
                .arg(titleColor.name(QColor::HexArgb)));
    }
    if (m_descText) {
        m_descText->setStyleSheet(
            QStringLiteral("background: transparent; color: %1;")
                .arg(descColor.name(QColor::HexArgb)));
    }
    if (m_icon) {
        // 图标用 ElaText 绘制（ElaAwesome 字形），颜色跟次要文字一致
        m_icon->setStyleSheet(
            QStringLiteral("background: transparent; color: %1;")
                .arg(descColor.name(QColor::HexArgb)));
    }
    update();
}

void SettingsCard::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event)
    // ElaScrollPageArea 的默认底色是「半透明叠色」，深色主题下与页面底色
    // 几乎融在一起，设置行看起来没有边界。这里改用不透明表面色 + 1px 描边，
    // 得到 WinUI3 SettingsCard 那种清晰但克制的分层。
    QPainter painter(this);
    painter.setRenderHints(QPainter::Antialiasing);
    painter.setPen(QPen(colorBorder(), 1));
    painter.setBrush(colorSurface());
    painter.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5),
                            Dim::RowRadius, Dim::RowRadius);
}

void SettingsCard::changeEvent(QEvent* event)
{
    if (event->type() == QEvent::EnabledChange)
        refreshEnabledLook();
    ElaScrollPageArea::changeEvent(event);
}

void SettingsCard::addControl(QWidget* control)
{
    if (!control) return;
    control->setParent(m_controlHost);
    m_controlLayout->addWidget(control, 0, Qt::AlignVCenter);
}

void SettingsCard::addControls(const QList<QWidget*>& controls)
{
    for (QWidget* control : controls)
        addControl(control);
}

void SettingsCard::addControlLayout(QLayout* layout)
{
    if (!layout) return;
    m_controlLayout->addLayout(layout);
}

void SettingsCard::setControlStretch(int stretch)
{
    m_controlStretch = stretch;
    m_row->setStretchFactor(m_controlHost, m_stacked ? 0 : qMax(0, stretch));
    m_row->setStretchFactor(m_headerHost, !m_stacked && stretch <= 0 ? 1 : 0);
}

QSize SettingsCard::minimumSizeHint() const
{
    // Scroll pages may shrink a row below its horizontal layout minimum.
    // resizeEvent then moves the controls beneath the title.
    return QSize(qMax(280, m_controlHost->minimumSizeHint().width() + 2 * Dim::RowPadH),
                 Dim::RowMinHeight);
}

void SettingsCard::resizeEvent(QResizeEvent* event)
{
    ElaScrollPageArea::resizeEvent(event);
    const bool stacked = width() < 620;
    if (stacked != m_stacked) {
        m_stacked = stacked;
        m_row->setDirection(stacked ? QBoxLayout::TopToBottom : QBoxLayout::LeftToRight);
        m_spacer->changeSize(0, 0, QSizePolicy::Minimum, QSizePolicy::Minimum);
        setControlStretch(m_controlStretch);
        m_row->invalidate();
        updateGeometry();
    }
}

void SettingsCard::setTall(bool tall)
{
    if (m_descText)
        m_descText->setIsWrapAnywhere(true);
    setMinimumHeight(tall ? 0 : Dim::RowMinHeight);
}

// ============================================================================
// PanelCard — 带标题的内容卡片
// ============================================================================

PanelCard::PanelCard(const QString& title, const QString& description, QWidget* parent)
    : Card(parent)
{
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(Dim::CardPad, Dim::CardPad - 2, Dim::CardPad, Dim::CardPad - 2);
    outer->setSpacing(10);

    m_headerRow = new QHBoxLayout();
    m_headerRow->setContentsMargins(0, 0, 0, 0);
    m_headerRow->setSpacing(8);
    m_headerRow->addWidget(cardTitle(title, this));
    m_headerRow->addStretch();
    outer->addLayout(m_headerRow);

    if (!description.isEmpty())
        outer->addWidget(detailText(description, this));

    m_body = new QVBoxLayout();
    m_body->setContentsMargins(0, 0, 0, 0);
    m_body->setSpacing(6);
    outer->addLayout(m_body, 1);
}

void PanelCard::setContent(QWidget* content, int stretch)
{
    if (!content) return;
    content->setParent(this);
    m_body->addWidget(content, stretch);
}

void PanelCard::addContentWidget(QWidget* widget, int stretch)
{
    if (!widget) return;
    widget->setParent(this);
    m_body->addWidget(widget, stretch);
}

void PanelCard::addHeaderWidget(QWidget* widget)
{
    if (!widget) return;
    widget->setParent(this);
    m_headerRow->addWidget(widget, 0, Qt::AlignVCenter);
}

// ============================================================================
// 控件工厂
// ============================================================================

ElaPushButton* primaryButton(const QString& text, QWidget* parent)
{
    auto* button = new ElaPushButton(text, parent);
    button->setBorderRadius(5);
    button->setMinimumHeight(32);
    button->setMinimumWidth(104);
    button->setLightDefaultColor(QColor(QStringLiteral("#0067C0")));
    button->setLightHoverColor(QColor(QStringLiteral("#1975C5")));
    button->setLightPressColor(QColor(QStringLiteral("#005A9E")));
    button->setDarkDefaultColor(QColor(QStringLiteral("#4CC2FF")));
    button->setDarkHoverColor(QColor(QStringLiteral("#5FCBFF")));
    button->setDarkPressColor(QColor(QStringLiteral("#3FB2EF")));
    button->setLightTextColor(Qt::white);
    button->setDarkTextColor(QColor(QStringLiteral("#102030")));
    return button;
}

ElaPushButton* secondaryButton(const QString& text, QWidget* parent)
{
    auto* button = new ElaPushButton(text, parent);
    button->setBorderRadius(5);
    button->setMinimumHeight(32);
    button->setMinimumWidth(88);
    return button;
}

ElaPushButton* subtleButton(const QString& text, QWidget* parent)
{
    auto* button = secondaryButton(text, parent);
    button->setMinimumHeight(28);
    return button;
}

ElaToolButton* iconButton(ElaIconType::IconName icon, const QString& tooltip,
                          QWidget* parent)
{
    auto* button = new ElaToolButton(parent);
    button->setFixedSize(32, 32);
    button->setIsTransparent(false);
    button->setElaIcon(icon);
    button->setToolTip(tooltip);
    button->setFocusPolicy(Qt::NoFocus);
    button->setCursor(Qt::PointingHandCursor);
    return button;
}

ElaToggleSwitch* toggleSwitch(bool checked, QWidget* parent)
{
    auto* toggle = new ElaToggleSwitch(parent);
    // ElaToggleSwitch::setIsToggled 会播放位移动画并异步发出 toggled，
    // 初始化阶段静默设置，避免把"载入已有配置"误当成用户操作。
    const bool blocked = toggle->blockSignals(true);
    toggle->setIsToggled(checked);
    toggle->blockSignals(blocked);
    return toggle;
}

QFrame* separator(Qt::Orientation orientation, QWidget* parent)
{
    auto* line = new QFrame(parent);
    line->setFrameShape(orientation == Qt::Horizontal ? QFrame::HLine : QFrame::VLine);
    line->setFrameShadow(QFrame::Plain);
    line->setObjectName(QStringLiteral("SnapSeparator"));
    bindTheme(line, [line, orientation] {
        const QString sizeRule = (orientation == Qt::Horizontal)
                                     ? QStringLiteral("max-height: 1px;")
                                     : QStringLiteral("max-width: 1px;");
        line->setStyleSheet(QStringLiteral(
            "QFrame#SnapSeparator { background: %1; border: none; %2 }")
                                .arg(colorBorder().name(QColor::HexArgb), sizeRule));
    });
    if (orientation == Qt::Horizontal)
        line->setFixedHeight(1);
    else
        line->setFixedWidth(1);
    return line;
}

class FocusShell final : public QWidget
{
public:
    explicit FocusShell(QWidget* parent) : QWidget(parent) {}

    void refreshLook()
    {
        const QColor border = m_focused ? colorPrimary() : colorBorder();
        setStyleSheet(QStringLiteral(
            "QWidget#SnapInputShell { background: %1; border: %2px solid %3;"
            " border-radius: 5px; }")
            .arg(colorSurface().name(QColor::HexArgb))
            .arg(m_focused ? 2 : 1)
            .arg(border.name(QColor::HexArgb)));
    }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (event->type() == QEvent::FocusIn || event->type() == QEvent::FocusOut) {
            m_focused = event->type() == QEvent::FocusIn;
            refreshLook();
        }
        return QWidget::eventFilter(watched, event);
    }

private:
    bool m_focused = false;
};

/// 把任意控件包进一个 Fluent 风格的输入框外壳。
/// 直接用样式表给容器上色会级联到子控件（Qt 的经典陷阱），
/// 因此这里用 objectName 限定选择器，只作用于外壳本身。
QWidget* inputShell(QWidget* inner, QWidget* parent)
{
    auto* host = new FocusShell(parent);
    host->setObjectName(QStringLiteral("SnapInputShell"));
    auto* layout = new QHBoxLayout(host);
    layout->setContentsMargins(8, 2, 8, 2);
    layout->setSpacing(6);
    if (inner) {
        inner->setParent(host);
        inner->installEventFilter(host);
        host->setFocusProxy(inner);
        layout->addWidget(inner, 1);
        inner->setStyleSheet(QStringLiteral(
            "QKeySequenceEdit, QKeySequenceEdit > QLineEdit"
            " { background: transparent; border: none; }"));
    }
    bindTheme(host, [host] { host->refreshLook(); });
    host->setMinimumHeight(32);
    return host;
}

// ============================================================================
// 设置行工厂
// ============================================================================

// 行首图标的兜底推断：按标题关键字选一个语义相近的字形。
// 调用方传入显式图标时以显式为准，因此这里只需要覆盖高频词。
static ElaIconType::IconName iconForTitle(const QString& title)
{
    struct Rule { const char* key; ElaIconType::IconName icon; };
    static const Rule rules[] = {
        {"快捷键",   ElaIconType::Keyboard},
        {"热键",     ElaIconType::Keyboard},
        {"主题",     ElaIconType::CircleHalf},
        {"颜色",     ElaIconType::Palette},
        {"色值",     ElaIconType::Palette},
        {"不透明度", ElaIconType::CircleHalfStroke},
        {"粗细",     ElaIconType::Sliders},
        {"倍率",     ElaIconType::MagnifyingGlassPlus},
        {"大小",     ElaIconType::Expand},
        {"半径",     ElaIconType::Expand},
        {"网格",     ElaIconType::TableCells},
        {"格式",     ElaIconType::FileImage},
        {"目录",     ElaIconType::FolderOpen},
        {"保存",     ElaIconType::FloppyDisk},
        {"语言",     ElaIconType::Language},
        {"模型",     ElaIconType::Robot},
        {"服务商",   ElaIconType::Cloud},
        {"API",      ElaIconType::Cloud},
        {"地址",     ElaIconType::Cloud},
        {"超时",     ElaIconType::ClockRotateLeft},
        {"Token",    ElaIconType::Text},
        {"温度",     ElaIconType::TemperatureArrowUp},
        {"惩罚",     ElaIconType::Sliders},
        {"种子",     ElaIconType::Wand},
        {"提示词",   ElaIconType::Comment},
        {"翻译",     ElaIconType::RightLeft},
        {"目标语言", ElaIconType::Language},
        {"坐标",     ElaIconType::Compass},
        {"光标",     ElaIconType::ArrowPointer},
        {"十字线",   ElaIconType::CircleHalfStroke},
        {"放大镜",   ElaIconType::MagnifyingGlassPlus},
        {"复制",     ElaIconType::Copy},
        {"启动",     ElaIconType::Rocket},
        {"关闭",     ElaIconType::DoorOpen},
        {"通知",     ElaIconType::Bell},
        {"日志",     ElaIconType::ClipboardList},
        {"模式",     ElaIconType::Sliders},
        {"标签",     ElaIconType::Font},
        {"截图",     ElaIconType::CameraViewfinder},
    };
    for (const Rule& r : rules) {
        if (title.contains(QString::fromUtf8(r.key))) return r.icon;
    }
    return ElaIconType::None;
}

static void applyRowIcon(SettingsCard* card, const QString& title,
                         ElaIconType::IconName icon)
{
    if (!card) return;
    card->setIcon(icon != ElaIconType::None ? icon : iconForTitle(title));
}

SettingsCard* row(const QString& title, const QString& description,
                  QWidget* control, QWidget* parent, ElaIconType::IconName icon)
{
    auto* card = new SettingsCard(title, description, parent);
    if (control) card->addControl(control);
    applyRowIcon(card, title, icon);
    return card;
}

SettingsCard* toggleRow(const QString& title, const QString& description,
                        bool checked, ElaToggleSwitch** toggleOut, QWidget* parent,
                        ElaIconType::IconName icon)
{
    auto* card = new SettingsCard(title, description, parent);
    auto* toggle = toggleSwitch(checked, card);
    card->addControl(toggle);
    applyRowIcon(card, title, icon);
    if (toggleOut) *toggleOut = toggle;
    return card;
}

// 滑块行内的控件：ElaSlider 占满剩余宽度 + 右侧定宽数值读数。
static QWidget* makeSliderControl(int value, int lo, int hi, const QString& suffix,
                                  QSlider** sliderOut, int decimals, double scale)
{
    auto* host = new QWidget();
    auto* layout = new QHBoxLayout(host);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(10);

    auto* slider = new ElaSlider(Qt::Horizontal, host);
    slider->setRange(lo, hi);
    slider->setValue(value);
    slider->setMinimumWidth(170);
    // ElaSliderStyle 的圆形手柄按原生 style 的槽位计算；24px 高度会
    // 使手柄在卡片里贴近上沿。使用示例控件接近的自然高度。
    slider->setFixedHeight(32);

    auto* readout = new ElaText(host);
    readout->setTextPixelSize(12);
    readout->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    readout->setFixedWidth(56);
    makeSecondaryText(readout);

    auto format = [readout, suffix, decimals, scale](int raw) {
        const double shown = decimals > 0 ? double(raw) / scale : double(raw);
        readout->setText(QString::number(shown, 'f', decimals) + suffix);
    };
    format(value);
    QObject::connect(slider, &QSlider::valueChanged, host,
                     [format](int raw) { format(raw); });

    layout->addWidget(slider, 1);
    layout->addWidget(readout);
    if (sliderOut) *sliderOut = slider;
    return host;
}

SettingsCard* sliderRow(const QString& title, const QString& description,
                        int value, int lo, int hi, const QString& suffix,
                        QSlider** sliderOut, QWidget* parent, ElaIconType::IconName icon)
{
    auto* card = new SettingsCard(title, description, parent);
    card->setControlStretch(1);
    card->addControl(makeSliderControl(value, lo, hi, suffix, sliderOut, 0, 1.0));
    applyRowIcon(card, title, icon);
    return card;
}

SettingsCard* decimalSliderRow(const QString& title, const QString& description,
                               double value, double lo, double hi, int decimals,
                               const QString& suffix, QSlider** sliderOut, QWidget* parent,
                               ElaIconType::IconName icon)
{
    const int mult = static_cast<int>(std::pow(10.0, decimals));
    auto* card = new SettingsCard(title, description, parent);
    card->setControlStretch(1);
    card->addControl(makeSliderControl(static_cast<int>(value * mult),
                                       static_cast<int>(lo * mult),
                                       static_cast<int>(hi * mult),
                                       suffix, sliderOut, decimals,
                                       static_cast<double>(mult)));
    applyRowIcon(card, title, icon);
    return card;
}

SettingsCard* comboRow(const QString& title, const QString& description,
                       const QStringList& items, int currentIndex,
                       QComboBox** comboOut, QWidget* parent, ElaIconType::IconName icon)
{
    auto* card = new SettingsCard(title, description, parent);
    auto* combo = new ElaComboBox(card);
    combo->addItems(items);
    if (currentIndex >= 0 && currentIndex < items.size())
        combo->setCurrentIndex(currentIndex);
    combo->setMinimumWidth(190);
    card->addControl(combo);
    applyRowIcon(card, title, icon);
    if (comboOut) *comboOut = combo;
    return card;
}

SettingsCard* comboRowData(const QString& title, const QString& description,
                           const QList<QPair<QString, QVariant>>& items, int currentIndex,
                           QComboBox** comboOut, QWidget* parent, ElaIconType::IconName icon)
{
    auto* card = new SettingsCard(title, description, parent);
    auto* combo = new ElaComboBox(card);
    for (const auto& item : items)
        combo->addItem(item.first, item.second);
    if (currentIndex >= 0 && currentIndex < items.size())
        combo->setCurrentIndex(currentIndex);
    combo->setMinimumWidth(190);
    card->addControl(combo);
    applyRowIcon(card, title, icon);
    if (comboOut) *comboOut = combo;
    return card;
}

SettingsCard* pathRow(const QString& title, const QString& description,
                      const QString& value, const QString& placeholder,
                      const QStringList& actionTexts, QWidget* parent,
                      ElaIconType::IconName icon)
{
    auto* card = new SettingsCard(title, description, parent);
    card->setControlStretch(1);
    applyRowIcon(card, title, icon);

    auto* host = new QWidget(card);
    auto* layout = new QHBoxLayout(host);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);

    auto* edit = new ElaLineEdit(host);
    edit->setText(value);
    edit->setPlaceholderText(placeholder);
    edit->setMinimumWidth(120);
    edit->setObjectName(QStringLiteral("SnapPathEdit"));
    layout->addWidget(edit, 1);

    for (int i = 0; i < actionTexts.size(); ++i) {
        auto* button = secondaryButton(actionTexts.at(i), host);
        button->setMinimumWidth(56);
        // 用 objectName 记录序号，便于 pathRowButton() 反查。
        button->setObjectName(QStringLiteral("SnapPathButton%1").arg(i));
        layout->addWidget(button);
    }

    card->addControl(host);
    return card;
}

QLineEdit* pathRowEdit(QWidget* card)
{
    if (!card) return nullptr;
    return card->findChild<ElaLineEdit*>(QStringLiteral("SnapPathEdit"));
}

QPushButton* pathRowButton(QWidget* card, int actionIndex)
{
    if (!card) return nullptr;
    return card->findChild<QPushButton*>(
        QStringLiteral("SnapPathButton%1").arg(actionIndex));
}

// ============================================================================
// NavItem
// ============================================================================

NavItem::NavItem(ElaIconType::IconName icon, const QString& text, QWidget* parent)
    : QWidget(parent)
    , m_text(text)
{
    setFixedHeight(38);
    setMinimumWidth(0);
    setCursor(Qt::PointingHandCursor);

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(10, 0, 10, 0);
    layout->setSpacing(10);

    m_glyph = new ElaText(QString(), this);
    m_glyph->setElaIcon(icon);
    m_glyph->setTextPixelSize(15);
    m_glyph->setFixedWidth(20);
    m_glyph->setAlignment(Qt::AlignCenter);

    m_label = new ElaText(text, this);
    m_label->setTextPixelSize(14);

    m_badgeText = new ElaText(QString(), this);
    m_badgeText->setTextPixelSize(12);
    m_badgeText->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

    layout->addWidget(m_glyph);
    layout->addWidget(m_label, 1);
    layout->addWidget(m_badgeText);

    bindTheme(this, [this] {
        restyle();
        update();
    });
}

void NavItem::setSelected(bool selected)
{
    if (m_selected == selected) return;
    m_selected = selected;
    restyle();
    update();
}

void NavItem::setBadge(const QString& badge)
{
    m_badge = badge;
    m_badgeText->setText(badge);
}

void NavItem::restyle()
{
    m_label->setStyleSheet(
        QStringLiteral("background: transparent; color: %1;")
            .arg((m_selected ? colorText() : colorTextSecondary()).name(QColor::HexArgb)));
    m_glyph->setStyleSheet(
        QStringLiteral("background: transparent; color: %1;")
            .arg((m_selected ? colorPrimary() : colorTextSecondary()).name(QColor::HexArgb)));
    m_badgeText->setStyleSheet(
        QStringLiteral("background: transparent; color: %1;")
            .arg(colorTextSecondary().name(QColor::HexArgb)));

    QFont f = m_label->font();
    f.setWeight(m_selected ? QFont::DemiBold : QFont::Normal);
    m_label->setFont(f);
}

void NavItem::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event)
    QPainter painter(this);
    painter.setRenderHints(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);

    if (m_selected) {
        QColor bg = colorPrimary();
        bg.setAlphaF(0.13f);
        painter.setBrush(bg);
        painter.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 5, 5);

        // 左侧强调条：NavigationView 的选中指示器
        QColor bar = colorPrimary();
        painter.setBrush(bar);
        painter.drawRoundedRect(QRectF(0, height() * 0.28, 3, height() * 0.44), 1.5, 1.5);
    } else if (m_hover) {
        QColor bg = colorText();
        bg.setAlphaF(0.06f);
        painter.setBrush(bg);
        painter.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 5, 5);
    }
}

void NavItem::enterEvent(QEnterEvent* event)
{
    m_hover = true;
    update();
    QWidget::enterEvent(event);
}

void NavItem::leaveEvent(QEvent* event)
{
    m_hover = false;
    update();
    QWidget::leaveEvent(event);
}

void NavItem::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && rect().contains(event->pos()))
        emit activated();
    QWidget::mouseReleaseEvent(event);
}

// ============================================================================
// NavList
// ============================================================================

NavList::NavList(QWidget* parent)
    : QWidget(parent)
{
    m_layout = new QVBoxLayout(this);
    m_layout->setContentsMargins(0, 0, 0, 0);
    m_layout->setSpacing(2);
    m_layout->addStretch();
}

NavItem* NavList::addItem(ElaIconType::IconName icon, const QString& text)
{
    auto* item = new NavItem(icon, text, this);
    m_layout->insertWidget(m_layout->count() - 1, item);
    const int index = m_items.size();
    m_items.append(item);
    connect(item, &NavItem::activated, this, [this, index] { setCurrentIndex(index); });
    if (m_current < 0) applySelection(index);
    return item;
}

void NavList::applySelection(int index)
{
    for (int i = 0; i < m_items.size(); ++i)
        m_items.at(i)->setSelected(i == index);
    m_current = index;
}

void NavList::setCurrentIndex(int index)
{
    if (index < 0 || index >= m_items.size()) return;
    if (index == m_current) return;
    applySelection(index);
    emit currentChanged(index);
}

// ============================================================================
// SettingsPage
// ============================================================================

SettingsPage::SettingsPage(const QString& title, const QString& subtitle, QWidget* parent)
    : QWidget(parent)
{
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    m_scroll = new ElaScrollArea(this);
    m_scroll->setWidgetResizable(true);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scroll->setIsGrabGesture(true, 0);
    m_scroll->setIsOverShoot(Qt::Vertical, true);

    auto* content = new QWidget();
    content->setObjectName(QStringLiteral("SnapSettingsPageContent"));
    content->setStyleSheet(QStringLiteral(
        "QWidget#SnapSettingsPageContent { background: transparent; }"));
    m_body = new QVBoxLayout(content);
    // 分类内容不再重复页面留白：外层已由设置页统一给边距
    m_body->setContentsMargins(0, 0, 0, 24);
    m_body->setSpacing(Dim::RowGap);
    m_body->setAlignment(Qt::AlignTop);

    // 分类标题比页面大标题低一级（20px 半粗），形成 页面 > 分类 > 分区 的层级
    auto* categoryTitle = pageTitle(title, content);
    categoryTitle->setTextPixelSize(20);
    QFont categoryFont = categoryTitle->font();
    categoryFont.setWeight(QFont::DemiBold);
    categoryTitle->setFont(categoryFont);
    m_body->addWidget(categoryTitle);
    if (!subtitle.isEmpty()) {
        m_body->addSpacing(2);
        m_body->addWidget(pageSubtitle(subtitle, content));
    }
    m_body->addSpacing(14);

    m_scroll->setWidget(content);
    outer->addWidget(m_scroll);
}

QWidget* SettingsPage::addSection(const QString& title, const QString& description)
{
    auto* section = new QWidget(m_scroll->widget());
    auto* layout = new QVBoxLayout(section);
    layout->setContentsMargins(0, Dim::SectionGap - Dim::RowGap, 0, 8);
    layout->setSpacing(2);
    layout->addWidget(sectionHeader(title, section));
    if (!description.isEmpty()) {
        layout->addWidget(detailText(description, section));
    }
    m_body->addWidget(section);
    return section;
}

void SettingsPage::addBlock(QWidget* widget, int bottomSpacing)
{
    if (!widget) return;
    widget->setParent(m_scroll->widget());
    m_body->addWidget(widget);
    if (bottomSpacing > 0) m_body->addSpacing(bottomSpacing);
}

void SettingsPage::addNote(const QString& text)
{
    m_body->addWidget(detailText(text, m_scroll->widget()));
}

// ============================================================================
// 其它
// ============================================================================

QString hotkeyText(const QString& keySequence)
{
    if (keySequence.isEmpty())
        return QString();
    return QKeySequence(keySequence).toString(QKeySequence::NativeText);
}

QPixmap makeAppIcon(int size)
{
    const qreal dpr = qApp->primaryScreen()
        ? qMax<qreal>(1.0, qApp->primaryScreen()->devicePixelRatio())
        : 1.0;
    QPixmap pixmap(QSize(size, size) * dpr);
    pixmap.setDevicePixelRatio(dpr);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing);
    const QRectF bounds(1, 1, size - 2, size - 2);
    QLinearGradient gradient(bounds.topLeft(), bounds.bottomRight());
    gradient.setColorAt(0.0, QColor(QStringLiteral("#62D7FF")));
    gradient.setColorAt(0.52, QColor(QStringLiteral("#1677D2")));
    gradient.setColorAt(1.0, QColor(QStringLiteral("#6750D8")));
    painter.setPen(Qt::NoPen);
    painter.setBrush(gradient);
    painter.drawRoundedRect(bounds, size * 0.22, size * 0.22);

    // 取景框四角
    const qreal inset = size * 0.24;
    const qreal arm = size * 0.15;
    painter.setPen(QPen(QColor(255, 255, 255, 235), qMax(1.6, size * 0.05),
                        Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.drawLine(QPointF(inset, inset + arm), QPointF(inset, inset));
    painter.drawLine(QPointF(inset, inset), QPointF(inset + arm, inset));
    painter.drawLine(QPointF(size - inset - arm, inset), QPointF(size - inset, inset));
    painter.drawLine(QPointF(size - inset, inset), QPointF(size - inset, inset + arm));
    painter.drawLine(QPointF(inset, size - inset - arm), QPointF(inset, size - inset));
    painter.drawLine(QPointF(inset, size - inset), QPointF(inset + arm, size - inset));

    QFont font(QStringLiteral("Microsoft YaHei UI"));
    font.setPixelSize(qRound(size * 0.32));
    font.setWeight(QFont::DemiBold);
    painter.setFont(font);
    painter.setPen(Qt::white);
    painter.drawText(QRectF(0, size * 0.35, size, size * 0.48),
                     Qt::AlignCenter, QStringLiteral("文"));
    return pixmap;
}

bool systemPrefersDark()
{
    return QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark;
}

namespace {

ThemeMode g_themeMode = ThemeMode::System;
bool      g_themeWatcherInstalled = false;
/// 置位期间说明这次 ElaTheme 变化是本模块自己发起的，用于区分
/// 「标题栏主题按钮直接改 ElaTheme」这种外部切换。
bool      g_applyingTheme = false;

void resolveAndApplyTheme()
{
    ElaThemeType::ThemeMode mode = ElaThemeType::Light;
    switch (g_themeMode) {
    case ThemeMode::Dark:
        mode = ElaThemeType::Dark;
        break;
    case ThemeMode::Light:
        mode = ElaThemeType::Light;
        break;
    case ThemeMode::System:
        mode = systemPrefersDark() ? ElaThemeType::Dark : ElaThemeType::Light;
        break;
    }
    g_applyingTheme = true;
    eTheme->setThemeMode(mode);
    g_applyingTheme = false;
    // 广播给关心外观的界面（设置页的主题下拉、向导的汇总等）
    ThemeNotifier::instance()->themeModeChanged(g_themeMode);
}

} // namespace

ThemeNotifier* ThemeNotifier::instance()
{
    static ThemeNotifier notifier;
    return &notifier;
}

ThemeMode themeModeFromString(const QString& value)
{
    if (value == QLatin1String("light")) return ThemeMode::Light;
    if (value == QLatin1String("dark"))  return ThemeMode::Dark;
    return ThemeMode::System;
}

QString themeModeToString(ThemeMode mode)
{
    switch (mode) {
    case ThemeMode::Light: return QStringLiteral("light");
    case ThemeMode::Dark:  return QStringLiteral("dark");
    case ThemeMode::System:
    default:               return QStringLiteral("system");
    }
}

ThemeMode currentThemeMode()
{
    return g_themeMode;
}

void applyThemeMode(ThemeMode mode)
{
    g_themeMode = mode;
    if (!g_themeWatcherInstalled) {
        g_themeWatcherInstalled = true;
        // Qt 原生控件的全局调色板跟随 ElaTheme（详见 applyAppPalette 注释）
        applyAppPalette();
        // 系统深浅色切换时，仅在「跟随系统」模式下重新解析
        QObject::connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged,
                         qApp, [](Qt::ColorScheme) {
                             if (g_themeMode == ThemeMode::System)
                                 resolveAndApplyTheme();
                         });
        // ElaAppBar 自带的标题栏主题按钮会直接调用 ElaTheme::setThemeMode()，
        // 绕过本模块。捕获这种外部切换并把它记为「显式浅色/深色」，
        // 这样设置页的「主题」下拉不会和实际外观脱节。
        QObject::connect(eTheme, &ElaTheme::themeModeChanged, qApp,
                         [](ElaThemeType::ThemeMode changed) {
                             if (g_applyingTheme) return;
                             g_themeMode = (changed == ElaThemeType::Dark) ? ThemeMode::Dark
                                                                           : ThemeMode::Light;
                             ThemeNotifier::instance()->themeModeChanged(g_themeMode);
                         });
    }
    resolveAndApplyTheme();
}

void applyAppPalette()
{
    auto paint = [] {
        QPalette pal = qApp->palette();
        pal.setColor(QPalette::Window, colorSurface());
        pal.setColor(QPalette::WindowText, colorText());
        pal.setColor(QPalette::Base, colorSurface());
        pal.setColor(QPalette::AlternateBase, colorSurface());
        pal.setColor(QPalette::ToolTipBase, colorSurface());
        pal.setColor(QPalette::ToolTipText, colorText());
        pal.setColor(QPalette::Text, colorText());
        pal.setColor(QPalette::PlaceholderText, colorTextTertiary());
        pal.setColor(QPalette::Button, colorSurface());
        pal.setColor(QPalette::ButtonText, colorText());
        pal.setColor(QPalette::BrightText, QColor(Qt::red));
        pal.setColor(QPalette::Highlight, colorPrimary());
        pal.setColor(QPalette::HighlightedText, colorAccentText());

        // 禁用态：与 ElaTheme 的 BasicTextDisable 观感保持一致
        QColor disabled = colorText();
        disabled.setAlphaF(0.45f);
        pal.setColor(QPalette::Disabled, QPalette::Text, disabled);
        pal.setColor(QPalette::Disabled, QPalette::WindowText, disabled);
        pal.setColor(QPalette::Disabled, QPalette::ButtonText, disabled);
        qApp->setPalette(pal);
    };

    paint();
    QObject::connect(eTheme, &ElaTheme::themeModeChanged, qApp,
                     [paint](ElaThemeType::ThemeMode) { paint(); });
}

void polishPopup(QWidget* popup)
{
    if (!popup) return;
    popup->setAttribute(Qt::WA_StyledBackground, true);
    popup->setStyleSheet(QStringLiteral(
        "QToolTip { padding: 6px 10px; border-radius: 5px; }"
        "QSplitter::handle { background: transparent; }"
        "QSplitter::handle:horizontal { width: 10px; }"
        "QSplitter::handle:vertical { height: 10px; }"));
}

} // namespace SnapUi
