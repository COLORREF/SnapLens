#include "color_picker.h"
#include <QColorDialog>
#include <QColor>

ColorPickerButton::ColorPickerButton(const QString& hexColor, QWidget* parent)
    : QPushButton(parent)
    , m_hex(hexColor)
{
    setCursor(Qt::PointingHandCursor);
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
    if (!current.isValid()) current = QColor("#FFFFFF");
    QColor color = QColorDialog::getColor(current, this, "选择颜色");
    if (color.isValid()) {
        m_hex = color.name();
        updateStyle();
        emit colorChanged(m_hex);
    }
}

void ColorPickerButton::updateStyle()
{
    // Luminance-aware text color: dark background → light text, light background → dark text
    QColor bg(m_hex);
    double lum = 0.299 * bg.red() + 0.587 * bg.green() + 0.114 * bg.blue();
    QString textColor = (lum > 150) ? "#000000" : "#FFFFFF";

    setStyleSheet(QString("QPushButton { background-color: %1; color: %2; border: 1px solid #888; min-width: 70px; min-height: 24px; padding: 0 4px; }")
                      .arg(m_hex, textColor));
    setText(m_hex.toUpper());
}