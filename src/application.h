#ifndef SNAPLENS_APPLICATION_H
#define SNAPLENS_APPLICATION_H

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <QApplication>
#include <QObject>
#include <QList>
#include <QPixmap>
#include <QString>

class Settings;
class NotifyManager;
class TrayIcon;
class IHotkeyProvider;
class MainWindow;
class SnipSession;
class TranslateWindow;
class OcrWindow;
class PinWindow;

// Application lifecycle, feature prerequisites, capture actions and shared services.
class AppController : public QObject
{
    Q_OBJECT

public:
    explicit AppController(QApplication *app, QObject *parent = nullptr);

    ~AppController() override;

    bool isCancelled() const { return m_cancelled; }

private slots:
    void startSnip();

    void openSettings();

    /// 设置页点击「保存设置」后的重新应用（热键 / 日志级别 / 启动模式 / 托盘文案）。
    void applySettingsAfterSave();

    void switchMode();

    void quitApp();

private:
    void initAfterSettings();

    void shutdown();
    bool ensureOcrReady();

    /// 按需创建主窗口（并接好它与本对象的信号）。
    void createMainWindowIfNeeded();

    /// 显示并激活主窗口。
    void showMainWindow();

    void onSave(const QPixmap &pixmap);

    void onPin(const QPixmap &pixmap);

    void onCopy(const QPixmap &pixmap);

    void onTranslate(const QPixmap &pixmap);

    void onOcr(const QPixmap &pixmap);

    void cleanupTempDir();

    QString generateFileName(const QString &format) const;

private:
    QApplication *_app;
    SnipSession *_session = nullptr; // 进行中的截图会话（同时只允许有一个）
    Settings *_settings = nullptr;
    NotifyManager *_notifyManager = nullptr;
    TrayIcon *m_tray = nullptr;
    IHotkeyProvider *m_hotkey = nullptr;
    MainWindow *m_mainWindow = nullptr;

    TranslateWindow *m_translateWin = nullptr;
    OcrWindow *m_ocrWin = nullptr;
    QList<PinWindow *> m_pins;
    bool m_cancelled = false;
    bool m_quitPromptOpen = false;
    bool m_shuttingDown = false;
    QPixmap m_pendingTranslation;
    QPixmap m_pendingOcr;
};

#endif // SNAPLENS_APPLICATION_H
