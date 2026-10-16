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

            // GLM 支持 do_sample，且推理档位只接受 low/high/max。
            provider->setCurrentIndex(provider->findData("glm"));
            check(doSample->isVisibleTo(window.settingsView()),
                  "GLM supports do_sample and should show the control");
            check(effort->findData("low") >= 0 && effort->findData("max") >= 0
                  && effort->findData("none") < 0 && effort->findData("medium") < 0,
                  "GLM reasoning effort options should be limited to low/high/max");
            doSample->setCurrentIndex(doSample->findData("off"));
            QMetaObject::invokeMethod(window.settingsView(), "onSave", Qt::DirectConnection);
            check(settings.ai_do_sample == "off", "GLM do_sample selection was not saved");

            // Kimi 支持 thinking，但固定采样参数：do_sample 行应隐藏。
            provider->setCurrentIndex(provider->findData("kimi"));
            check(!doSample->isVisibleTo(window.settingsView()),
                  "GLM-only do_sample row must be hidden for Kimi");
            check(thinking->isVisibleTo(window.settingsView()),
                  "Kimi supports thinking and should show the control");
            check(!effort->isVisibleTo(window.settingsView()),
                  "Kimi has no reasoning_effort API; the row should be hidden");

            // 混元没有思考参数，整组都应隐藏。
            provider->setCurrentIndex(provider->findData("hunyuan"));
            check(!thinking->isVisibleTo(window.settingsView()),
                  "Hunyuan has no thinking parameter and should hide the control");
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
    QByteArray lastRequest;
    QObject::connect(&server, &QTcpServer::newConnection, &window, [&] {
        while (auto* socket = server.nextPendingConnection()) {
            QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            QObject::connect(socket, &QTcpSocket::readyRead, socket, [&, socket] {
                lastRequest = socket->readAll();
                if (socket->property("sent").toBool() || stall) return;
                socket->setProperty("sent", true);
                const QByteArray header = "HTTP/1.1 " + QByteArray::number(status)
                    + " Test\r\nContent-Type: application/json\r\nContent-Length: "
                    + QByteArray::number(payload.size()) + "\r\nConnection: close\r\n\r\n";
                socket->write(header + payload);
                socket->disconnectFromHost();
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
    payload = R"({"data":[{"id":"local-model"}]})";
    auto models = api.listModels({}, base + '/', 2);
    check(models.errorCode == 0 && models.modelIds.contains("local-model"), "custom model list without key failed");
    check(lastRequest.startsWith("GET /v1/models HTTP/"), "custom model list URL had an incorrect path");

    // ---- 服务商能力裁剪：只发送该服务商真正支持的字段 ----
    // 依据各厂商官方文档（见 providers.h 的 docUrl）。
    payload = R"({"choices":[{"message":{"content":"hello"}}]})";
    auto sentBody = [&] { return lastRequest.mid(lastRequest.indexOf("\r\n\r\n") + 4); };

    // Kimi：temperature / top_p / 惩罚项一律不可改，传值会 400；输出用 max_completion_tokens
    auto kimiParams = makeParams("k", base, QStringLiteral("kimi"));
    kimiParams.temperature = 0.3;
    kimiParams.topP = 0.5;
    kimiParams.frequencyPenalty = 0.4;
    kimiParams.presencePenalty = 0.4;
    kimiParams.maxTokens = 2048;
    api.chatCreate(kimiParams);
    {
        const QString body = sentBody();
        check(!body.contains("temperature") && !body.contains("top_p")
              && !body.contains("frequency_penalty") && !body.contains("presence_penalty"),
              "Kimi request must not send fixed sampling parameters");
        check(body.contains("max_completion_tokens") && !body.contains("\"max_tokens\""),
              "Kimi request must use max_completion_tokens");
    }

    // DeepSeek：frequency_penalty / presence_penalty 已废弃，不应发送；温度与 top_p 保留
    auto dsParams = makeParams("d", base, QStringLiteral("deepseek"));
    dsParams.frequencyPenalty = 0.4;
    dsParams.presencePenalty = 0.4;
    dsParams.thinking = QStringLiteral("on");
    dsParams.reasoningEffort = QStringLiteral("high");
    api.chatCreate(dsParams);
    {
        const QString body = sentBody();
        check(!body.contains("frequency_penalty") && !body.contains("presence_penalty"),
              "DeepSeek request must not send deprecated penalty parameters");
        check(body.contains("temperature") && body.contains("top_p"),
              "DeepSeek request should keep temperature and top_p");
        check(body.contains("\"thinking\"") && body.contains("enabled")
              && body.contains("reasoning_effort"),
              "DeepSeek request should carry thinking and reasoning_effort");
    }

    // GLM：温度上限为 1（非 2），支持 do_sample；越界的推理强度会就近映射
    auto glmParams = makeParams("g", base, QStringLiteral("glm"));
    glmParams.temperature = 1.8;              // 超出 GLM 的 0..1
    glmParams.reasoningEffort = QStringLiteral("none");   // GLM-5.3 只接受 low/high/max
    api.chatCreate(glmParams);
    {
        const QString body = sentBody();
        check(body.contains("reasoning_effort"), "GLM request should carry reasoning_effort");
        const QJsonObject obj = QJsonDocument::fromJson(body.toUtf8()).object();
        const QJsonValue tempValue = obj.value("temperature");
        check(!tempValue.isUndefined() && tempValue.toDouble() <= 1.0,
              "GLM temperature must be clamped to the documented 0..1 range");
    }

    // GLM do_sample=false：服务商忽略 temperature / top_p，请求不应携带这两个字段
    auto glmNoSample = makeParams("g", base, QStringLiteral("glm"));
    glmNoSample.doSample = QStringLiteral("off");
    api.chatCreate(glmNoSample);
    {
        const QString body = sentBody();
        check(body.contains("\"do_sample\":false"), "GLM request should send do_sample when set");
        check(!body.contains("temperature") && !body.contains("top_p"),
              "GLM with do_sample=false must omit temperature and top_p");
    }

    // 通义千问：思考开关是顶层 enable_thinking，不是 thinking 对象
    auto qwenParams = makeParams("q", base, QStringLiteral("qwen"));
    qwenParams.thinking = QStringLiteral("off");
    api.chatCreate(qwenParams);
    {
        const QString body = sentBody();
        check(body.contains("enable_thinking") && !body.contains("\"thinking\""),
              "Qwen should use the top-level enable_thinking flag");
    }

    // 自定义服务：按 OpenAI 兼容超集发送，惩罚项与 seed 都应存在
    auto customParams = makeParams("c", base);
    customParams.seed = 42;
    customParams.stop = QStringList{"END"};
    customParams.streamUsage = true;
    api.chatCreate(customParams);
    {
        const QString body = sentBody();
        check(body.contains("frequency_penalty") && body.contains("presence_penalty"),
              "custom provider should keep penalty parameters");
        check(body.contains("\"seed\":42"), "custom provider should send a non-zero seed");
        check(body.contains("\"stop\""), "custom provider should send stop sequences");
    }
    payload = R"({"choices":[{"message":{"content":"hello","reasoning_content":"reason"}}]})";
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
    if (!failures.isEmpty()) qCritical().noquote() << failures.join('\n');
    else qInfo() << "Regression checks passed: close lifecycle and active translation, settings, capture, PNG/JPG/BMP, Qt-Leptonica JPEG interoperability, OCR, JSON/SSE/HTTP/cancellation.";
    QFile report(output + "/results.txt");
    if (report.open(QIODevice::WriteOnly)) report.write(failures.isEmpty() ? QByteArray("PASS\n") : failures.join('\n').toUtf8());
    window.hide();
    Q_UNUSED(app);
    return failures.isEmpty() ? 0 : 1;
}
