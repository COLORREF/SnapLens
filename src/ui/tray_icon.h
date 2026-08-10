#ifndef SNAPLENS_UI_TRAY_ICON_H
#define SNAPLENS_UI_TRAY_ICON_H

#include <QSystemTrayIcon>
#include <QMenu>

class TrayIcon : public QSystemTrayIcon
{
    Q_OBJECT

public:
    explicit TrayIcon(QObject* parent = nullptr);
    void setMode(const QString& mode);
    void setHotkeyText(const QString& hotkey);

signals:
    void snipRequested();
    void settingsRequested();
    void quitRequested();
    void modeSwitchRequested();
    void translateRequested();

private:
    QMenu*  m_menu = nullptr;
    QAction* m_snipAction = nullptr;
    QAction* m_translateAction = nullptr;
    QAction* m_modeAction = nullptr;
};

#endif