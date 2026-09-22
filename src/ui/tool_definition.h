#pragma once

#include "ElaWidgetTools.hpp"
#include <QString>
#include <functional>

// A built-in tool contributes metadata and a page factory to the shell.
// Keep service credentials and worker lifetimes out of this presentation contract.
struct ToolDefinition {
    QString id;
    QString title;
    QString description;
    QString detail;
    ElaIconType::IconName icon;
    std::function<QWidget*()> createPage;
};
