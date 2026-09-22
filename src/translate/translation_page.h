#pragma once
#include "ElaWidgetTools.hpp"
#include <QPointer>
class Settings;
class NotifyManager;
class TextTranslateService;
class QPlainTextEdit;
class QSplitter;
class QComboBox;
class PivotPanel : public QWidget
{
    Q_OBJECT
public:
    PivotPanel(const QString& title1, const QString& title2,
               QPlainTextEdit* edit1, QPlainTextEdit* edit2,
               bool defaultMerged, QWidget* parent = nullptr);

    void applyState(bool merged, double ratio);
    bool isMerged() const { return m_merged; }
    double ratio() const { return m_savedRatio; }
    QSplitter* splitter = nullptr;

signals:
    void stateChanged();

private:
    void rebuild(bool merged);
    void captureRatio();

    QPlainTextEdit* m_edit1 = nullptr;
    QPlainTextEdit* m_edit2 = nullptr;
    QString m_title1;
    QString m_title2;

    QSplitter*      m_splitter = nullptr;
    QWidget*        m_boxA = nullptr;
    QWidget*        m_boxB = nullptr;
    ElaTabWidget*   m_tabA = nullptr;
    ElaTabWidget*   m_tabB = nullptr;

    bool   m_merged = false;
    double m_savedRatio = 0.62;
    bool   m_rebuilding = false;
};

class TranslationPage : public QWidget
{
    Q_OBJECT
public:
    TranslationPage(Settings* settings, NotifyManager* notify, QWidget* parent = nullptr);
    bool isBusy() const;
    void prepareForQuit();
signals:
    void settingsRequested(const QString& category);
private slots:
    void onTranslate();
    void onTranslated(const QString& text);
    void onThinking(const QString& text);
    void onError(const QString& msg);
    void toggleLayout();
    void resetLayout();
    void refreshPromptPreview();
    void copyTranslation();
    void clearWorkspace();

private:
    QWidget* buildTranslatePage();
    void applyOrientation(bool horizontal);
    void saveLayoutState();
    Settings* m_settings;
    NotifyManager* m_notifyManager;
    QComboBox*      m_scenarioCombo  = nullptr;
    QComboBox*      m_targetLangCombo = nullptr;
    QPushButton*    m_layoutBtn      = nullptr;
    QPushButton*    m_resetLayoutBtn = nullptr;
    QPushButton*    m_translateBtn   = nullptr;
    QPushButton*    m_copyBtn        = nullptr;
    QPushButton*    m_clearBtn       = nullptr;
    QLabel*         m_statusLabel    = nullptr;
    QPlainTextEdit* m_sourceEdit     = nullptr;
    QPlainTextEdit* m_promptEdit     = nullptr;
    QPlainTextEdit* m_targetEdit     = nullptr;
    QPlainTextEdit* m_thinkEdit      = nullptr;
    QSplitter*      m_splitter       = nullptr;
    PivotPanel*     m_leftPanel      = nullptr;
    PivotPanel*     m_rightPanel     = nullptr;

    // 线程对象通过 finished → deleteLater 自动删除，QPointer 确保删除后
    // 成员自动置空，杜绝 onTranslate/closeEvent 访问悬空指针（use-after-free）。
    QPointer<TextTranslateService> m_service;
};
