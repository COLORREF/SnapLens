#include "toolbar.h"

#include <QHBoxLayout>
#include <QGraphicsOpacityEffect>
#include <QIcon>
#include <QSize>

#include "log/log_manager.h"

static const QSize  ICON_SIZE(20, 20);
static const QColor TOOLBAR_BG(45, 45, 45);

SnipToolbar::SnipToolbar(double opacity, QWidget *parent)
    : QWidget(parent)
{
    setAutoFillBackground(true);

    QPalette pal = palette();
    pal.setColor(backgroundRole(), TOOLBAR_BG);
    setPalette(pal);

    if (opacity < 1.0) {
        auto *effect = new QGraphicsOpacityEffect(this);
        effect->setOpacity(opacity);
        setGraphicsEffect(effect);
    }

    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(2);

    // 左侧操作按钮
    layout->addWidget(makeButton(QStringLiteral(":/icons/Save"),
                                 QStringLiteral("save"),
                                 QStringLiteral("保存到文件")));
    layout->addWidget(makeButton(QStringLiteral(":/icons/Pushpin"),
                                 QStringLiteral("pin"),
                                 QStringLiteral("钉图置顶")));
    layout->addWidget(makeButton(QStringLiteral(":/icons/Translate"),
                                 QStringLiteral("translate"),
                                 QStringLiteral("AI 翻译图片文字")));
    layout->addWidget(makeButton(QStringLiteral(":/icons/Ocr"),
                                 QStringLiteral("ocr"),
                                 QStringLiteral("OCR 识别文字")));

    // 弹性空间
    layout->addStretch();

    // 右侧：取消 + 确认复制
    layout->addWidget(makeButton(QStringLiteral(":/icons/Close"),
                                 QStringLiteral("cancel"),
                                 QStringLiteral("取消截图")));
    layout->addWidget(makeButton(QStringLiteral(":/icons/Check"),
                                 QStringLiteral("copy"),
                                 QStringLiteral("确认并复制到剪贴板")));
}

QToolButton *SnipToolbar::makeButton(const QString &iconPath,
                                      const QString &action,
                                      const QString &tooltip)
{
    auto *btn = new QToolButton(this);
    btn->setIcon(QIcon(iconPath));
    btn->setIconSize(ICON_SIZE);
    btn->setToolTip(tooltip);
    btn->setFocusPolicy(Qt::NoFocus);
    btn->setCursor(Qt::ArrowCursor);
    btn->setAutoRaise(true);
    connect(btn, &QToolButton::clicked, this, [this, action]() {
        emit actionChosen(action);
    });
    return btn;
}