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
#include "../translate/ai_client.h"
#include "../capture/snip_session.h"
#include "../capture/screen_capture.h"
#include <QSystemTrayIcon>
#include <QTemporaryDir>
#include <QPlainTextEdit>
#include <QComboBox>
#include <QLineEdit>
#include "../translate/translation_page.h"
#include <leptonica/allheaders.h>

namespace {
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

    window.showSettings("screenshot");
    settle(250);
    auto* invertToggle = window.settingsView()->findChild<ElaToggleSwitch*>("crosshairInvertToggle");
    auto* invertCard = window.settingsView()->findChild<QWidget*>("crosshairInvertCard");
    auto* colorCard = window.settingsView()->findChild<QWidget*>("crosshairColorCard");
    auto* alphaCard = window.settingsView()->findChild<QWidget*>("crosshairAlphaCard");
    auto* thicknessCard = window.settingsView()->findChild<QWidget*>("crosshairThicknessCard");
    check(invertToggle && invertCard && colorCard && alphaCard && thicknessCard,
          "crosshair visibility controls missing");
    if (invertToggle && invertCard && colorCard && alphaCard && thicknessCard) {
        invertToggle->setIsToggled(true);
        settle(250);
        window.grab().save(output + "/crosshair-invert-on.png");
        check(!colorCard->isVisible() && !alphaCard->isVisible() && !thicknessCard->isVisible(),
              "inverted crosshair parameters must be hidden");
        invertToggle->setIsToggled(false);
        settle(250);
        window.grab().save(output + "/crosshair-invert-off.png");
        check(colorCard->isVisible() && alphaCard->isVisible() && thicknessCard->isVisible(),
              "normal crosshair parameters must be visible");
        check(invertCard->geometry().top() < colorCard->geometry().top(),
              "crosshair parameters must appear below the invert switch");
    }
    window.showSettings("general");
    settle(150);

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
    auto chat = api.chatCreate("fixture-key", base, "fixture-model", messages, 2);
    check(chat.errorCode == 0 && chat.content == "hello" && chat.thinking == "reason", "JSON chat response failed: " + chat.errorMsg);
    chat = api.chatCreate({}, base + '/', "fixture-model", messages, 2);
    check(chat.errorCode == 0 && chat.content == "hello", "custom local endpoint without key failed: " + chat.errorMsg);
    check(lastRequest.startsWith("POST /v1/chat/completions HTTP/")
          && !lastRequest.toLower().contains("authorization:"), "custom URL or optional auth header was incorrect");
    payload = R"({"data":[{"id":"local-model"}]})";
    auto models = api.listModels({}, base + '/', 2);
    check(models.errorCode == 0 && models.modelIds.contains("local-model"), "custom model list without key failed");
    check(lastRequest.startsWith("GET /v1/models HTTP/"), "custom model list URL had an incorrect path");
    payload = R"({"choices":[{"message":{"content":"hello","reasoning_content":"reason"}}]})";
    payload = "data:{\"choices\":[{\"delta\":{\"content\":\"stream\"}}]}\n\ndata: [DONE]\n\n";
    QString stream;
    const int streamCode = api.chatCreateStream("fixture-key", base, "fixture-model", messages,
        2, 0.1, 128, 1.0, 0, 0, 0, [&](const QString& content, const QString&, bool final, int code, const QString&) {
            if (final && code == 0) stream = content;
        });
    check(streamCode == 0 && stream == "stream", "SSE data without a space failed");
    status = 401;
    payload = R"({"error":{"message":"fixture unauthorized"}})";
    check(api.chatCreate("fixture-key", base, "fixture-model", messages, 2).errorCode != 0, "HTTP error reported success");
    stall = true;
    std::atomic<int> cancel{1};
    QElapsedTimer cancellation;
    cancellation.start();
    check(api.chatCreate("fixture-key", base, "fixture-model", messages, 2, 0.1, 128, 1, 0, 0, 0, &cancel).errorCode != 0,
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
        OcrEngine engine;
        check(engine.init(tessdata, "eng"), "bundled English OCR failed to initialize");
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
