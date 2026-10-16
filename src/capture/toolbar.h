#ifndef SNAPLENS_CAPTURE_TOOLBAR_H
#define SNAPLENS_CAPTURE_TOOLBAR_H

#include <QWidget>
#include "ElaWidgetTools.hpp"

class QPaintEvent;

/// 截图完成后浮在选区旁的迷你工具条。
///
/// 视觉上对齐 Windows 截屏工具的浮动操作条：圆角深色面板 + 悬停高亮 +
/// 分组分隔线。这里刻意使用深色（不跟随应用主题）—— 它悬浮在用户截取的
/// 任意画面之上，浅色面板会融入背景而失去边界感。
class SnipToolbar : public QWidget
{
    Q_OBJECT

public:
    explicit SnipToolbar(double opacity = 1.0, QWidget *parent = nullptr);

signals:
    void actionChosen(const QString &action);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    ElaIconButton *makeButton(ElaIconType::IconName icon,
                            const QString &action,
                            const QString &tooltip,
                            bool accent = false);
    QWidget *makeSeparator();
};

#endif // SNAPLENS_CAPTURE_TOOLBAR_H
