#include "home_tool_card.h"
#include "snapkit.h"

#include <QEnterEvent>
#include <QFocusEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QVBoxLayout>

HomeToolCard::HomeToolCard(const ToolDefinition& tool, QWidget* parent)
    : QPushButton(parent)
{
    setObjectName("homeToolCard");
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::StrongFocus);
    setAccessibleName(tool.title);
    setAccessibleDescription(tool.description + QStringLiteral("。") + tool.detail);
    setMinimumSize(260, 176);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setFlat(true);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(20, 16, 20, 16);
    layout->setSpacing(5);
    auto* top = new QHBoxLayout();
    auto* glyph = SnapUi::iconGlyph(tool.icon, 27, this);
    glyph->setFixedSize(38, 38);
    glyph->setAttribute(Qt::WA_TransparentForMouseEvents);
    top->addWidget(glyph);
    top->addStretch();
    auto* arrow = SnapUi::detailText(QStringLiteral("打开  ›"), this);
    arrow->setAttribute(Qt::WA_TransparentForMouseEvents);
    top->addWidget(arrow);
    layout->addLayout(top);

    auto* title = SnapUi::cardTitle(tool.title, this);
    title->setAttribute(Qt::WA_TransparentForMouseEvents);
    layout->addWidget(title);
    auto* summary = SnapUi::detailText(tool.description, this);
    summary->setAttribute(Qt::WA_TransparentForMouseEvents);
    layout->addWidget(summary);
    layout->addStretch();
    m_detail = SnapUi::detailText(tool.detail, this);
    m_detail->setWordWrap(true);
    m_detail->setFixedHeight(38);
    m_detail->setAttribute(Qt::WA_TransparentForMouseEvents);
    layout->addWidget(m_detail);
    updateDetail();

    SnapUi::bindTheme(this, [this] {
        const auto border = SnapUi::colorBorder().name(QColor::HexArgb);
        const auto surface = SnapUi::colorSurface().name(QColor::HexArgb);
        const auto accent = SnapUi::colorPrimary().name(QColor::HexArgb);
        setStyleSheet(QStringLiteral(
            "QPushButton#homeToolCard { background: %1; border: 1px solid %2; border-radius: 9px; text-align: left; }"
            "QPushButton#homeToolCard:hover, QPushButton#homeToolCard:focus { border: 2px solid %3; }"
        ).arg(surface, border, accent));
    });
}

void HomeToolCard::updateDetail()
{
    m_detail->setVisible(m_hovered || hasFocus());
}

void HomeToolCard::enterEvent(QEnterEvent* event)
{
    m_hovered = true;
    updateDetail();
    QPushButton::enterEvent(event);
}

void HomeToolCard::leaveEvent(QEvent* event)
{
    m_hovered = false;
    updateDetail();
    QPushButton::leaveEvent(event);
}

void HomeToolCard::focusInEvent(QFocusEvent* event)
{
    QPushButton::focusInEvent(event);
    updateDetail();
}

void HomeToolCard::focusOutEvent(QFocusEvent* event)
{
    QPushButton::focusOutEvent(event);
    updateDetail();
}
