#ifndef SNAPLENS_TRANSLATE_MAIN_WINDOW_H
#define SNAPLENS_TRANSLATE_MAIN_WINDOW_H

#include <QMainWindow>
#include <QDialog>
#include <QString>
#include <QPointer>
#include <utility>

class QComboBox;
class QPushButton;
class QPlainTextEdit;
class QCheckBox;
class QSplitter;
class QLabel;
class QTabWidget;
class Settings;
class NotifyManager;
class TextTranslateService;

// ============================================================================
// CloseDialog — 关闭窗口确认弹窗（匹配 Python _CloseDialog）
// ============================================================================

class CloseDialog : public QDialog
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

// ============================================================================
// SplitTabPanel — 可合并/分离的双面板（使用 QTabWidget 而非裸 QSplitter）
// ============================================================================

class SplitTabPanel : public QWidget
{
    Q_OBJECT
public:
    SplitTabPanel(const QString& title1, const QString& title2,
                  QPlainTextEdit* edit1, QPlainTextEdit* edit2,
                  bool defaultMerged, QWidget* parent = nullptr);

    void applyState(bool merged, double ratio);
    bool isMerged() const;
    double ratio() const;
    QSplitter* splitter;

signals:
    void stateChanged();

private:
    void onDoubleClicked(bool toA);
    void mergeBintoA();
    void mergeAintoB();
    void split();

    QPlainTextEdit* m_edit1;
    QPlainTextEdit* m_edit2;
    QString m_title1, m_title2;
    QTabWidget* m_tabA  = nullptr;
    QTabWidget* m_tabB  = nullptr;
    bool m_docked   = false;
    bool m_reverse  = false;
    double m_savedRatio = 0.7;
};

// ============================================================================
// MainWindow
// ============================================================================

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(Settings* settings,
                        NotifyManager* notifyManager = nullptr,
                        const QString& appMode = "translate",
                        QWidget* parent = nullptr);

    // 是否有文本翻译任务正在运行（供设置保存时"下次生效"提示检测）
    bool isBusy() const;

signals:
    void settingsRequested();

protected:
    void closeEvent(QCloseEvent* event) override;

private slots:
    void onTranslate();
    void onTranslated(const QString& text);
    void onThinking(const QString& text);
    void onError(const QString& msg);
    void toggleLayout();
    void resetLayout();
    void refreshPromptPreview();

private:
    void buildUi();
    void saveLayoutState();

    Settings*       m_settings;
    NotifyManager*  m_notifyManager;
    QString         m_appMode;

    QComboBox*      m_scenarioCombo  = nullptr;
    QComboBox*      m_targetLangCombo = nullptr;
    QPushButton*    m_settingsBtn    = nullptr;
    QPushButton*    m_layoutBtn      = nullptr;
    QPushButton*    m_resetLayoutBtn = nullptr;
    QPushButton*    m_translateBtn   = nullptr;
    QPlainTextEdit* m_sourceEdit     = nullptr;
    QPlainTextEdit* m_promptEdit     = nullptr;
    QPlainTextEdit* m_targetEdit     = nullptr;
    QPlainTextEdit* m_thinkEdit      = nullptr;
    QSplitter*      m_splitter       = nullptr;
    SplitTabPanel*  m_leftPanel      = nullptr;
    SplitTabPanel*  m_rightPanel     = nullptr;

    // 线程对象通过 finished → deleteLater 自动删除，QPointer 确保删除后
    // 成员自动置空，杜绝 onTranslate/closeEvent 访问悬空指针（use-after-free）。
    QPointer<TextTranslateService> m_service;
};

#endif
