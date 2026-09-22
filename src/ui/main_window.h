#pragma once
#include "ElaWidgetTools.hpp"
#include "tool_definition.h"
#include <utility>
class Settings;
class NotifyManager;
class SettingsView;
class TranslationPage;
class QGridLayout;
class HomeToolCard;
class CloseDialog : public ElaDialog
{
    Q_OBJECT
public:
    explicit CloseDialog(QWidget* parent = nullptr);

    /// Returns (toTray, remember). toTray=QString() if cancelled.
    static std::pair<QVariant, bool> ask(QWidget* parent);

private:
    void done(bool toTray);
    QPushButton* m_btnTray    = nullptr;
    QPushButton* m_btnExit    = nullptr;
    QCheckBox*   m_remember   = nullptr;
    bool         m_result     = false;    // true = to tray
    bool         m_decided    = false;    // false = cancelled
};

// Application shell: navigation, tool discovery, settings and close policy.
class MainWindow : public ElaWindow
{
    Q_OBJECT
public:
    explicit MainWindow(Settings* settings, NotifyManager* notifyManager = nullptr,
                        const QString& appMode = "translate", QWidget* parent = nullptr);
    bool isBusy() const;
    void showSettings(const QString& category = {});
    void prepareForQuit();
    SettingsView* settingsView() const { return m_settingsView; }
signals:
    void settingsSaved();
    void snipRequested();
    void quitRequested();
protected:
    void closeEvent(QCloseEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
private:
    void buildUi();
    QWidget* buildHomePage();
    void layoutHomeCards();
    QWidget* buildCapturePage();
    Settings* m_settings;
    NotifyManager* m_notifyManager;
    SettingsView* m_settingsView = nullptr;
    TranslationPage* m_translationPage = nullptr;
    QString m_settingsKey;
    bool m_closePending = false;
    QList<ToolDefinition> m_tools;
    QGridLayout* m_homeGrid = nullptr;
    QList<HomeToolCard*> m_homeCards;
};
