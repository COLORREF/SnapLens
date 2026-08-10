#include "application.h"
#include "log/log_manager.h"

int main(int argc, char *argv[])
{
    QApplication::setApplicationName("SnapLens");
    QApplication::setOrganizationName("SnapLens");

    QApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(false);

    AppController controller(&app);
    if (controller.isCancelled())
    {
        return 0; // 首次运行向导被取消，不进入事件循环
    }

    return app.exec();
}
