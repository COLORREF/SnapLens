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

/*
 * 应用编排器：首次运行弹出引导向导，按模式显示/隐藏翻译窗口，驻留托盘。
 * 支持两种模式：
 *  - translate（翻译模式）：启动时显示文本翻译窗口
 *  - screenshot（截图模式）：后台静默运行，仅托盘图标
 *  两种模式均可通过快捷键截图，均可使用翻译功能。
 */
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

    void switchMode();

    void quitApp();

private:
    void initAfterSettings();

    void runSetupWizard();

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
};

#endif // SNAPLENS_APPLICATION_H
