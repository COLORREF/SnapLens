#ifndef SNAPLENS_UI_SETUP_WIZARD_H
#define SNAPLENS_UI_SETUP_WIZARD_H

#include <QWizard>
#include <QWizardPage>
#include <QMap>
#include <QSet>
#include <QVariant>
#include <QString>
#include <memory>

#include "../settings.h"

class QCheckBox;
class QComboBox;
class QKeySequenceEdit;
class QLineEdit;
class QPushButton;
class QRadioButton;
class QButtonGroup;
class QLabel;
class NotifyManager;
class ApiClient;

// ============================================================================
// Pages
// ============================================================================

class WelcomePage : public QWizardPage {
    Q_OBJECT
public:
    explicit WelcomePage(QWidget* parent = nullptr);
};

class ModePage : public QWizardPage {
    Q_OBJECT
public:
    explicit ModePage(QWidget* parent = nullptr);
    QString getMode() const;

private:
    QRadioButton* m_translateRadio = nullptr;
    QRadioButton* m_screenshotRadio = nullptr;
    QButtonGroup* m_modeGroup = nullptr;
};

class HotkeyPage : public QWizardPage {
    Q_OBJECT
public:
    explicit HotkeyPage(QWidget* parent = nullptr);
    QString getHotkey() const;

private:
    QKeySequenceEdit* m_hotkeyEdit = nullptr;
};

class AiTranslationPage : public QWizardPage {
    Q_OBJECT
public:
    explicit AiTranslationPage(QWidget* parent = nullptr);
    ~AiTranslationPage() override;  // 在 .cpp 定义（unique_ptr<ApiClient> 需完整类型）

    QString getProvider() const;
    QString getApiKey()   const;
    QString getApiBase()  const;
    QString getModel()    const;
    QString getTargetLang() const;

private slots:
    void onProviderChanged(int index);
    void onFetchModels();

private:
    QComboBox*  m_providerCombo  = nullptr;
    QLineEdit*  m_apiKeyEdit     = nullptr;
    QLineEdit*  m_apiBaseEdit    = nullptr;
    QComboBox*  m_modelCombo     = nullptr;
    QPushButton* m_fetchModelsBtn = nullptr;
    QComboBox*  m_targetLangCombo = nullptr;
    QLabel*     m_errorLabel     = nullptr;

    QMap<QString, QString> m_providerKeys;
    QString                m_currentProvider;

    // L11 异步化：模型获取客户端（成员生命周期 = 页面生命周期，
    // 页面析构自动取消请求并断开回调，无 use-after-free）
    std::unique_ptr<ApiClient> m_aiClient;
    // 获取代次：再次获取时递增，回调校验代次丢弃过期结果
    int m_fetchGen = 0;
};

class OcrPage : public QWizardPage {
    Q_OBJECT
public:
    explicit OcrPage(QWidget* parent = nullptr);
    QString getSelectedLangs() const;

private:
    struct LangEntry {
        QString code;
        QString name;
    };
    static const QVector<LangEntry> LANG_MAP;
    static QSet<QString> scanInstalled(const QString& tessdataDir);

    QMap<QString, QCheckBox*> m_checks;
};

class SavePage : public QWizardPage {
    Q_OBJECT
public:
    explicit SavePage(QWidget* parent = nullptr);
    QString getSaveDir()  const;
    QString getFormat()   const;

private slots:
    void browse();

private:
    QLineEdit* m_saveDirEdit   = nullptr;
    QComboBox* m_formatCombo   = nullptr;
};

class FinishPage : public QWizardPage {
    Q_OBJECT
public:
    explicit FinishPage(QWidget* parent = nullptr);
    void setSummary(const QString& text);

private:
    QLabel* m_summary = nullptr;
};

// ============================================================================
// Wizard
// ============================================================================

class SetupWizard : public QWizard {
    Q_OBJECT
public:
    explicit SetupWizard(NotifyManager* notifyManager = nullptr, QWidget* parent = nullptr);

    Settings buildSettings() const;
    void reject() override;

private slots:
    void onPageChanged(int pageId);

private:
    NotifyManager* m_notifyManager = nullptr;

    WelcomePage*       m_pageWelcome = nullptr;
    ModePage*          m_pageMode    = nullptr;
    HotkeyPage*        m_pageHotkey  = nullptr;
    AiTranslationPage* m_pageAi      = nullptr;
    OcrPage*           m_pageOcr     = nullptr;
    SavePage*          m_pageSave    = nullptr;
    FinishPage*        m_pageFinish  = nullptr;
};

#endif // SNAPLENS_UI_SETUP_WIZARD_H
