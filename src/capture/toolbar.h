#ifndef SNAPLENS_CAPTURE_TOOLBAR_H
#define SNAPLENS_CAPTURE_TOOLBAR_H

#include <QWidget>
#include <QToolButton>

class SnipToolbar : public QWidget
{
    Q_OBJECT

public:
    explicit SnipToolbar(double opacity = 1.0, QWidget *parent = nullptr);

signals:
    void actionChosen(const QString &action);

private:
    QToolButton *makeButton(const QString &iconPath,
                            const QString &action,
                            const QString &tooltip);
};

#endif // SNAPLENS_CAPTURE_TOOLBAR_H