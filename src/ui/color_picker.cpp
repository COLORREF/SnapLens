#include "color_picker.h"

#include "ElaWidgetTools.hpp"

#include <QColor>

ColorPickerButton::ColorPickerButton(const QString& hexColor, QWidget* parent)
    : QPushButton(parent)
    , m_hex(hexColor)
{
    setCursor(Qt::PointingHandCursor);
    setToolTip(QStringLiteral("点击打开取色器"));
    connect(this, &QPushButton::clicked, this, &ColorPickerButton::pick);
    updateStyle();
}

QString ColorPickerButton::color() const { return m_hex; }

void ColorPickerButton::setColor(const QString& hexColor)
{
    m_hex = hexColor;
    updateStyle();
}

void ColorPickerButton::pick()
{
    QColor current(m_hex);
    if (!current.isValid()) current = QColor(Qt::white);

    // 使用 ElaWidgetTools 自带的取色器（ElaColorDialog），
    // 它自带取色面板 / 色相与明度滑条 / RGB-HSV 数值 / 基本与自定义色板，
    // 且跟随 ElaTheme 深浅主题 —— 比 Qt 原生的 QColorDialog 观感统一。
    ElaColorDialog dialog(this);
    dialog.setCurrentColor(current);
    connect(&dialog, &ElaColorDialog::colorSelected, this, [this](const QColor& selected) {
        if (selected.isValid()) {
            m_hex = selected.name();
            updateStyle();
            emit colorChanged(m_hex);
        }
    });
    dialog.exec();
}

void ColorPickerButton::updateStyle()
{
    // 亮度自适应文字色：深底浅字、浅底深字，保证色值始终可读。
    QColor bg(m_hex);
    if (!bg.isValid()) bg = QColor(Qt::white);
    const double lum = 0.299 * bg.red() + 0.587 * bg.green() + 0.114 * bg.blue();
    const QString textColor = (lum > 150) ? QStringLiteral("#000000")
                                          : QStringLiteral("#FFFFFF");

    setStyleSheet(QStringLiteral(
        "QPushButton {"
        "  background-color: %1;"
        "  color: %2;"
        "  border: 1px solid rgba(128, 128, 128, 140);"
        "  border-radius: 5px;"
        "  min-width: 84px;"
        "  min-height: 24px;"
        "  padding: 0 8px;"
        "  font-family: Consolas, monospace;"
        "}"
        "QPushButton:hover { border-color: rgba(128, 128, 128, 220); }")
        .arg(m_hex, textColor));
    setText(m_hex.toUpper());
}
