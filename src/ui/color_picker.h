#ifndef SNAPLENS_UI_COLOR_PICKER_H
#define SNAPLENS_UI_COLOR_PICKER_H

#include <QPushButton>
#include <QString>

class ColorPickerButton : public QPushButton
{
    Q_OBJECT

public:
    explicit ColorPickerButton(const QString& hexColor = "#FFFFFF", QWidget* parent = nullptr);

    QString color() const;
    void setColor(const QString& hexColor);

signals:
    void colorChanged(const QString& hexColor);

private slots:
    void pick();

private:
    void updateStyle();
    QString m_hex;
};

#endif