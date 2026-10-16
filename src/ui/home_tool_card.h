#pragma once

#include "tool_definition.h"
#include <QPushButton>

class QLabel;

// 首页工具入口：固定卡片尺寸，鼠标悬浮或键盘聚焦时显示补充说明。
class HomeToolCard : public QPushButton
{
public:
    explicit HomeToolCard(const ToolDefinition& tool, QWidget* parent = nullptr);

protected:
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void focusInEvent(QFocusEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;

private:
    void updateDetail();
    QLabel* m_detail = nullptr;
    bool m_hovered = false;
};
