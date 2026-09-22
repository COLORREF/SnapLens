#ifndef SNAPLENS_UI_SETUP_WIZARD_H
#define SNAPLENS_UI_SETUP_WIZARD_H

// ============================================================================
// 首次运行引导
//
// 完全基于 ElaWidgetTools 实现：外壳是 ElaDialog，步骤导轨复用 SnapUi::NavList，
// 内容区是 QStackedWidget —— 不再使用 QWizard / QWizardPage 这套 Qt 原生向导
// 控件（它们的标题带由样式引擎自绘，既不跟随 ElaTheme，也无法与整体观感统一）。
//
// 顶部右侧提供主题切换，可在引导过程中即时预览深浅色。
// ============================================================================

#include "snapkit.h"
#include "../settings.h"

#include <QDialog>
#include <QMap>
#include <QSet>
#include <QString>
#include <QVector>
#include <memory>

/// 步骤导轨：编号圆点 + 连接线 + 标题/说明。
///
/// 刻意不复用设置页的 NavList —— 设置是「并列分类」，引导是「先后步骤」，
/// 两者用不同的视觉语言才不会互相混淆。
class StepRail : public QWidget
{
    Q_OBJECT
public:
    explicit StepRail(QWidget* parent = nullptr);

    void addStep(const QString& title, const QString& description = {});
    void setCurrent(int index);
    int  count() const { return m_items.size(); }

signals:
    void stepClicked(int index);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    struct Item {
        QString title;
        QString description;
        QRect   rect;
    };
    void relayout();
    int  indexAt(const QPoint& pos) const;

    QList<Item> m_items;
    int m_current = 0;
    int m_hover = -1;
};

class QComboBox;
class QKeySequenceEdit;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QStackedWidget;
class QVBoxLayout;
class NotifyManager;
class ApiClient;

// ============================================================================
// SetupStep — 步骤基类
// ============================================================================

class SetupStep : public QWidget
{
    Q_OBJECT
public:
    SetupStep(const QString& title, const QString& subtitle,
              const QString& intro = QString(), QWidget* parent = nullptr);

    /// 当前步骤是否需要展示「完成」而非「下一步」。
    virtual bool isFinishStep() const { return false; }
    /// 进入该步骤时的回调（例如汇总页刷新摘要）。
    virtual void onEnter() {}

protected:
    /// 步骤内容的纵向布局（标题/说明已就绪）。
    QVBoxLayout* body() const { return m_body; }

private:
    QVBoxLayout* m_body = nullptr;
};

// ============================================================================
// 各步骤
// ============================================================================

class WelcomeStep : public SetupStep
{
    Q_OBJECT
public:
    explicit WelcomeStep(QWidget* parent = nullptr);
};

class ModeStep : public SetupStep
{
    Q_OBJECT
public:
    explicit ModeStep(QWidget* parent = nullptr);
    QString getMode() const;

private:
    ElaRadioButton* m_translateRadio = nullptr;
    ElaRadioButton* m_screenshotRadio = nullptr;
};

class HotkeyStep : public SetupStep
{
    Q_OBJECT
public:
    explicit HotkeyStep(QWidget* parent = nullptr);
    QString getHotkey() const;

private:
    QKeySequenceEdit* m_hotkeyEdit = nullptr;
};

class AiStep : public SetupStep
{
    Q_OBJECT
public:
    explicit AiStep(QWidget* parent = nullptr);
    ~AiStep() override;  // unique_ptr<ApiClient> 需要完整类型

    QString getProvider() const;
    QString getApiKey() const;
    QString getApiBase() const;
    QString getModel() const;
    QString getTargetLang() const;

private slots:
    void onProviderChanged(int index);
    void onFetchModels();

private:
    QComboBox*   m_providerCombo = nullptr;
    QLineEdit*   m_apiKeyEdit = nullptr;
    QLineEdit*   m_apiBaseEdit = nullptr;
    QComboBox*   m_modelCombo = nullptr;
    QPushButton* m_fetchModelsBtn = nullptr;
    ElaText*     m_errorLabel = nullptr;
    QWidget*     m_errorRow = nullptr;
    QComboBox*   m_targetLangCombo = nullptr;

    QMap<QString, QString> m_providerKeys;
    QString                m_currentProvider;

    std::unique_ptr<ApiClient> m_aiClient;  // 随步骤析构取消请求
    int m_fetchGen = 0;                     // 代次：丢弃过期结果
};

class OcrStep : public SetupStep
{
    Q_OBJECT
public:
    explicit OcrStep(QWidget* parent = nullptr);
    QString getSelectedLangs() const;

private:
    struct LangEntry { const char* code; const char* name; };
    static const QVector<LangEntry> LANG_MAP;
    static QSet<QString> scanInstalled(const QString& tessdataDir);

    QMap<QString, ElaCheckBox*> m_checks;
};

class SaveStep : public SetupStep
{
    Q_OBJECT
public:
    explicit SaveStep(QWidget* parent = nullptr);
    QString getSaveDir() const;
    QString getFormat() const;

private:
    void browse();

    QLineEdit* m_saveDirEdit = nullptr;
    QComboBox* m_formatCombo = nullptr;
};

class FinishStep : public SetupStep
{
    Q_OBJECT
public:
    explicit FinishStep(QWidget* parent = nullptr);
    bool isFinishStep() const override { return true; }

    void setSummary(const QString& text);

private:
    ElaText* m_summary = nullptr;
};

// ============================================================================
// SetupWizard
// ============================================================================

class SetupWizard : public ElaDialog
{
    Q_OBJECT
public:
    explicit SetupWizard(NotifyManager* notifyManager = nullptr, QWidget* parent = nullptr);

    Settings buildSettings() const;
    void reject() override;

    /// 供视觉回归/自动化驱动步骤。
    void selectStep(int index);
    int  stepCount() const;

private:
    void buildUi();
    void goToStep(int index);
    void updateFooter();
    void refreshSummary();

    NotifyManager* m_notifyManager = nullptr;
    bool m_rejecting = false;

    StepRail*        m_rail = nullptr;
    QStackedWidget*  m_stack = nullptr;
    ElaText*         m_progressLabel = nullptr;
    QPushButton*     m_backBtn = nullptr;
    QPushButton*     m_nextBtn = nullptr;
    QPushButton*     m_cancelBtn = nullptr;

    WelcomeStep* m_stepWelcome = nullptr;
    ModeStep*    m_stepMode = nullptr;
    HotkeyStep*  m_stepHotkey = nullptr;
    AiStep*      m_stepAi = nullptr;
    OcrStep*     m_stepOcr = nullptr;
    SaveStep*    m_stepSave = nullptr;
    FinishStep*  m_stepFinish = nullptr;
};

#endif // SNAPLENS_UI_SETUP_WIZARD_H
