#include "application.h"
#include "diagnostics/regression_checks.h"
#include "diagnostics/visual_checks.h"
#include "log/log_manager.h"
#include "ui/snapkit.h"
#include "ElaWidgetTools.hpp"
#include <QDir>
#include <QScreen>

/** @brief 初始化日志、Qt 与主题，然后分派诊断模式或常规应用流程。 */
int main(int argc, char *argv[])
{
    QApplication::setApplicationName("SnapLens");
    QApplication::setOrganizationName("SnapLens");

    LogOptions logOptions;
    for (int i = 1; i < argc; ++i) {
        if (QString::fromLocal8Bit(argv[i]) == "--regression-checks") {
            logOptions.filePath = QDir::current().filePath("regression-trace.log");
            logOptions.truncate = true;
        }
    }
    // 在 QApplication 之前初始化、在其析构之后关闭，覆盖启动与退出诊断。
    LogSession logSession(logOptions);
    qInfo("startup: QApplication");
    QApplication app(argc, argv);
    qInfo("startup: Ela init");
    eApp->init();
    qInfo("startup: theme");
    // 启动即按「跟随系统」初始化主题（含 Qt 原生控件的全局调色板）；
    // 读到用户配置后 AppController 会用实际设置覆盖。
    SnapUi::applyThemeMode(SnapUi::ThemeMode::System);

    // ElaApplication 用「像素字号」设置应用字体（setPixelSize），而 Qt 的现代
    // Windows 样式在 polish() 时会按屏幕 DPI 缩放字体、走 pointSizeF() 换算；
    // 像素字体的 pointSizeF() 恒为 -1，于是风格层每次 polish 都会调用
    // setPointSize(-1) 并打印 "QFont::setPointSize: Point size <= 0 (-1)"。
    // 这里把应用字体换算成一个等效点值（96 DPI 下与像素值等价），
    // 消除告警的同时保留原有的视觉尺寸（Qt 会按 DPI 正确缩放点值）。
    if (QScreen* screen = QGuiApplication::primaryScreen()) {
        QFont appFont = qApp->font();
        if (appFont.pointSizeF() <= 0 && appFont.pixelSize() > 0) {
            appFont.setPointSizeF(appFont.pixelSize() * 72.0
                                  / screen->logicalDotsPerInchY());
            qApp->setFont(appFont);
        }
    }
    app.setQuitOnLastWindowClosed(false);

    if (app.arguments().contains("--regression-checks")) return runRegressionChecks(app);

    if (const auto result = runUiChecks(app)) return *result;

    AppController controller(&app);
    return app.exec();
}
