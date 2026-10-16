#pragma once
class QApplication;
/** @brief 运行本地功能回归（禁用用户配置写入）；成功返回 0，失败返回 1。 */
int runRegressionChecks(QApplication& app);
