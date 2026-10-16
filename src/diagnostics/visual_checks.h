#pragma once
#include <optional>

class QApplication;

/** @brief 执行命令行指定的 UI 冒烟或快照检查，不修改用户配置。
 * @return 诊断退出码；未指定诊断模式时返回 nullopt。
 */
std::optional<int> runUiChecks(QApplication& app);
