#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "application.h"
#include "settings.h"
#include "notify/notify_manager.h"
#include "ui/tray_icon.h"
#include "ui/pin_window.h"
#include "ui/settings_view.h"
#include "ui/snapkit.h"
#include "capture/snip_session.h"
#include "translate/translator.h"
#include "translate/translate_window.h"
#include "ui/main_window.h"
#include "ocr/ocr_engine.h"
#include "ocr/ocr_window.h"
#include "platform/platform_base.h"
#include "platform/platform_factory.h"
#include "log/log_manager.h"

#include <QClipboard>
#include <QDir>
#include <QKeySequence>
#include <QMessageBox>
#include <QStandardPaths>
#include <QDateTime>
#include <QFileInfo>

// ============================================================================
// AppController
// ============================================================================

AppController::AppController(QApplication* app, QObject* parent)
    : QObject(parent)
    , _app(app)
{
    _notifyManager = new NotifyManager();

    _settings = new Settings(Settings::load());
    _notifyManager->setSettings(_settings);
    SnapUi::applyThemeMode(SnapUi::themeModeFromString(_settings->theme_mode));
    initAfterSettings();
}

AppController::~AppController()
{
    if (m_hotkey) {
        m_hotkey->stop();
    }
    if (_session) {
        auto* session = _session;
        _session = nullptr;
        disconnect(session, nullptr, this, nullptr);
        session->close();
        delete session;
    }
    cleanupTempDir();
}

void AppController::createMainWindowIfNeeded()
{
    if (m_mainWindow) return;
    m_mainWindow = new MainWindow(_settings, _notifyManager, _settings->app_mode);
    connect(m_mainWindow, &MainWindow::settingsSaved,
            this, &AppController::applySettingsAfterSave);
    connect(m_mainWindow, &MainWindow::snipRequested, this, &AppController::startSnip);
    connect(m_mainWindow, &MainWindow::quitRequested, this, &AppController::shutdown);
    _notifyManager->setDefaultParent(m_mainWindow);
}

void AppController::showMainWindow()
{
    createMainWindowIfNeeded();
    if (m_mainWindow->isMinimized()) {
        m_mainWindow->showNormal();
    } else {
        m_mainWindow->show();
    }
    m_mainWindow->raise();
    m_mainWindow->activateWindow();
}

void AppController::initAfterSettings()
{
    // Clean temp directory on startup
    cleanupTempDir();

    // Create tray
    m_tray = new TrayIcon();
    m_tray->setMode(_settings->app_mode);
    m_tray->show();
    _notifyManager->setTray(m_tray);

    connect(m_tray, &TrayIcon::snipRequested, this, &AppController::startSnip);
    connect(m_tray, &TrayIcon::settingsRequested, this, &AppController::openSettings);
    connect(m_tray, &TrayIcon::quitRequested, this, &AppController::quitApp);
    connect(m_tray, &TrayIcon::modeSwitchRequested, this, &AppController::switchMode);
    connect(m_tray, &TrayIcon::translateRequested, this, [this]() {
        // 主窗口按需创建；设置随窗口一起出现（窗口内已是普通页面）
        showMainWindow();
    });

    // Register hotkey
    m_hotkey = createHotkeyProvider();
    QObject::connect(m_hotkey, &IHotkeyProvider::triggered, this, &AppController::startSnip);
    if (!m_hotkey->start(_settings->hotkey)) {
        SNAP_LOG_WARNING("热键注册失败: " + _settings->hotkey);
        QKeySequence ks(_settings->hotkey);
        QString native = ks.toString(QKeySequence::NativeText);
        _notifyManager->notify("hotkey_fail", "SnapLens 截图",
            QString("快捷键 %1 注册失败，可能被其它程序占用，请在设置中更换。").arg(native));
    }
    // Update tray hotkey text
    if (m_tray) {
        QKeySequence ks(_settings->hotkey);
        m_tray->setHotkeyText(ks.toString(QKeySequence::NativeText));
    }

    // Apply log levels from settings
    LogManager::instance().applySettings(
        _settings->log_debug_enabled,
        _settings->log_info_enabled,
        _settings->log_warning_enabled,
        _settings->log_error_enabled
    );

    // A missing tray must never leave a running app without an entry point.
    if (_settings->app_mode == "translate" || !QSystemTrayIcon::isSystemTrayAvailable()) {
        showMainWindow();
    } else {
        // 截图模式：主窗口按需创建，需要时从托盘打开
    }
}

void AppController::startSnip()
{
    if (_session || m_shuttingDown) {
        SNAP_LOG_DEBUG("startSnip: session already active, ignoring");
        return;
    }

    SNAP_LOG_DEBUG("startSnip: starting session");

    _session = new SnipSession(
        [this](const QPixmap& pixmap) { onSave(pixmap); },
        [this](const QPixmap& pixmap) { onPin(pixmap); },
        [this](const QPixmap& pixmap) { onCopy(pixmap); },
        [this](const QPixmap& pixmap) { onTranslate(pixmap); },
        [this](const QPixmap& pixmap) { onOcr(pixmap); },
        _settings
    );

    connect(_session, &SnipSession::finished, this, [this]() {
        // 判空防御：start() 失败路径中 finished 会先于此处被同步触发，
        // 且对象析构时 close() 还可能再次 emit finished —— 成员可能已为 nullptr。
        if (_session) {
            _session->deleteLater();
            _session = nullptr;
        }
    });

    if (!_session->start()) {
        SNAP_LOG_WARNING("startSnip: failed to start");
        _notifyManager->notify("capture_fail", "SnapLens", "屏幕抓取失败，请重试。");
        // start() 失败时内部已 emit finished()（上面的 lambda 已清理 _session），
        // 这里仅作兜底：若指针仍有效则清理，避免对空指针 deleteLater（UB）。
        if (_session) {
            _session->deleteLater();
            _session = nullptr;
        }
    }
}

void AppController::onSave(const QPixmap& pixmap)
{
    QString fileName = generateFileName(_settings->save_format);
    QDir().mkpath(QFileInfo(fileName).absolutePath());

    if (pixmap.save(fileName, _settings->save_format.toUpper().toUtf8().constData())) {
        SNAP_LOG_INFO("保存成功: " + fileName);
        _notifyManager->notify("save_success", "SnapLens 截图", "已保存：" + fileName);
    } else {
        SNAP_LOG_ERROR("保存失败: " + fileName);
        _notifyManager->notify("save_fail", "SnapLens 截图", "保存失败，请检查保存目录是否可写。");
    }
}

void AppController::onPin(const QPixmap& pixmap)
{
    PinWindow* pin = new PinWindow(pixmap, _settings->save_dir);
    connect(pin, &PinWindow::closed, this, [this, pin]() {
        m_pins.removeAll(pin);
    });
    m_pins.append(pin);
    pin->show();
}

void AppController::onCopy(const QPixmap& pixmap)
{
    QApplication::clipboard()->setPixmap(pixmap);
    _notifyManager->notify("copy", "SnapLens 截图", "已复制到剪贴板。");
}

void AppController::onTranslate(const QPixmap& pixmap)
{
    if (!ensureOcrReady()) { m_pendingTranslation = pixmap; return; }
    if (_settings->ai_api_key.trimmed().isEmpty() || _settings->ai_model.trimmed().isEmpty()) {
        m_pendingTranslation = pixmap;
        showMainWindow();
        m_mainWindow->showSettings("ai");
        _notifyManager->info("配置 AI 服务", "填写 API Key 与模型并保存后，再使用截图翻译。保存后将继续处理本次截图。", m_mainWindow);
        return;
    }
    m_pendingTranslation = {};
    bool autoTranslate = !_settings->ai_confirm_before_translate;

    // 安全替换窗口（防御 close() 同步触发 closed 信号的重入副作用）：
    // 1. 先取出旧指针并将成员置空 —— close() 会同步 emit closed()，
    //    其 lambda 会将成员置空；若先 close 后 deleteLater，
    //    则会对空指针调用 deleteLater()（未定义行为）。
    // 2. 断开旧窗口到本对象的全部信号连接，关闭期间不再重入本对象。
    // 3. close() + deleteLater() 均在局部指针上进行，确保非空、可追踪。
    TranslateWindow* old = m_translateWin;
    m_translateWin = nullptr;
    if (old) {
        disconnect(old, nullptr, this, nullptr);
        old->close();
        old->deleteLater();
    }

    m_translateWin = new TranslateWindow(pixmap, _settings, autoTranslate, _notifyManager);
    connect(m_translateWin, &TranslateWindow::closed, this, [this]() {
        m_translateWin = nullptr;
    });
    m_translateWin->show();
}

bool AppController::ensureOcrReady()
{
    QString dir = _settings->ocr_tessdata_dir.trimmed();
    if (dir.isEmpty()) dir = OcrEngine::findTessdataDir();
    if (!dir.isEmpty() && QDir(dir).entryList({"*.traineddata"}, QDir::Files).isEmpty()
        && QDir(dir + "/tessdata").exists()) dir += "/tessdata";
    bool ready = !dir.isEmpty() && !_settings->ai_ocr_langs.trimmed().isEmpty();
    for (const auto& lang : _settings->ai_ocr_langs.split('+', Qt::SkipEmptyParts))
        ready = ready && QFileInfo(QDir(dir).filePath(lang + ".traineddata")).isFile();
    if (ready) return true;
    showMainWindow();
    m_mainWindow->showSettings("ocr");
    _notifyManager->info("配置文字识别", "请安装所选语言包并保存设置，随后继续处理本次截图。", m_mainWindow);
    return false;
}

void AppController::onOcr(const QPixmap& pixmap)
{
    if (!ensureOcrReady()) { m_pendingOcr = pixmap; return; }
    m_pendingOcr = {};
    // 同 onTranslate：先取出旧指针并置空成员，断开其信号连接，
    // 再 close + deleteLater，避免 close() 同步触发 closed 信号
    // 将成员置空后对空指针调用 deleteLater()（未定义行为）。
    OcrWindow* old = m_ocrWin;
    m_ocrWin = nullptr;
    if (old) {
        disconnect(old, nullptr, this, nullptr);
        old->close();
        old->deleteLater();
    }

    m_ocrWin = new OcrWindow(pixmap, _settings, _notifyManager);
    connect(m_ocrWin, &OcrWindow::closed, this, [this]() {
        m_ocrWin = nullptr;
    });
    m_ocrWin->show();
}

void AppController::openSettings()
{
    // 设置已是主窗口内的一个页面：打开窗口并切到该页，不再弹独立对话框。
    showMainWindow();
    m_mainWindow->showSettings();
}

void AppController::applySettingsAfterSave()
{
    if (!m_pendingTranslation.isNull()) {
        const QPixmap pending = m_pendingTranslation;
        onTranslate(pending);
    }
    if (!m_pendingOcr.isNull()) {
        const QPixmap pending = m_pendingOcr;
        onOcr(pending);
    }
    // Re-apply hotkey
    if (m_hotkey) {
        m_hotkey->stop();
        bool ok = m_hotkey->start(_settings->hotkey);
        QKeySequence ks(_settings->hotkey);
        QString native = ks.toString(QKeySequence::NativeText);
        if (m_tray) {
            m_tray->setHotkeyText(native);
        }
        if (!ok) {
            _notifyManager->notify("hotkey_fail", "SnapLens 截图",
                QString("快捷键 %1 注册失败，可能被其它程序占用，请在设置中更换。").arg(native));
        }
    }

    // Re-apply log levels
    LogManager::instance().applySettings(
        _settings->log_debug_enabled,
        _settings->log_info_enabled,
        _settings->log_warning_enabled,
        _settings->log_error_enabled
    );

    // Update tray mode text
    if (m_tray) {
        m_tray->setMode(_settings->app_mode);
    }

    // 轻提示：保存设置时仍有 OCR/翻译任务在运行 → 修改将在下次任务生效
    // （配置以快照方式在任务启动时读取，运行中的任务不受影响）
    if ((m_translateWin && m_translateWin->isBusy())
        || (m_ocrWin && m_ocrWin->isBusy())
        || (m_mainWindow && m_mainWindow->isBusy())) {
        _notifyManager->notify("settings_deferred", "SnapLens 设置",
            "有任务正在进行，配置修改将在下次 OCR/翻译时生效。");
    }
}

void AppController::switchMode()
{
    if (_settings->app_mode == "translate") {
        _settings->app_mode = "screenshot";
        if (m_mainWindow) {
            m_mainWindow->hide();
        }
    } else {
        _settings->app_mode = "translate";
        showMainWindow();
    }
    _settings->save();
    if (m_tray) {
        m_tray->setMode(_settings->app_mode);
    }
}

void AppController::quitApp()
{
    if (m_quitPromptOpen || m_shuttingDown) return;
    m_quitPromptOpen = true;
    showMainWindow();
    // The tray exit action confirms once; the window decision already confirmed.
    bool confirmed = _notifyManager->confirm("退出确认", "确定要退出 SnapLens 吗？");
    m_quitPromptOpen = false;
    if (!confirmed) return;

    shutdown();
}

void AppController::shutdown()
{
    if (m_shuttingDown) return;
    m_shuttingDown = true;
    if (_session) _session->close();
    // Close all pin windows
    // 先取出并清空列表，再逐一关闭：close() 会同步触发 closed 信号
    // （其 lambda 执行 m_pins.removeAll），若边遍历边修改容器会迭代器失效并漏关窗口。
    const auto pins = m_pins;
    m_pins.clear();
    for (auto* pin : pins) {
        pin->close();
    }

    // Close translate/ocr windows
    if (m_translateWin) m_translateWin->close();
    if (m_ocrWin) m_ocrWin->close();
    if (m_mainWindow) m_mainWindow->prepareForQuit();

    // Stop hotkey
    if (m_hotkey) {
        m_hotkey->stop();
    }

    // Hide tray
    if (m_tray) {
        m_tray->hide();
    }

    // Cleanup
    cleanupTempDir();

    _app->quit();
}

void AppController::cleanupTempDir()
{
    QString tempDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation) + "/SnapLens";
    QDir dir(tempDir);
    if (!dir.exists()) return;

    int removed = 0;
    const auto entries = dir.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QFileInfo& entry : entries) {
        if (entry.isFile() || entry.isSymLink()) {
            QFile::remove(entry.absoluteFilePath());
        } else if (entry.isDir()) {
            QDir(entry.absoluteFilePath()).removeRecursively();
        }
        removed++;
    }
    if (removed > 0) {
        SNAP_LOG_INFO(QString("temp cleanup: removed %1 item(s) from %2").arg(removed).arg(tempDir));
    }
}

QString AppController::generateFileName(const QString& format) const
{
    QString dir = _settings->save_dir;
    QString timestamp = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
    QString baseName = QString("SnapLens_%1").arg(timestamp);

    QString path = QDir(dir).filePath(baseName + "." + format);
    if (!QFileInfo::exists(path)) return path;

    // Avoid overwriting: append _2, _3, ...
    for (int i = 2; i < 1000; ++i) {
        path = QDir(dir).filePath(QString("%1_%2.%3").arg(baseName).arg(i).arg(format));
        if (!QFileInfo::exists(path)) return path;
    }
    return path;
}
