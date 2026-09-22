#include "toolbar.h"

#include <QFrame>
#include <QGraphicsDropShadowEffect>
#include <QGraphicsOpacityEffect>
#include <QHBoxLayout>
#include <QIcon>
#include <QPainter>
#include <QSize>

#include "log/log_manager.h"

namespace {

constexpr int  ICON_SIZE      = 19;
constexpr int  BUTTON_SIZE    = 34;
constexpr int  TOOLBAR_RADIUS = 8;

// 浮动操作条是深色面板（覆盖在用户截取的画面上），因此颜色不跟随应用主题。
const QColor PANEL_BG      (32, 32, 32, 236);
const QColor PANEL_BORDER  (255, 255, 255, 30);
const QColor BUTTON_HOVER  (255, 255, 255, 32);
const QColor ACCENT        (0, 103, 192);

} // namespace

SnipToolbar::SnipToolbar(double opacity, QWidget *parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_StyledBackground, false);

    if (opacity < 1.0) {
        auto *effect = new QGraphicsOpacityEffect(this);
        effect->setOpacity(qBound(0.1, opacity, 1.0));
        setGraphicsEffect(effect);
    }

    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(6, 5, 6, 5);
    layout->setSpacing(2);

    layout->addWidget(makeButton(ElaIconType::FloppyDisk,
                                 QStringLiteral("save"),
                                 QStringLiteral("保存到文件（Ctrl+S）")));
    layout->addWidget(makeButton(ElaIconType::Thumbtack,
                                 QStringLiteral("pin"),
                                 QStringLiteral("钉图置顶")));
    layout->addWidget(makeButton(ElaIconType::Language,
                                 QStringLiteral("translate"),
                                 QStringLiteral("OCR + AI 翻译图片文字")));
    layout->addWidget(makeButton(ElaIconType::Text,
                                 QStringLiteral("ocr"),
                                 QStringLiteral("OCR 识别文字")));

    layout->addWidget(makeSeparator());

    layout->addWidget(makeButton(ElaIconType::Xmark,
                                 QStringLiteral("cancel"),
                                 QStringLiteral("取消截图（Esc）")));
    layout->addWidget(makeButton(ElaIconType::Check,
                                 QStringLiteral("copy"),
                                 QStringLiteral("复制到剪贴板（Enter）"),
                                 true));
}

void SnipToolbar::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)
    QPainter painter(this);
    painter.setRenderHints(QPainter::Antialiasing);
    painter.setPen(QPen(PANEL_BORDER, 1));
    painter.setBrush(PANEL_BG);
    painter.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5),
                            TOOLBAR_RADIUS, TOOLBAR_RADIUS);
}

ElaIconButton *SnipToolbar::makeButton(ElaIconType::IconName icon,
                                     const QString &action,
                                     const QString &tooltip,
                                     bool accent)
{
    auto *btn = new ElaIconButton(icon, ICON_SIZE, BUTTON_SIZE, BUTTON_SIZE, this);
    btn->setBorderRadius(6);
    btn->setLightIconColor(Qt::white);
    btn->setDarkIconColor(Qt::white);
    btn->setLightHoverIconColor(Qt::white);
    btn->setDarkHoverIconColor(Qt::white);
    btn->setLightHoverColor(accent ? ACCENT : BUTTON_HOVER);
    btn->setDarkHoverColor(accent ? ACCENT : BUTTON_HOVER);
    btn->setIsSelected(accent);
    btn->setToolTip(tooltip);
    btn->setFocusPolicy(Qt::NoFocus);
    btn->setCursor(Qt::PointingHandCursor);

    connect(btn, &QToolButton::clicked, this, [this, action]() {
        emit actionChosen(action);
    });
    return btn;
}

QWidget *SnipToolbar::makeSeparator()
{
    auto *line = new QFrame(this);
    line->setFixedSize(1, 22);
    line->setStyleSheet(QStringLiteral(
        "QFrame { background: rgba(255, 255, 255, 40); border: none; }"));
    auto *host = new QWidget(this);
    auto *layout = new QHBoxLayout(host);
    layout->setContentsMargins(5, 0, 5, 0);
    layout->setSpacing(0);
    layout->addWidget(line);
    return host;
}
