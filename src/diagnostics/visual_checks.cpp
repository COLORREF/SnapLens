#include "visual_checks.h"
#include "settings.h"
#include "notify/notify_manager.h"
#include "ui/main_window.h"
#include "translate/translation_page.h"
#include "ui/settings_view.h"
#include "ui/setup_wizard.h"
#include "ui/snapkit.h"
#include "capture/toolbar.h"
#include "ElaWidgetTools.hpp"

#include <QDir>
#include <QFile>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QThread>
#include <QTimer>

namespace {

/// 让界面稳定并推进动画：ElaWindow 的页面切换是时间驱动的过渡动画，
/// 只跑 processEvents 不消耗真实时间会永远停在旧页面，因此这里按毫秒等待。
void settle(QWidget* widget, int millis = 120)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < millis) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        QThread::msleep(5);
        if (widget) widget->repaint();
    }
}

} // namespace

std::optional<int> runUiChecks(QApplication& app)
{
    // 非持久化的对话框回归检查：自动点击确认/取消及本页重置。
    // 它不调用“恢复全部默认”，因此不会改写用户实际配置。
    if (app.arguments().contains(QStringLiteral("--ui-smoke-dialogs"))) {
        auto* testSettings = new Settings(Settings::defaultsDict());
        testSettings->setPersistenceEnabled(false);
        auto* testNotify = new NotifyManager();
        testNotify->setSettings(testSettings);
        auto* window = new MainWindow(testSettings, testNotify, "translate");
        window->show();
        auto* wizard = new SetupWizard(testNotify);
        wizard->show();

        QTimer::singleShot(200, &app, [=, &app] {
            bool ok = true;
            int failureCode = 0;
            QStringList failures;
            const auto initialTheme = eTheme->getThemeMode();
            QMetaObject::invokeMethod(wizard, "themeChangeButtonClicked");
            if (eTheme->getThemeMode() == initialTheme) {
                ok = false; failureCode |= 1; failures << "向导主题按钮未切换";
            }
            auto* panel = window->findChild<PivotPanel*>();
            if (panel) {
                const bool wasMerged = panel->isMerged();
                auto* tabs = panel->findChild<ElaTabWidget*>();
                if (tabs) QMetaObject::invokeMethod(tabs, "tabBarDoubleClicked",
                                                    Q_ARG(int, 0));
                if (panel->isMerged() == wasMerged) {
                    ok = false; failures << "页签双击未切换合并状态";
                }
            }
            auto clickModal = [&app, &ok, &failures](const QString& choice) {
                QTimer::singleShot(90, &app, [choice, &ok, &failures] {
                    auto* modal = QApplication::activeModalWidget();
                    if (!modal) { ok = false; failures << "没有活动确认弹窗"; return; }
                    if (!qobject_cast<ElaContentDialog*>(modal)) {
                        ok = false; failures << "确认弹窗未使用 ElaContentDialog";
                    }
                    if (modal->windowTitle() == QStringLiteral("确认下载")) {
                        auto* scroll = modal->findChild<QScrollArea*>();
                        if (!scroll || scroll->verticalScrollBar()->maximum() == 0) {
                            ok = false; failures << "长提示文字没有可滚动区域";
                        }
                        modal->grab().save(QStringLiteral(
                            "cmake-build-release/ui-snapshots/download-confirm.png"));
                    }
                    for (auto* button : modal->findChildren<ElaPushButton*>()) {
                        if (button->text() == choice) { button->click(); return; }
                    }
                    ok = false;
                    failures << QString("未找到按钮：%1").arg(choice);
                    modal->close();
                });
            };

            clickModal("取消");
            wizard->reject();
            if (!wizard->isVisible()) { ok = false; failureCode |= 2; failures << "取消后向导关闭"; }
            clickModal("确定");
            wizard->reject();
            if (wizard->isVisible()) { ok = false; failureCode |= 4; failures << "确认后向导仍可见"; }

            window->showSettings();
            settle(window, 450);
            auto* settings = window->settingsView();
            settings->selectPage(0);
            bool foundReset = false;
            for (auto* widget : QApplication::allWidgets()) {
                auto* button = qobject_cast<ElaPushButton*>(widget);
                if (!button || button->text() != QStringLiteral("恢复本页默认")) continue;
                clickModal("确定");
                button->click();
                foundReset = true;
                break;
            }
            if (!foundReset) { ok = false; failureCode |= 8; failures << "未找到本页重置按钮"; }

            const QString longMessage = QStringLiteral("OCR 语言包下载地址：\n")
                + QStringLiteral("https://cdn.jsdelivr.net/gh/tesseract-ocr/tessdata_fast@main/ara.traineddata\n\n")
                + QStringLiteral("安全提示：文件来自第三方 CDN。\n").repeated(24);
            clickModal("取消");
            if (testNotify->confirm("确认下载", longMessage, window)) {
                ok = false; failures << "下载取消返回确定";
                failureCode |= 16;
            }
            clickModal("确定");
            if (!testNotify->confirm("确认下载", longMessage, window)) {
                ok = false; failures << "下载确定返回取消";
                failureCode |= 32;
            }
            if (!failures.isEmpty()) qWarning().noquote() << failures.join("; ");
            app.exit(ok ? 0 : 64 + failureCode);
        });
        return app.exec();
    }

    // 开发者用的视觉回归工具：绕过持久化设置，渲染主窗口、窗口内设置页的
    // 每一页、以及首次运行引导的每一步，随后直接退出，不修改用户数据。
    const QString snapshotPrefix = QStringLiteral("--ui-snapshot-dir=");
    for (const QString& arg : app.arguments()) {
        if (!arg.startsWith(snapshotPrefix)) continue;
        const QString outputDir = QDir::cleanPath(arg.mid(snapshotPrefix.size()));
        QDir().mkpath(outputDir);

        auto* previewSettings = new Settings(Settings::defaultsDict());
        previewSettings->setPersistenceEnabled(false);
        auto* previewNotify = new NotifyManager();
        previewNotify->setSettings(previewSettings);

        auto* previewWindow = new MainWindow(previewSettings, previewNotify, "screenshot");
        previewWindow->show();

        QTimer::singleShot(900, previewWindow, [=] {
            settle(previewWindow, 300);
            previewWindow->grab().save(QDir(outputDir).filePath("main-window.png"));

            // 设置页已是窗口内的一页：切过去后逐页抓图
            previewWindow->showSettings();
            settle(previewWindow, 600);   // 等页面切换动画结束
            auto* nav = previewWindow->settingsView();
            const int pageCount = nav ? nav->pageCount() : 0;
            for (int i = 0; i < pageCount; ++i) {
                if (nav) nav->selectPage(i);
                settle(previewWindow);
                previewWindow->grab().save(
                    QDir(outputDir).filePath(QString("settings-%1.png").arg(i)));
            }

            auto* wizard = new SetupWizard(previewNotify);
            wizard->show();
            settle(wizard, 400);
            const int steps = wizard->stepCount();
            for (int i = 0; i < steps; ++i) {
                wizard->selectStep(i);
                settle(wizard, 250);
                wizard->grab().save(
                    QDir(outputDir).filePath(QString("wizard-%1.png").arg(i)));
            }

            // 再抓一组浅色主题的关键画面，用于核对主题切换
            SnapUi::applyThemeMode(SnapUi::ThemeMode::Light);
            settle(wizard, 300);
            wizard->selectStep(0);
            settle(wizard, 300);
            wizard->grab().save(QDir(outputDir).filePath("wizard-0-light.png"));
            wizard->hide();

            SnipToolbar toolbar(1.0);
            toolbar.show();
            settle(&toolbar, 120);
            toolbar.grab().save(QDir(outputDir).filePath("snip-toolbar.png"));
            toolbar.hide();

            if (nav) nav->selectPage(0);
            settle(previewWindow, 300);
            previewWindow->grab().save(QDir(outputDir).filePath("settings-0-light.png"));

            SnapUi::applyThemeMode(SnapUi::ThemeMode::System);
            QApplication::quit();
        });
        return app.exec();
    }

    return std::nullopt;
}
