#include "regression_checks.h"
#include "../ui/main_window.h"
#include "../settings.h"
#include "../notify/notify_manager.h"
#include "../ui/settings_view.h"
#include "../ocr/ocr_engine.h"
#include <QApplication>
#include <QTimer>
#include <QElapsedTimer>
#include <QThread>
#include <QDir>
#include <QFile>
#include <QPainter>
#include <QTcpServer>
#include <QTcpSocket>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include "../translate/ai_client.h"
#include "../translate/translator.h"
#include "../translate/model_capabilities.h"
#include "../translate/ai_model_selector.h"
#include "../translate/translate_window.h"
#include "../translate/providers.h"
#include "../capture/snip_session.h"
#include "../capture/overlay.h"
#include <QClipboard>
#include <QKeyEvent>
#include <QKeySequenceEdit>
#include "../capture/screen_capture.h"
#include <QSystemTrayIcon>
#include <QTemporaryDir>
#include <QPlainTextEdit>
#include <QComboBox>
#include <QCheckBox>
#include <QLineEdit>
#include <QMouseEvent>
#include <QScrollArea>
#include "../translate/translation_page.h"
#include <leptonica/allheaders.h>

namespace {
class TestCursorProvider : public ICursorProvider {
public:
    QPoint position{80, 80};
    void getPos(int& x, int& y) override { x = position.x(); y = position.y(); }
    bool setPos(int x, int y) override { position = QPoint(x, y); return true; }
};

void settle(int milliseconds = 180)
{
    QElapsedTimer time;
    time.start();
    while (time.elapsed() < milliseconds) {
        QApplication::processEvents();
        QThread::msleep(5);
    }
}
}

int runRegressionChecks(QApplication& app)
{
    Settings settings(Settings::defaultsDict());
    settings.setPersistenceEnabled(false);
    NotifyManager notify;
    notify.setSettings(&settings);
    qInfo("regression: construct window");
    MainWindow window(&settings, &notify);
    qInfo("regression: show window");
    notify.setDefaultParent(&window);
    window.show();
    settle();
    QStringList failures;
    auto check = [&](bool value, const QString& message) { if (!value) failures << message; };
    const QString output = QDir::current().filePath("cmake-build-release-visual-studio/regression");
    QDir().mkpath(output);
    window.grab().save(output + "/home.png");
    window.showSettings("general");
    settle(400);
    window.grab().save(output + "/settings.png");
    check(window.settingsView()->pageCount() == 8, "settings categories missing");

    // 使用主窗口同款导航，保留搜索、折叠后跳转与主题视觉回归入口。
    auto* navigation = window.settingsView()->findChild<ElaNavigationBar*>();
    auto* tree = navigation ? navigation->findChild<ElaNavigationView*>() : nullptr;
    if (tree) {
        check(tree->isAnimated(), "settings tree expansion animation disabled");
        const auto group = tree->model()->index(1, 0);
        const auto groupKey = static_cast<ElaNavigationNode*>(group.internalPointer())->getNodeKey();
        const auto ocr = tree->model()->index(1, 0, group);
        const auto originalTheme = eTheme->getThemeMode();
        for (const auto theme : {ElaThemeType::Dark, ElaThemeType::Light}) {
            eTheme->setThemeMode(theme);
            window.settingsView()->selectCategory("ai");
            settle(300);
            const QPoint center = tree->visualRect(ocr).center();
            QMouseEvent hover(QEvent::MouseMove, center, tree->viewport()->mapToGlobal(center),
                              Qt::NoButton, Qt::NoButton, Qt::NoModifier);
            QApplication::sendEvent(tree->viewport(), &hover);
            settle(40);
            const QString suffix = theme == ElaThemeType::Dark ? "dark" : "light";
            window.grab(QRect(tree->mapTo(&window, QPoint()), tree->size())).save(output + "/sidebar-hover-" + suffix + ".png");
            window.settingsView()->selectCategory("ocr");
            settle(40);
            window.grab(QRect(tree->mapTo(&window, QPoint()), tree->size())).save(output + "/sidebar-selected-" + suffix + ".png");
        }
        eTheme->setThemeMode(originalTheme);
        navigation->collapseNode(groupKey);
        settle(60);
        window.grab(QRect(tree->mapTo(&window, QPoint()), tree->size())).save(output + "/sidebar-collapse-mid.png");
        settle(300);
        check(!tree->isExpanded(group), "settings group did not collapse");
        window.grab(QRect(tree->mapTo(&window, QPoint()), tree->size())).save(output + "/sidebar-collapsed.png");
        for (auto* edit : window.settingsView()->findChildren<QLineEdit*>()) {
            if (edit->placeholderText() != "搜索设置分类") continue;
            edit->setText("OCR");
            check(!tree->isRowHidden(ocr.row(), group) && tree->isRowHidden(0, {}),
                  "settings category search failed");
            edit->clear();
        }
        navigation->collapseNode(groupKey);
        settle(300);
        window.settingsView()->selectCategory("ocr");
        settle(300);
        const auto* model = qobject_cast<ElaNavigationModel*>(tree->model());
        check(tree->isExpanded(group) && model &&
                  model->getSelectedNode() == static_cast<ElaNavigationNode*>(ocr.internalPointer()),
              "category jump did not expand and select target");
        window.settingsView()->selectCategory("general");
    } else {
        check(false, "settings sidebar is not an ElaNavigationBar");
    }

    QMetaObject::invokeMethod(window.settingsView(), "onSave", Qt::DirectConnection);
    check(settings.app_mode == "translate", "saving settings lost startup mode data");
    check(settings.ai_ocr_langs.contains("eng"), "saving unrelated settings lost OCR language selection");
    for (auto* combo : window.settingsView()->findChildren<QComboBox*>()) {
        if (combo->findData("screenshot") >= 0) {
            combo->setCurrentIndex(combo->findData("screenshot"));
            QMetaObject::invokeMethod(window.settingsView(), "onSave", Qt::DirectConnection);
            check(settings.app_mode == "screenshot", "background startup preference was not saved");
            combo->setCurrentIndex(combo->findData("translate"));
            QMetaObject::invokeMethod(window.settingsView(), "onSave", Qt::DirectConnection);
        }
    }

    auto* provider = window.settingsView()->findChild<QComboBox*>("aiProviderCombo");
    auto* apiBase = window.settingsView()->findChild<QLineEdit*>("aiApiBaseEdit");
    auto* apiKey = window.settingsView()->findChild<QLineEdit*>("aiApiKeyEdit");
    auto* customModels = window.settingsView()->findChild<QLineEdit*>("aiCustomModelsEdit");
    auto* model = window.settingsView()->findChild<QComboBox*>("aiModelCombo");
    check(provider && apiBase && apiKey && customModels && model, "custom AI controls missing");
    if (provider && apiBase && apiKey && customModels && model) {
        provider->setCurrentIndex(provider->findData("custom"));
        apiBase->setText("http://127.0.0.1:5001/v1/");
        apiKey->clear();
        customModels->setText("local-a, local-b");
        model->setCurrentText("local-b");
        QMetaObject::invokeMethod(window.settingsView(), "onSave", Qt::DirectConnection);
        check(settings.ai_provider == "custom" && settings.ai_api_base.endsWith("/v1/")
              && settings.ai_model == "local-b" && settings.ai_custom_models.contains("local-a"),
              "custom AI settings were not saved");
        provider->setCurrentIndex(provider->findData("deepseek"));
        provider->setCurrentIndex(provider->findData("custom"));
        check(apiBase->text().endsWith("/v1/") && model->currentText() == "local-b",
              "custom AI values were lost when switching providers");

        // 新增的 AI 参数（思考 / 推理强度 / 停止序列 / 流式用量）应能保存
        // 并按服务商能力显隐。显隐判断需要 AI 页处于当前页，否则整页被判定为隐藏。
        window.settingsView()->selectCategory("ai");
        // 细粒度参数收在「高级设置」折叠区里，显隐断言前先展开。
        auto* advanced = window.settingsView()->findChild<SnapUi::AdvancedSection*>("aiAdvancedSection");
        check(advanced != nullptr, "AI advanced section is missing");
        check(advanced && !advanced->isExpanded(), "advanced section should be collapsed by default");
        if (advanced) {
            // 点击标题行应展开（整行热区，而非仅小箭头）。
            if (auto* header = advanced->findChild<QWidget*>("advancedHeader")) {
                QMouseEvent press(QEvent::MouseButtonPress, QPointF(20, 10),
                                  Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
                QMouseEvent release(QEvent::MouseButtonRelease, QPointF(20, 10),
                                    Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
                QApplication::sendEvent(header, &press);
                QApplication::sendEvent(header, &release);
                check(advanced->isExpanded(), "clicking the header should expand the section");
            }
            advanced->setExpanded(true);
        }
        auto* doSample = window.settingsView()->findChild<QComboBox*>("aiDoSampleCombo");
        auto* thinking = window.settingsView()->findChild<QComboBox*>("aiThinkingCombo");
        auto* effort = window.settingsView()->findChild<QComboBox*>("aiEffortCombo");
        auto* stop = window.settingsView()->findChild<QPlainTextEdit*>("aiStopEdit");
        auto* streamUsage = window.settingsView()->findChild<ElaToggleSwitch*>("aiStreamUsageToggle");
        check(doSample && thinking && effort && stop && streamUsage, "new AI parameter controls missing");
        if (doSample && thinking && effort && stop && streamUsage) {
            // DeepSeek 同时支持思考与推理强度，用它验证保存链路。
            provider->setCurrentIndex(provider->findData("deepseek"));
            thinking->setCurrentIndex(thinking->findData("on"));
            effort->setCurrentIndex(effort->findData("high"));
            stop->setPlainText("END\n###\n\nSTOP");
            streamUsage->setIsToggled(false);
            QMetaObject::invokeMethod(window.settingsView(), "onSave", Qt::DirectConnection);
            check(settings.ai_thinking == "on" && settings.ai_reasoning_effort == "high",
                  "thinking / reasoning effort were not saved");
            check(settings.ai_stop == "END\n###\nSTOP",
                  "stop sequences should be normalized (trim/blank dropped): " + settings.ai_stop);
            check(!settings.ai_stream_usage, "stream usage toggle was not saved");

            // 新模型默认自动；同一端点/模型恢复自己的参数，不把档位映射到相邻值。
            auto* sendTemp = window.settingsView()->findChild<QCheckBox*>("aiSend_temperature");
            auto* extra = window.settingsView()->findChild<QPlainTextEdit*>("aiExtraBody");
            check(sendTemp && extra, "optional parameter controls missing");
            if (sendTemp && extra) {
                sendTemp->setChecked(true);
                extra->setPlainText(R"({"provider_extension":{"mode":"custom"}})");
                effort->setCurrentText("vendor-ultra");
                const QString firstModel = model->currentText();
                model->setCurrentText("unlisted-model");
                check(!sendTemp->isChecked() && extra->toPlainText().isEmpty()
                      && effort->currentText() == "auto", "new model inherited parameters");
                model->setCurrentText(firstModel);
                check(sendTemp->isChecked() && effort->currentText() == "vendor-ultra"
                      && extra->toPlainText().contains("provider_extension"),
                      "model profile did not restore explicit options");
                QMetaObject::invokeMethod(window.settingsView(), "onSave", Qt::DirectConnection);
                QMetaObject::invokeMethod(window.settingsView(), "onRevert", Qt::DirectConnection);
                check(sendTemp->isChecked() && effort->currentText() == "vendor-ultra",
                      "model profile did not survive save/reload");
                check(AiRequestOptions::fromSettings(settings).enabledParameters.contains("temperature"),
                      "runtime snapshot did not use saved profile");
                Settings otherEndpoint(settings.toDict());
                otherEndpoint.ai_api_base = "http://different.invalid/v1";
                check(AiRequestOptions::fromSettings(otherEndpoint).enabledParameters.isEmpty(),
                      "runtime snapshot leaked profile to another endpoint");
                Settings sameEndpoint(settings.toDict());
                sameEndpoint.ai_api_base += '/';
                check(AiRequestOptions::fromSettings(sameEndpoint).enabledParameters.contains("temperature"),
                      "trailing slash changed profile identity");
                settle();
                window.grab().save(output + "/ai-parameters.png");
                for (auto* scroll : window.settingsView()->findChildren<QScrollArea*>()) {
                    if (!scroll->isAncestorOf(extra)) continue;
                    scroll->ensureWidgetVisible(extra);
                    settle();
                    window.grab().save(output + "/ai-parameters-advanced.png");
                    break;
                }
                model->setCurrentText("unlisted-model");
                check(!sendTemp->isChecked(), "saved new model is not automatic");
                model->setCurrentText(firstModel);
            }
            provider->setCurrentIndex(provider->findData("custom"));
        }
    }

    int dialogs = 0;
    QTimer cancelTimer;
    QObject::connect(&cancelTimer, &QTimer::timeout, &window, [&] {
        if (auto* dialog = qobject_cast<CloseDialog*>(QApplication::activeModalWidget())) {
            ++dialogs;
            dialog->grab().save(output + "/close.png");
            check(dialog->height() < 400, "close dialog unexpectedly tall");
            dialog->reject();
        }
    });
    cancelTimer.start(30);
    auto* bar = window.findChild<ElaAppBar*>(QString(), Qt::FindDirectChildrenOnly);
    check(bar != nullptr, "main app bar missing");
    if (bar) QMetaObject::invokeMethod(bar, "onCloseButtonClicked", Qt::DirectConnection);
    settle(500);
    check(dialogs == 1, "titlebar cancel must show exactly one dialog");
    check(window.isVisible(), "cancel hid the main window");
    window.close();
    settle(400);
    check(dialogs == 2, "native close must show one more dialog");
    cancelTimer.stop();

    int quitCount = 0;
    QObject::connect(&window, &MainWindow::quitRequested, &window, [&] { ++quitCount; });
    settings.close_to_tray = false;
    window.close();
    settle();
    check(quitCount == 1, "exit must dispatch once without another dialog");
    settings.close_to_tray = true;
    window.close();
    settle();
    if (QSystemTrayIcon::isSystemTrayAvailable()) check(!window.isVisible(), "tray close did not hide window");
    check(quitCount == 1, "tray close requested exit");

    // Saving into an invalid directory must report failure without replacing a file.
    QTemporaryDir temp;
    Settings disk(Settings::defaultsDict());
    QFile blocker(temp.filePath("file"));
    check(blocker.open(QIODevice::WriteOnly), "cannot create persistence test fixture");
    blocker.write("unchanged");
    blocker.close();
    disk.config_dir = blocker.fileName() + "/invalid";
    check(!disk.save(), "failed settings save reported success");

    ScreenShot shot;
    shot.pixmap = QPixmap(200, 100);
    shot.pixmap.fill(Qt::white);
    shot.devicePixelRatio = 2.0;
    check(CapturedDesktop::crop(shot, QRect(500, 500, 10, 10)).isNull(), "out-of-bounds crop exposed entire screen");
    check(CapturedDesktop::crop(shot, QRect(0, 0, 20, 10)).size() == QSize(40, 20), "high DPI crop dimensions incorrect");

    {
        Settings defaults(Settings::defaultsDict());
        check(defaults.coord_label_bg_alpha == 67 && defaults.cursor_keyboard_enabled
              && defaults.cursor_keyboard_mode == "arrows", "keyboard cursor defaults incorrect");
        for (qreal scale : {1.0, 1.25, 1.5, 2.0}) {
            for (const QString& mode : {QString("arrows"), QString("wasd"), QString("both")}) {
                TestCursorProvider cursor;
                Settings config(Settings::defaultsDict());
                config.cursor_keyboard_mode = mode;
                ScreenShot fixture;
                fixture.geometry = QRect(0, 0, 640, 480);
                fixture.devicePixelRatio = scale;
                QImage pixels(qRound(640 * scale), qRound(480 * scale), QImage::Format_RGB32);
                pixels.fill(Qt::white);
                pixels.setPixelColor(81, 80, QColor(17, 34, 51));
                fixture.pixmap = QPixmap::fromImage(pixels);
                SnipOverlay overlay(fixture, {}, &cursor, &config);
                auto press = [&](int code, Qt::KeyboardModifiers mods = Qt::NoModifier, bool repeat = false) {
                    QKeyEvent event(QEvent::KeyPress, code, mods, {}, repeat);
                    QApplication::sendEvent(&overlay, &event);
                };
                const int right = mode == "wasd" ? Qt::Key_D : Qt::Key_Right;
                press(right);
                check(cursor.position == QPoint(81, 80), "cursor did not move one physical pixel");
                if (scale == 1.5 && mode == "both") {
                    const QString savedClipboard = QApplication::clipboard()->text();
                    press(Qt::Key_C);
                    settle(80);
                    check(QApplication::clipboard()->text() == "rgb(17, 34, 51)",
                          "color sampling rounded away a single physical pixel at 150 percent DPI");
                    overlay.setAttribute(Qt::WA_UnderMouse, true);
                    overlay.grab().save(output + "/cursor-keyboard-150-percent.png");
                    QApplication::clipboard()->setText(savedClipboard);
                    settle(30);
                }
                press(right, Qt::NoModifier, true);
                press(right, Qt::ControlModifier);
                check(cursor.position == QPoint(81, 80), "repeat or modified key moved cursor");
                if (mode != "both") {
                    press(mode == "wasd" ? Qt::Key_Right : Qt::Key_D);
                    check(cursor.position == QPoint(81, 80), "disabled cursor key group still active");
                }
                if (mode == "both") {
                    press(Qt::Key_A);
                    check(cursor.position == QPoint(80, 80), "WASD failed in both mode");
                    press(Qt::Key_Right);
                }
                press(mode == "wasd" ? Qt::Key_W : Qt::Key_Up);
                press(mode == "wasd" ? Qt::Key_A : Qt::Key_Left);
                press(mode == "wasd" ? Qt::Key_S : Qt::Key_Down);
                check(cursor.position == QPoint(80, 80), "cursor directional round trip failed");
                config.cursor_keyboard_enabled = false;
                SnipOverlay disabled(fixture, {}, &cursor, &config);
                QKeyEvent event(QEvent::KeyPress, right, Qt::NoModifier);
                QApplication::sendEvent(&disabled, &event);
                check(cursor.position == QPoint(80, 80), "disabled keyboard movement still moved cursor");
            }
        }
        auto* mode = window.settingsView()->findChild<QComboBox*>("cursorKeyboardMode");
        auto* enabled = window.settingsView()->findChild<ElaToggleSwitch*>("cursorKeyboardEnabled");
        check(mode && enabled, "keyboard cursor settings missing");
        if (mode && enabled) {
            mode->setCurrentIndex(2);
            enabled->setIsToggled(false);
            QMetaObject::invokeMethod(window.settingsView(), "onSave", Qt::DirectConnection);
            Settings restored(settings.toDict());
            check(!restored.cursor_keyboard_enabled && restored.cursor_keyboard_mode == "both",
                  "keyboard cursor preferences did not persist");
            mode->setCurrentIndex(0);
            QMetaObject::invokeMethod(window.settingsView(), "onRevert", Qt::DirectConnection);
            check(mode->currentIndex() == 2 && !mode->isEnabled(), "keyboard cursor preferences did not reload");
        }
    }

    {
        ScreenShot colorShot;
        colorShot.geometry = QRect(0, 0, 800, 600);
        colorShot.pixmap = QPixmap(800, 600);
        colorShot.pixmap.fill(QColor(26, 43, 60));
        Settings colorSettings(Settings::defaultsDict());
        colorSettings.copy_color_key = "Alt+C";
        SnipOverlay overlay(colorShot, {}, nullptr, &colorSettings);
        const QString previousClipboard = QApplication::clipboard()->text();
        auto key = [&](QEvent::Type type, int code, Qt::KeyboardModifiers modifiers, bool repeat = false) {
            QKeyEvent event(type, code, modifiers, {}, repeat);
            QApplication::sendEvent(&overlay, &event);
            settle(30);
        };
        key(QEvent::KeyPress, Qt::Key_C, Qt::AltModifier);
        check(QApplication::clipboard()->text() == "rgb(26, 43, 60)", "custom color copy shortcut failed");
        key(QEvent::KeyPress, Qt::Key_Shift, Qt::ShiftModifier);
        key(QEvent::KeyRelease, Qt::Key_Shift, Qt::NoModifier);
        key(QEvent::KeyPress, Qt::Key_C, Qt::AltModifier);
        check(QApplication::clipboard()->text() == "#1a2b3c", "Shift did not switch copied color to Hex");
        key(QEvent::KeyPress, Qt::Key_Shift, Qt::ShiftModifier);
        key(QEvent::KeyPress, Qt::Key_C, Qt::ShiftModifier);
        key(QEvent::KeyRelease, Qt::Key_Shift, Qt::NoModifier);
        key(QEvent::KeyPress, Qt::Key_C, Qt::AltModifier);
        check(QApplication::clipboard()->text() == "#1a2b3c", "Shift chord unexpectedly toggled color format");
        key(QEvent::KeyPress, Qt::Key_Shift, Qt::ShiftModifier);
        key(QEvent::KeyRelease, Qt::Key_Shift, Qt::NoModifier, true);
        key(QEvent::KeyPress, Qt::Key_Shift, Qt::ShiftModifier, true);
        key(QEvent::KeyRelease, Qt::Key_Shift, Qt::NoModifier);
        key(QEvent::KeyPress, Qt::Key_C, Qt::AltModifier);
        check(QApplication::clipboard()->text() == "rgb(26, 43, 60)", "Shift auto-repeat toggled more than once");
        overlay.setAttribute(Qt::WA_UnderMouse, true);
        overlay.grab().save(output + "/color-label-rgb.png");
        key(QEvent::KeyPress, Qt::Key_Shift, Qt::ShiftModifier);
        key(QEvent::KeyRelease, Qt::Key_Shift, Qt::NoModifier);
        overlay.grab().save(output + "/color-label-hex.png");
        check(colorSettings.color_format == "rgb", "temporary color format switch changed saved default");
        check(colorSettings.color_label_bg_alpha == 67 && colorSettings.color_swatch_enabled,
              "color label defaults must use 67 percent opacity and show swatch");
        colorSettings.color_format_key = "F2";
        SnipOverlay customOverlay(colorShot, {}, nullptr, &colorSettings);
        QKeyEvent customToggle(QEvent::KeyPress, Qt::Key_F2, Qt::NoModifier);
        QApplication::sendEvent(&customOverlay, &customToggle);
        QKeyEvent copyColor(QEvent::KeyPress, Qt::Key_C, Qt::AltModifier);
        QApplication::sendEvent(&customOverlay, &copyColor);
        settle(80);
        check(QApplication::clipboard()->text() == "#1a2b3c", "custom color format shortcut failed: " + QApplication::clipboard()->text());
        QKeyEvent oldShiftPress(QEvent::KeyPress, Qt::Key_Shift, Qt::ShiftModifier);
        QKeyEvent oldShiftRelease(QEvent::KeyRelease, Qt::Key_Shift, Qt::NoModifier);
        QApplication::sendEvent(&customOverlay, &oldShiftPress);
        QApplication::sendEvent(&customOverlay, &oldShiftRelease);
        settle(30);
        QApplication::sendEvent(&customOverlay, &copyColor);
        settle(80);
        check(QApplication::clipboard()->text() == "#1a2b3c", "replaced Shift shortcut still toggles format: " + QApplication::clipboard()->text());
        for (double zoom : {4.0, 7.0, 9.5, 10.0}) {
            for (bool swatch : {true, false}) {
                colorSettings.magnifier_zoom = zoom;
                colorSettings.color_swatch_enabled = swatch;
                SnipOverlay preview(colorShot, {}, nullptr, &colorSettings);
                preview.setAttribute(Qt::WA_UnderMouse, true);
                preview.grab().save(output + QString("/color-layout-%1-%2.png").arg(zoom).arg(swatch));
            }
        }
        auto* mode = window.settingsView()->findChild<QComboBox*>("colorFormatKeyMode");
        auto* formatEdit = window.settingsView()->findChild<QKeySequenceEdit*>("colorFormatKeyEdit");
        check(mode && formatEdit, "color format shortcut settings missing");
        if (mode && formatEdit) {
            mode->setCurrentIndex(1);
            formatEdit->setKeySequence(QKeySequence("F2"));
            QMetaObject::invokeMethod(window.settingsView(), "onSave", Qt::DirectConnection);
            check(Settings(settings.toDict()).color_format_key == "F2", "custom color shortcut was not saved");
            mode->setCurrentIndex(0);
            QMetaObject::invokeMethod(window.settingsView(), "onRevert", Qt::DirectConnection);
            check(mode->currentIndex() == 1 && formatEdit->keySequence() == QKeySequence("F2"),
                  "custom color shortcut was not restored");
        }
        QApplication::clipboard()->setText(previousClipboard);
    }

    {
        SnipSession session(nullptr, nullptr, nullptr, nullptr, nullptr, &settings);
        int finished = 0;
        QObject::connect(&session, &SnipSession::finished, &window, [&] { ++finished; });
        check(session.start(), "screen capture session could not start");
        session.close();
        session.close();
        check(finished == 1, "capture close must finish exactly once");
    }

    // Exercise both encoder and decoder with the OCR SDK linked into this process.
    for (const auto& format : {"PNG", "JPG", "BMP"}) {
        const QString file = temp.filePath(QString("image.%1").arg(QString::fromLatin1(format).toLower()));
        check(shot.pixmap.save(file, format), QString("image save failed: %1").arg(format));
        check(QImage(file).size() == shot.pixmap.size(), QString("image read failed: %1").arg(format));
    }

    // Crossing the Qt / Leptonica boundary catches ABI mismatches on either side.
    QFile jpegFixture(temp.filePath("image.jpg"));
    check(jpegFixture.open(QIODevice::ReadOnly), "cannot read JPEG fixture");
    const QByteArray jpegBytes = jpegFixture.readAll();
    PIX* leptImage = pixReadMemJpeg(reinterpret_cast<const l_uint8*>(jpegBytes.constData()),
                                  size_t(jpegBytes.size()), 0, 1, nullptr, 0);
    check(leptImage != nullptr, "Leptonica cannot decode Qt JPEG (possible ABI mismatch)");
    if (leptImage) {
        l_uint8* encoded = nullptr;
        size_t encodedSize = 0;
        const int result = pixWriteMemJpeg(&encoded, &encodedSize, leptImage, 90, 1);
        check(result == 0 && encoded != nullptr, "Leptonica JPEG encoding failed");
        if (encoded) {
            const QImage decoded = QImage::fromData(encoded, int(encodedSize), "JPG");
            check(decoded.size() == shot.pixmap.size(), "Qt cannot decode Leptonica progressive JPEG");
            lept_free(encoded);
        }
        pixDestroy(&leptImage);
    }

    QTcpServer server;
    check(server.listen(QHostAddress::LocalHost), "mock HTTP server could not listen");
    QByteArray payload = R"({"choices":[{"message":{"content":"hello","reasoning_content":"reason"}}]})";
    int status = 200;
    bool stall = false;
    int responseDelayMs = 0;
    QList<QByteArray> pacedChunks;
    const int chunkIntervalMs = 650;
    QByteArray lastRequest;
    QObject::connect(&server, &QTcpServer::newConnection, &window, [&] {
        while (auto* socket = server.nextPendingConnection()) {
            QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            QObject::connect(socket, &QTcpSocket::readyRead, socket, [&, socket] {
                lastRequest = socket->readAll();
                if (socket->property("sent").toBool() || stall) return;
                socket->setProperty("sent", true);
                QByteArray response = payload;
                if (!pacedChunks.isEmpty()) {
                    response.clear();
                    for (const auto& chunk : pacedChunks) response += chunk;
                }
                const QByteArray header = "HTTP/1.1 " + QByteArray::number(status)
                    + " Test\r\nContent-Type: application/json\r\nContent-Length: "
                    + QByteArray::number(response.size()) + "\r\nConnection: close\r\n\r\n";
                if (!pacedChunks.isEmpty()) {
                    socket->write(header);
                    for (int i = 0; i < pacedChunks.size(); ++i) {
                        const auto chunk = pacedChunks[i];
                        const bool last = i == pacedChunks.size() - 1;
                        QTimer::singleShot((i + 1) * chunkIntervalMs, socket, [socket, chunk, last] {
                            socket->write(chunk);
                            if (last) socket->disconnectFromHost();
                        });
                    }
                } else {
                    QTimer::singleShot(responseDelayMs, socket, [socket, header, response] {
                        socket->write(header + response);
                        socket->disconnectFromHost();
                    });
                }
            });
        }
    });
    const QString base = QString("http://127.0.0.1:%1/v1").arg(server.serverPort());
    ApiClient api;
    const QJsonArray messages{QJsonObject{{"role", "user"}, {"content", "fixture"}}};
    auto makeParams = [&](const QString& key, const QString& url,
                          const QString& provider = QStringLiteral("custom")) {
        ApiClient::ChatParams params;
        params.apiKey = key;
        params.apiBase = url;
        params.model = QStringLiteral("fixture-model");
        params.provider = provider;
        params.messages = messages;
        params.timeoutSecs = 2;
        return params;
    };
    auto chat = api.chatCreate(makeParams("fixture-key", base));
    check(chat.errorCode == 0 && chat.content == "hello" && chat.thinking == "reason", "JSON chat response failed: " + chat.errorMsg);
    chat = api.chatCreate(makeParams({}, base + '/'));
    check(chat.errorCode == 0 && chat.content == "hello", "custom local endpoint without key failed: " + chat.errorMsg);
    check(lastRequest.startsWith("POST /v1/chat/completions HTTP/")
          && !lastRequest.toLower().contains("authorization:"), "custom URL or optional auth header was incorrect");
    // Inference may stay silent after upload; connection timeout is not a wall-clock generation limit.
    auto slowParams = makeParams({}, base);
    slowParams.timeoutSecs = 1;
    responseDelayMs = 1600;
    chat = api.chatCreate(slowParams);
    check(chat.errorCode == 0 && chat.content == "hello", "silent reasoning exceeded connection timeout: " + chat.errorMsg);
    responseDelayMs = 0;
    pacedChunks = {": keep-alive\n\n",
        "data: {\"choices\":[{\"delta\":{\"reasoning_content\":\"still thinking\"}}]}\n\n",
        "data: {\"choices\":[{\"delta\":{\"content\":\"answer\"}}]}\n\n", "data: [DONE]\n\n"};
    slowParams.responseIdleTimeoutSecs = 1;
    int pacedFinals = 0;
    QString pacedThinking, pacedContent;
    const int pacedCode = api.chatCreateStream(slowParams,
        [&](const QString& content, const QString& reasoning, bool final, int, const QString&) {
            if (final) ++pacedFinals;
            pacedThinking = reasoning;
            pacedContent = content;
        });
    check(pacedCode == 0 && pacedFinals == 1 && pacedThinking == "still thinking" && pacedContent == "answer",
          "SSE heartbeats/thinking did not keep the response alive beyond the connection timeout");
    pacedChunks.clear();
    stall = true;
    chat = api.chatCreate(slowParams);
    check(chat.errorCode == -2 && chat.errorMsg.contains("响应空闲超时"), "configured response idle deadline did not abort a stalled response");
    slowParams.responseIdleTimeoutSecs = 0;
    std::atomic<int> slowCancel{0};
    slowParams.cancelFlag = &slowCancel;
    QTimer slowCancelTimer;
    slowCancelTimer.setSingleShot(true);
    QObject::connect(&slowCancelTimer, &QTimer::timeout, &window, [&] { slowCancel.store(1); });
    slowCancelTimer.start(1400);
    chat = api.chatCreate(slowParams);
    check(chat.errorCode == -3, "unlimited silent inference could not be cancelled or was prematurely timed out");
    check(api.listModels({}, base, 1).errorCode == -2, "model discovery lost its bounded deadline");
    stall = false;

    payload = R"({"data":[{"id":"local-model","context_length":32000,"supported_parameters":["temperature"]}]})";
    auto models = api.listModels({}, base + '/', 2);
    check(models.errorCode == 0 && models.modelIds.contains("local-model"), "custom model list without key failed");
    check(lastRequest.startsWith("GET /v1/models HTTP/"), "custom model list URL had an incorrect path");
    check(models.metadata["local-model"].toObject()["context_length"].toInt() == 32000,
          "synchronous model discovery discarded metadata");
    bool discoveryFinished = false;
    api.listModelsAsync({}, base, 2, [&](const ApiClient::ModelsResult& value) {
        check(value.metadata["local-model"].toObject()["context_length"].toInt() == 32000,
              "asynchronous model discovery discarded metadata");
        discoveryFinished = true;
    });
    QElapsedTimer discoveryDeadline;
    discoveryDeadline.start();
    while (!discoveryFinished && discoveryDeadline.elapsed() < 3000) settle(10);
    check(discoveryFinished, "asynchronous model discovery did not complete");

    // Capability declarations remain route-specific, optional and separate from user intent.
    auto json = [](const QByteArray& text) { return QJsonDocument::fromJson(text).object(); };
    const auto richMetadata = json(R"({"id":"local-model","limit":{"context":64000,"input":60000,"output":4096},
        "supports_reasoning":true,"reasoningLevels":["low","vendor-deep","low"],"reasoningDefaultLevel":"vendor-deep",
        "supported_parameters":["enable_thinking","reasoning_effort","max_completion_tokens"]})");
    const auto caps = ModelCapabilities::fromMetadata(richMetadata);
    check(caps.reasoning == true && caps.effortChoices() == QStringList{"low", "vendor-deep"}
          && caps.defaultEffort == "vendor-deep" && caps.contextWindow == 64000
          && caps.maxInputTokens == 60000 && caps.maxOutputTokens == 4096,
          "model capabilities were not normalized accurately");
    check(caps.thinkingFormat == "enable_thinking" && !caps.thinkingModes
          && caps.outputField == "max_completion_tokens", "field hints fabricated switch support");
    // DeepSeek's live /models response nests its effort declaration under "effort".
    const auto deepseek = ModelCapabilities::fromMetadata(json(R"({"id":"deepseek-v4-pro",
        "context_window":1048576,"max_output_tokens":393216,
        "effort":{"supported_levels":["low","high","max"],"default_level":"high"}})"));
    check(deepseek.efforts && deepseek.effortChoices() == QStringList{"low", "high", "max"}
          && deepseek.defaultEffort == "high" && deepseek.contextWindow == 1048576
          && deepseek.maxOutputTokens == 393216,
          "DeepSeek effort declaration or capacity was not parsed");
    const auto glmListEntry = ModelCapabilities::fromMetadata(json(R"({"id":"glm-5.3",
        "object":"model","created":1,"owned_by":"glm"})"));
    check(!glmListEntry.efforts && glmListEntry.contextWindow == 0,
          "GLM's ID-only model list was treated as an upstream capability declaration");
    const auto glmDocumented = ModelCapabilities::documentedFallback(
        "https://open.bigmodel.cn/api/paas/v4/", "glm-5.3");
    check(glmDocumented && glmDocumented->effortChoices() == QStringList{"low", "high", "max"}
          && glmDocumented->defaultEffort == "max" && glmDocumented->thinkingModes
          && *glmDocumented->thinkingModes == QStringList{"on"},
          "documented GLM-5.3 fallback is missing or incorrectly scoped");
    check(!ModelCapabilities::documentedFallback("https://proxy.example/v1", "glm-5.3")
          && !ModelCapabilities::documentedFallback("https://open.bigmodel.cn/api/paas/v4", "glm-5.3-flashx")
          && !ModelCapabilities::documentedFallback("https://open.bigmodel.cn/api/paas/v4/other", "glm-5.3"),
          "documented GLM suggestions leaked to a proxy, unknown variant or another route");
    const auto unknown = ModelCapabilities::fromMetadata({});
    check(!unknown.reasoning && !unknown.efforts && !unknown.parameters
          && unknown.contextWindow == 0 && unknown.effortChoices().contains("high"),
          "unknown capabilities did not retain fallback suggestions");
    const auto disabled = ModelCapabilities::fromMetadata(json(R"({"supports_reasoning":false,"reasoningLevels":[]})"));
    check(disabled.reasoning == false && disabled.efforts && disabled.effortChoices().isEmpty(),
          "explicit unsupported/empty capabilities treated as unknown");
    const auto malformed = ModelCapabilities::fromMetadata(json(R"({"context_length":-1,"contextWindow":20000,
        "max_output_tokens":1.5,"supports_reasoning":"false","reasoningLevels":[null,3]})"));
    check(malformed.contextWindow == 20000 && malformed.maxOutputTokens == 0
          && !malformed.reasoning && !malformed.efforts, "malformed metadata became valid capability data");
    const auto inputOnly = ModelCapabilities::fromMetadata(json(R"({"max_input_tokens":32000})"));
    check(inputOnly.maxInputTokens == 32000 && inputOnly.contextWindow == 0,
          "input token limit was mislabeled as the combined context window");
    check(ModelCapabilities::fromMetadata(json(R"({"reasoning":{"efforts":[{"id":"tiny"},{"id":"full"}]}})"))
              .effortChoices() == QStringList{"tiny", "full"}, "adapter-owned effort IDs were remapped");
    check(ModelCapabilities::fromMetadata(json(R"({"reasoning_options":[{"type":"effort","values":[null,"minimal","null","high"]}]})"))
              .effortChoices() == QStringList{"minimal", "high"}, "catalog effort options were ignored");
    const auto fixed = ModelCapabilities::fromMetadata(json(R"({"parameter_schema":{"properties":{
        "enable_thinking":{"type":"boolean","const":true}}}})"));
    check(fixed.thinkingModes && *fixed.thinkingModes == QStringList{"on"}, "fixed thinking flag advertised an off switch");
    const auto budget = ModelCapabilities::fromMetadata(json(R"({"supported_parameters":["thinking"],
        "parameter_schema":{"properties":{"thinking":{"required":["type","budget_tokens"],
        "properties":{"type":{"enum":["enabled","disabled"]}}}}}})"));
    check(budget.thinkingFormat.isEmpty() && !budget.thinkingModes, "budget-based thinking guessed as a simple switch");
    const auto mapped = ApiClient::parseModels(json(R"({"models":{"wire-id":{"id":"catalog-id","limit":{"context":128000}}}})"));
    check(mapped.errorCode == 0 && mapped.modelIds == QStringList{"wire-id"}
          && mapped.metadata["wire-id"].toObject()["id"] == "wire-id", "enriched model map lost endpoint-facing identity");
    check(ApiClient::parseModels(json(R"({"error":"not a catalog"})")).errorCode != 0,
          "invalid catalog was accepted as a successful empty list");
    const QJsonObject cache{{providerEndpoint(base, "models").toString(),
        QJsonObject{{"models", QJsonObject{{"local-model", richMetadata}}}}}};
    check(ModelCapabilities::cachedModel(cache, base + '/', "local-model") == richMetadata
          && ModelCapabilities::cachedModel(cache, base, "another").isEmpty()
          && ModelCapabilities::cachedModel(cache, base + "/other", "local-model").isEmpty(),
          "capability cache leaked across endpoint/model boundaries");

    {
        Settings localSettings(Settings::defaultsDict());
        localSettings.ai_provider = "custom";
        localSettings.ai_api_base = base;
        localSettings.ai_model = "model-a";
        localSettings.ai_custom_models = "model-a,model-b";
        localSettings.ai_model_profiles = QString::fromUtf8(QJsonDocument(QJsonObject{
            {aiModelProfileKey(base, "model-a"), QJsonObject{
                {"ai_reasoning_effort", "high"}, {"ai_thinking_format", "thinking"},
                {"ai_extra_body", "{\"reasoning_effort\":\"max\",\"thinking\":{\"type\":\"enabled\"},\"vendor_flag\":true}"}}},
            {aiModelProfileKey(base, "model-b"), QJsonObject{{"ai_reasoning_effort", "low"}}}
        }).toJson(QJsonDocument::Compact));
        const auto before = localSettings.toDict();
        AiModelSelector selector(&localSettings);
        auto* quickModel = selector.findChild<QComboBox*>("translationModel");
        auto* quickEffort = selector.findChild<QComboBox*>("translationEffort");
        auto* quickThinking = selector.findChild<QComboBox*>("translationThinking");
        check(quickModel && quickEffort && quickThinking, "shared translation AI controls missing");
        if (quickModel && quickEffort && quickThinking) {
            QString quickError;
            auto body = ApiClient::buildRequestBody(selector.snapshot().toChatParams(messages), false, &quickError);
            check(quickError.isEmpty() && body["reasoning_effort"] == "max", "inherited quick selection lost advanced model profile");
            quickEffort->setCurrentText("low");
            quickThinking->setCurrentIndex(quickThinking->findData("auto"));
            body = ApiClient::buildRequestBody(selector.snapshot().toChatParams(messages), false, &quickError);
            check(quickError.isEmpty() && body["reasoning_effort"] == "low" && !body.contains("thinking")
                  && body["vendor_flag"].toBool(), "quick choices failed to override only matching advanced fields");
            quickModel->setCurrentText("model-b");
            body = ApiClient::buildRequestBody(selector.snapshot().toChatParams(messages), false, &quickError);
            check(body["model"] == "model-b" && body["reasoning_effort"] == "low" && !body.contains("vendor_flag"),
                  "switching quick model leaked another model's advanced parameters");
            quickModel->setCurrentText("model-a");
            check(selector.snapshot().reasoningEffort == "low", "returning to model lost workspace selection");
            check(localSettings.toDict() == before, "quick translation settings modified global preferences");
        }
    }

    // End-to-end settings discovery: known choices, preserved custom value, failure fallback and saved runtime snapshot.
    if (provider && apiBase && model) {
        window.show(); // Earlier close-to-tray checks intentionally hid the window.
        window.showSettings("ai");
        settle();
        provider->setCurrentIndex(provider->findData("custom"));
        apiBase->setText(base);
        model->setCurrentText("local-model");
        auto* effort = window.settingsView()->findChild<QComboBox*>("aiEffortCombo");
        auto* thinking = window.settingsView()->findChild<QComboBox*>("aiThinkingCombo");
        payload = QJsonDocument(QJsonObject{{"data", QJsonArray{richMetadata,
            QJsonObject{{"id", "plain"}, {"supports_reasoning", false}}}}}).toJson();
        auto fetch = [&] {
            lastRequest.clear();
            QMetaObject::invokeMethod(window.settingsView(), "onFetchModels", Qt::DirectConnection);
            settle(350);
            check(lastRequest.startsWith("GET /v1/models"), "settings discovery did not reach the fixture");
        };
        if (effort && thinking) {
            effort->setCurrentText("custom-retained");
            fetch();
            check(effort->findText("vendor-deep") >= 0 && effort->findText("high") < 0
                  && effort->currentText() == "custom-retained", "discovery failed to update suggestions or replaced explicit effort");
            model->setCurrentText("plain");
            check(effort->count() == 1 && effort->currentText() == "auto" && thinking->count() == 1,
                  "negative capability did not update suggestions for another model");
            model->setCurrentText("local-model");
            check(effort->currentText() == "custom-retained", "switching models lost saved custom effort");
            status = 503;
            payload = R"({"error":{"message":"fixture discovery unavailable"}})";
            fetch();
            check(effort->findText("vendor-deep") >= 0 && effort->currentText() == "custom-retained",
                  "failed refresh discarded known capabilities or changed selection");
            status = 200;
            QMetaObject::invokeMethod(window.settingsView(), "onSave", Qt::DirectConnection);
            const auto options = AiRequestOptions::fromSettings(settings);
            check(options.modelMetadata == richMetadata, "runtime did not receive the exact model metadata snapshot");
            for (auto* scroll : window.settingsView()->findChildren<QScrollArea*>()) {
                if (!scroll->isAncestorOf(effort)) continue;
                scroll->ensureWidgetVisible(effort);
                settle();
                window.grab().save(output + "/ai-capabilities.png");
                break;
            }
            QMetaObject::invokeMethod(window.settingsView(), "onRevert", Qt::DirectConnection);
            check(effort->findText("vendor-deep") >= 0 && effort->currentText() == "custom-retained",
                  "capability cache and explicit effort did not survive settings reload");
        }
    }

    // Optional values: absence differs from explicit zero, across all provider presets.
    for (const QString& pid : {"custom", "openai", "kimi", "glm", "deepseek"}) {
        auto p = makeParams("k", base, pid);
        p.modelMetadata = richMetadata;
        QString error;
        const auto body = ApiClient::buildRequestBody(p, true, &error);
        check(error.isEmpty() && body.size() == 3 && !body.contains("stream_options"),
              "automatic request must contain only model/messages/stream: " + pid);
    }
    auto explicitParams = makeParams({}, base);
    explicitParams.enabledParameters = "temperature,top_p,frequency_penalty,presence_penalty,seed,max_tokens";
    explicitParams.temperature = 0.0;
    explicitParams.topP = 0.0;
    explicitParams.seed = 0;
    explicitParams.reasoningEffort = "vendor-ultra";
    explicitParams.maxTokensField = "max_completion_tokens";
    explicitParams.maxTokens = 90000;
    QString configError;
    auto body = ApiClient::buildRequestBody(explicitParams, false, &configError);
    check(configError.isEmpty() && body.contains("seed") && body["seed"].toInt() == 0
          && body["top_p"].toDouble() == 0.0 && body["frequency_penalty"].toDouble() == 0.0,
          "explicit zero parameters were omitted");
    check(body["reasoning_effort"] == "vendor-ultra" && body["max_completion_tokens"].toInt() == 90000,
          "explicit effort or output budget was silently remapped/clamped");
    explicitParams.extraBody = R"({"temperature":null,"reasoning_effort":"custom","thinking":{"type":"adaptive"}})";
    body = ApiClient::buildRequestBody(explicitParams, true, &configError);
    check(configError.isEmpty() && !body.contains("temperature")
          && body["reasoning_effort"] == "custom"
          && body["thinking"].toObject()["type"] == "adaptive", "JSON override precedence failed");
    payload = R"({"choices":[{"message":{"content":"hello"}}]})";
    api.chatCreate(explicitParams);
    const auto actual = QJsonDocument::fromJson(
        lastRequest.mid(lastRequest.indexOf("\r\n\r\n") + 4)).object();
    check(actual == ApiClient::buildRequestBody(explicitParams, false),
          "actual HTTP request differs from preview");
    for (const QString& invalid : {QString("[]"), QString("{broken"),
            QString(R"({"messages":[]})"), QString(R"({"stream":false})"),
            QString(R"({"model":"other"})"), QString(R"({"max_tokens":10})")}) {
        explicitParams.extraBody = invalid;
        ApiClient::buildRequestBody(explicitParams, true, &configError);
        check(!configError.isEmpty(), "invalid/reserved/conflicting JSON accepted: " + invalid);
    }
    explicitParams.extraBody = "[]";
    lastRequest.clear();
    check(api.chatCreate(explicitParams).errorCode != 0 && lastRequest.isEmpty(),
          "invalid configuration reached the network");
    int configFailures = 0;
    api.chatCreateStream(explicitParams, [&](const QString&, const QString&, bool final, int code, const QString&) {
        if (final && code != 0) ++configFailures;
    });
    check(configFailures == 1 && lastRequest.isEmpty(), "stream configuration failure callback incorrect");
    auto qwen = makeParams({}, base, "qwen");
    qwen.thinking = "off";
    body = ApiClient::buildRequestBody(qwen, false, &configError);
    check(body.contains("enable_thinking") && !body["enable_thinking"].toBool(),
          "explicit Qwen thinking field format incorrect");
    qwen.provider = "custom";
    ApiClient::buildRequestBody(qwen, false, &configError);
    check(!configError.isEmpty(), "unknown thinking format was guessed");
    qwen.thinkingFormat = "thinking";
    body = ApiClient::buildRequestBody(qwen, false, &configError);
    check(configError.isEmpty() && body["thinking"].toObject()["type"] == "disabled",
          "explicit thinking format failed");
    qwen.thinkingFormat = "auto";
    qwen.modelMetadata = richMetadata;
    qwen.enabledParameters = "max_tokens";
    body = ApiClient::buildRequestBody(qwen, false, &configError);
    check(configError.isEmpty() && body["enable_thinking"] == false
          && body.contains("max_completion_tokens") && !body.contains("reasoning_effort"),
          "upstream field suggestions failed or materialized the upstream default effort");
    qwen.thinkingFormat = "thinking";
    qwen.maxTokensField = "max_tokens";
    body = ApiClient::buildRequestBody(qwen, false, &configError);
    check(configError.isEmpty() && body.contains("thinking") && body.contains("max_tokens")
          && !body.contains("enable_thinking") && !body.contains("max_completion_tokens"),
          "upstream metadata overrode explicit field formats");
    const auto metadataResult = ApiClient::parseModels(QJsonDocument::fromJson(
        R"({"data":[{"id":"m","context_length":128000,"supported_parameters":["temperature"]},{"id":"plain"}]})").object());
    check(metadataResult.metadata["m"].toObject()["context_length"].toInt() == 128000
          && !metadataResult.metadata["plain"].toObject().contains("context_length"),
          "model metadata lost or unknown capability fabricated");
    Settings zeroSettings(QJsonObject{{"ai_top_p", 0.0}, {"ai_reasoning_effort", "vendor-ultra"},
                                      {"ai_max_tokens", 2000000}});
    check(zeroSettings.ai_top_p == 0 && zeroSettings.ai_reasoning_effort == "vendor-ultra"
          && zeroSettings.ai_max_tokens == 2000000, "settings silently altered explicit model options");
    payload = "data:{\"choices\":[{\"delta\":{\"content\":\"stream\"}}]}\n\ndata: [DONE]\n\n";
    QString stream;
    int partialChunks = 0;
    int finalChunks = 0;
    const int streamCode = api.chatCreateStream(makeParams("fixture-key", base),
        [&](const QString& content, const QString&, bool final, int code, const QString&) {
            if (final) ++finalChunks;
            else ++partialChunks;
            if (final && code == 0) stream = content;
        });
    check(streamCode == 0 && stream == "stream", "SSE data without a space failed");
    check(partialChunks == 1 && finalChunks == 1, "SSE content must emit incremental and one final callback");
    payload = "data: {\"choices\":[{\"delta\":{\"content\":\"tail\"}}]}";
    stream.clear();
    api.chatCreateStream(makeParams({}, base),
        [&](const QString& content, const QString&, bool final, int code, const QString&) {
            if (final && code == 0) stream = content;
        });
    check(stream == "tail", "SSE final line without newline was lost");
    status = 500;
    payload = R"({"error":{"message":"fixture server failure"}})";
    QString streamError;
    api.chatCreateStream(makeParams({}, base),
        [&](const QString&, const QString&, bool final, int code, const QString& error) {
            if (final && code != 0) streamError = error;
        });
    check(streamError.contains("fixture server failure"), "SSE consumed HTTP error details");
    {
        AiRequestOptions ai;
        ai.apiBase = base;
        ai.model = QStringLiteral("fixture-model");
        ai.timeout = 2;
        ai.maxTokens = 128;
        ai.streamThinking = true;
        TextTranslateService service("fixture", "English", "general", "{source_text}", {},
            ai);
        int errors = 0;
        QObject::connect(&service, &TextTranslateService::error, &window,
                         [&](const QString&) { ++errors; });
        service.start();
        QElapsedTimer deadline;
        deadline.start();
        while (service.isRunning() && deadline.elapsed() < 4000) settle(20);
        service.requestCancel();
        service.wait();
        settle(20);
        check(errors == 1, "streaming translation emitted duplicate error notifications");
    }
    status = 401;
    payload = R"({"error":{"message":"fixture unauthorized"}})";
    check(api.chatCreate(makeParams("fixture-key", base)).errorCode != 0, "HTTP error reported success");
    stall = true;
    std::atomic<int> cancel{1};
    QElapsedTimer cancellation;
    cancellation.start();
    auto cancelled = makeParams("fixture-key", base);
    cancelled.cancelFlag = &cancel;
    check(api.chatCreate(cancelled).errorCode != 0,
          "cancelled request reported success");
    check(cancellation.elapsed() < 1500, "request cancellation did not stop promptly");

    auto* translation = window.findChild<TranslationPage*>();
    settings.ai_api_key = "fixture-key";
    settings.ai_api_base = base;
    settings.ai_model = "fixture-model";
    settings.ai_timeout = 10;
    settings.close_to_tray = QVariant();
    window.show();
    if (translation) {
        for (auto* editor : translation->findChildren<QPlainTextEdit*>())
            if (editor->placeholderText().startsWith("输入或粘贴")) editor->setPlainText("fixture source");
        QMetaObject::invokeMethod(translation, "onTranslate", Qt::DirectConnection);
        settle(150);
        check(window.isBusy(), "translation fixture did not start");
        cancelTimer.start(30);
        window.close();
        settle(150);
        cancelTimer.stop();
        check(window.isVisible() && window.isBusy(), "cancel close interrupted translation");
        QMetaObject::invokeMethod(translation, "clearWorkspace", Qt::DirectConnection);
        settle(300);
    } else check(false, "translation tool page missing");

    const QString tessdata = OcrEngine::findTessdataDir();
    if (!tessdata.isEmpty() && QFile::exists(tessdata + "/eng.traineddata")) {
        // Reproduce default languages with only English installed. Saving must
        // match visible checkboxes, otherwise both screenshot actions are blocked.
        const QString ocrModels = temp.filePath("ocr/tessdata");
        check(QDir().mkpath(ocrModels), "could not create OCR language fixture directory");
        check(QFile::copy(tessdata + "/eng.traineddata", ocrModels + "/eng.traineddata"),
              "could not copy English OCR fixture");
        const QString defaults = Settings(Settings::defaultsDict()).ai_ocr_langs;
        OcrLangManager languages(defaults, temp.filePath("ocr"), nullptr);
        check(languages.getSelectedLangs() == "eng",
              "initial OCR selection includes invisible missing languages or ignores tessdata child");
        for (auto* checkbox : languages.findChildren<QCheckBox*>()) checkbox->setChecked(false);
        check(languages.getSelectedLangs().isEmpty(), "empty OCR selection silently enabled English");
        languages.reload(defaults, ocrModels);
        check(languages.getSelectedLangs() == "eng", "reloaded OCR selection includes missing languages");
        languages.reload(defaults, temp.filePath("missing-models"));
        check(languages.getSelectedLangs().isEmpty(), "directory change retained unavailable OCR languages");

        auto* languageList = window.settingsView()->findChild<OcrLangManager*>();
        check(languageList != nullptr, "OCR language settings control missing");
        if (languageList) {
            languageList->reload(defaults, ocrModels);
            QMetaObject::invokeMethod(window.settingsView(), "onSave", Qt::DirectConnection);
            check(settings.ai_ocr_langs == "eng", "settings save retained hidden default OCR languages");
            Settings restored(settings.toDict());
            check(restored.ai_ocr_langs == "eng", "OCR selection did not survive settings serialization");
        }
        OcrEngine engine;
        check(engine.init(ocrModels, settings.ai_ocr_langs), "saved OCR language selection failed to initialize");
        QImage sample(640, 120, QImage::Format_RGB888);
        sample.fill(Qt::white);
        { QPainter painter(&sample); QFont font("Arial"); font.setPixelSize(50);
          painter.setFont(font); painter.setPen(Qt::black); painter.drawText(20, 80, "HELLO WORLD"); }
        const QString recognized = engine.extractText(sample.constBits(), sample.width(), sample.height(), 3, sample.bytesPerLine());
        check(recognized.contains("HELLO", Qt::CaseInsensitive), "OCR could not recognize generated fixture");
        check(!engine.init(temp.filePath("missing-models"), "eng"), "OCR reused a different model directory");
    } else failures << "bundled English OCR data missing";

    window.show();
    SnapUi::applyThemeMode(SnapUi::ThemeMode::Light);
    window.resize(900, 600);
    window.showSettings("ai");
    settle(450);
    window.grab().save(output + "/settings-small-light.png");
    window.navigation(window.property("tool.translate").toString());
    settle(450);
    window.grab().save(output + "/translate-small-light.png");
    check(translation && translation->findChild<AiModelSelector*>(), "text translation did not embed shared AI controls");
    {
        QPixmap sample(640, 200);
        sample.fill(Qt::white);
        { QPainter painter(&sample); painter.setPen(Qt::black);
          painter.drawText(sample.rect(), Qt::AlignCenter, "Screenshot translation preview"); }
        TranslateWindow screenshot(sample, &settings, false, &notify);
        screenshot.resize(900, 620);
        screenshot.show();
        settle(600);
        auto* screenshotControls = screenshot.findChild<AiModelSelector*>();
        check(screenshotControls && screenshotControls->isVisible(), "screenshot translation controls are missing or hidden behind an empty page");
        check(!screenshot.isBusy(), "screenshot quick controls triggered an unsolicited AI request");
        screenshot.grab().save(output + "/screenshot-translate-small-light.png");
        screenshot.hide();
    }
    if (!failures.isEmpty()) qCritical().noquote() << failures.join('\n');
    else qInfo() << "Regression checks passed: close lifecycle and active translation, settings, capture, PNG/JPG/BMP, Qt-Leptonica JPEG interoperability, OCR, JSON/SSE/HTTP/cancellation.";
    QFile report(output + "/results.txt");
    if (report.open(QIODevice::WriteOnly)) report.write(failures.isEmpty() ? QByteArray("PASS\n") : failures.join('\n').toUtf8());
    window.hide();
    Q_UNUSED(app);
    return failures.isEmpty() ? 0 : 1;
}
