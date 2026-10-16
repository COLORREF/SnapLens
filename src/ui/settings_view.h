#ifndef SNAPLENS_UI_SETTINGS_VIEW_H
#define SNAPLENS_UI_SETTINGS_VIEW_H

// ============================================================================
// SettingsView — 应用内嵌的设置页
//
// 设置不再是独立弹窗，而是主窗口里的一页（由 MainWindow 挂到页脚「设置」节点）。
// 页面结构：左侧分类导航 + 右侧分区/设置行 + 底部操作栏。
//
// 交互语义与旧弹窗保持一致：
//   · 保存设置 —— 校验并落盘，成功后 emit settingsSaved()
//   · 放弃修改 —— 从已保存的配置回填所有控件（等价于旧弹窗的「取消」）
//   · 恢复全部默认 —— 重置为出厂默认并立即落盘
// ============================================================================

#include "snapkit.h"

#include <QMap>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVector>
#include <functional>
#include <memory>

class QCheckBox;
class QComboBox;
class QKeySequenceEdit;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QSlider;
class QStackedWidget;
class QVBoxLayout;
class QNetworkAccessManager;
class QNetworkReply;
class QProgressBar;
class Settings;
class NotifyManager;
class ColorPickerButton;
class ApiClient;

// ============================================================================
// OcrLangManager — OCR 语言包管理控件
// ============================================================================

class OcrLangManager : public QWidget {
    Q_OBJECT
public:
    /// customTessdataDir：用户自定义的语言包目录（非空优先使用），为空时自动检测。
    explicit OcrLangManager(const QString& currentLangs, const QString& customTessdataDir,
                            NotifyManager* notifyManager,
                            QWidget* settingsViewParent = nullptr, QWidget* parent = nullptr);

    QString getSelectedLangs() const;
    void reload(const QString& languages, const QString& directory);

private:
    void rebuildUi();
    void startDownload(const QString& code, const QString& name, const QString& url);
    void deleteLang(const QString& code, const QString& name);

    QString        m_tessdataDir;
    QSet<QString>  m_selected;
    QSet<QString>  m_installed;
    NotifyManager* m_notifyManager = nullptr;
    QWidget*       m_settingsViewParent = nullptr;
    QMap<QString, QCheckBox*> m_checks;

    // Download tracking — key → (reply, name)
    QMap<QString, QPair<QNetworkReply*, QString>> m_downloads;
    QNetworkAccessManager* m_nam = nullptr;
};

// ============================================================================
// SettingsView
// ============================================================================

class SettingsView : public QWidget {
    Q_OBJECT
public:
    explicit SettingsView(Settings* settings, NotifyManager* notifyManager,
                          QWidget* parent = nullptr);

    ~SettingsView() override;  // 在 .cpp 定义（unique_ptr<ApiClient> 需完整类型）

    /// 供快照/自动化切页。
    void selectPage(int index);
    void selectCategory(const QString& id);
    int  pageCount() const;

signals:
    /// 配置已通过校验并成功落盘（宿主窗口据此重新应用热键/日志级别/启动模式等）。
    void settingsSaved();

private slots:
    void onSave();
    void onRevert();
    void onResetAll();
    void onProviderChanged(int index);
    void onFetchModels();

private:
    // ---- 装配 ----
    void buildUi();
    QWidget* buildBody();
    QWidget* buildScreenshotTab();
    QWidget* buildMagnifierTab();
    QWidget* buildLabelsTab();
    QWidget* buildStorageTab();
    QWidget* buildOcrTab();
    QWidget* buildAiSettingsTab();
    QWidget* buildTranslateTab();
    QWidget* buildGeneralTab();

    /// 在页面末尾追加「恢复本页默认」按钮行。
    void appendPageReset(SnapUi::SettingsPage* page, const QString& tabId,
                         const QString& tabLabel);
    /// 颜色行。
    SnapUi::SettingsCard* colorRow(SnapUi::SettingsPage* page, const QString& title,
                                   const QString& desc, const QString& hex,
                                   ColorPickerButton** out);

    // ---- 数据流 ----
    /// 控件 → 配置；校验失败返回 false（并已提示）。
    bool saveToSettings();
    /// 配置 → 控件（「放弃修改」与初始化共用）。
    void loadFromSettings();

    // ---- 联动（从属设置不适用时隐藏整张卡片，保留控件值）----
    // 每个刷新方法都从控件当前状态重新计算，避免"多个开关互相覆盖"的状态机 bug。
    void refreshScreenshotPage();
    void refreshMagnifierPage();
    void refreshLabelsPage();

    // ---- 表驱动重置（对齐 Python `_reset_map`）----
    struct ResetItem {
        QString                              tab;
        std::function<void(const Settings&)> apply;  // 参数为默认值 Settings
    };

    bool confirmReset(const QString& scope);
    void resetTab(const QString& tab, const QString& scope);
    void buildResetTable();
    void refreshToggleStates();

    // ---- 浏览辅助 ----
    void browseSaveDir();
    void browseConfigDir();
    void resetConfigDir();
    void browseDir(QLineEdit* edit, const QString& title);

    Settings*      m_settings      = nullptr;
    NotifyManager* m_notifyManager = nullptr;

    QMap<QString, QString> m_providerKeys;
    QMap<QString, QString> m_providerBases;
    QMap<QString, QString> m_providerModels;
    QString                m_currentProvider;

    QVector<ResetItem> m_resetItems;  // 表驱动重置注册表

    // ---- 页面骨架 ----
    QWidget*        m_body = nullptr;    // 导航 + 内容卡片
    /// @brief 使用与主窗口相同的 Ela 导航；页面键对应右侧堆叠页索引。
    ElaNavigationBar* m_nav = nullptr;
    QStringList m_navPages;
    QStringList m_navGroups;
    ElaLineEdit* m_navSearch = nullptr;
    QMap<QString, int> m_categories;
    QStackedWidget* m_stack = nullptr;

    // ---- 截图页 ----
    QKeySequenceEdit*  m_hotkeyEdit        = nullptr;
    QSlider*           m_toolbarOpacity    = nullptr;
    ColorPickerButton* m_crosshairColor    = nullptr;
    QSlider*           m_crosshairAlpha    = nullptr;
    QSlider*           m_crosshairThickness = nullptr;
    ElaToggleSwitch*   m_crosshairInvert   = nullptr;
    ElaToggleSwitch*   m_crosshairEnabled  = nullptr;
    ElaToggleSwitch* m_cursorKeyboardEnabled = nullptr;
    QComboBox* m_cursorKeyboardMode = nullptr;
    ElaToggleSwitch*   m_cursorEnabled     = nullptr;
    QList<QWidget*>    m_screenshotCrosshairRows;
    QList<QWidget*>    m_screenshotCrosshairColorRows;

    // ---- 放大镜页 ----
    ElaToggleSwitch*   m_magnifierEnabled  = nullptr;
    QSlider*           m_magnifierZoom     = nullptr;
    QSlider*           m_magnifierFixedZoom = nullptr;
    ElaToggleSwitch*   m_magnifierWheelZoom = nullptr;
    QSlider*           m_magnifierSize     = nullptr;
    ColorPickerButton* m_magCrossColor     = nullptr;
    QSlider*           m_magCrossAlpha     = nullptr;
    QSlider*           m_magCrossThickness = nullptr;
    ElaToggleSwitch*   m_magCrossInvert    = nullptr;
    ElaToggleSwitch*   m_gridEnabled       = nullptr;
    ColorPickerButton* m_gridColor         = nullptr;
    QSlider*           m_gridAlpha         = nullptr;
    QComboBox*         m_edgeModeCombo     = nullptr;
    ColorPickerButton* m_edgeColor         = nullptr;
    ElaToggleSwitch*   m_zoomLabelEnabled  = nullptr;
    ColorPickerButton* m_zoomLabelTextColor = nullptr;
    QSlider*           m_zoomLabelTextAlpha = nullptr;
    ColorPickerButton* m_zoomLabelBgColor  = nullptr;
    QSlider*           m_zoomLabelBgAlpha  = nullptr;

    QList<QWidget*>    m_magnifierRows;      // 放大镜页全部从属行
    QList<QWidget*>    m_wheelZoomRows;
    QList<QWidget*>    m_fixedZoomRows;
    QList<QWidget*>    m_gridRows;
    QList<QWidget*>    m_edgeColorRows;
    QList<QWidget*>    m_zoomLabelRows;
    QList<QWidget*>    m_magCrossColorRows;

    // ---- 坐标与颜色页 ----
    ElaToggleSwitch*   m_coordLabelEnabled = nullptr;
    ColorPickerButton* m_coordTextColor    = nullptr;
    QSlider*           m_coordTextAlpha    = nullptr;
    ColorPickerButton* m_coordBgColor      = nullptr;
    QSlider*           m_coordBgAlpha      = nullptr;
    ElaToggleSwitch*   m_colorLabelEnabled = nullptr;
    ColorPickerButton* m_colorTextColor    = nullptr;
    QSlider*           m_colorTextAlpha    = nullptr;
    ColorPickerButton* m_colorBgColor      = nullptr;
    QSlider*           m_colorBgAlpha      = nullptr;
    QComboBox*         m_colorFormatCombo  = nullptr;
    QKeySequenceEdit*  m_copyColorKeyEdit  = nullptr;
    QComboBox* m_colorFormatKeyMode = nullptr;
    QKeySequenceEdit* m_colorFormatKeyEdit = nullptr;
    ElaToggleSwitch* m_colorSwatchEnabled = nullptr;
    ElaToggleSwitch*   m_copyHexPrefix     = nullptr;
    ElaToggleSwitch*   m_copyRgbPrefix     = nullptr;

    QList<QWidget*>    m_coordLabelRows;
    QList<QWidget*>    m_colorLabelRows;

    // ---- 存储页 ----
    QLineEdit*         m_dirEdit           = nullptr;
    QComboBox*         m_formatCombo       = nullptr;
    QLineEdit*         m_configEdit        = nullptr;

    // ---- OCR 页 ----
    OcrLangManager*    m_ocrLangList       = nullptr;
    QLineEdit*         m_ocrTessdataEdit   = nullptr;

    // ---- AI 服务页 ----
    QComboBox*         m_aiProviderCombo   = nullptr;
    QLineEdit*         m_aiApiKeyEdit      = nullptr;
    QLineEdit*         m_aiApiBaseEdit     = nullptr;
    QLineEdit*         m_aiCustomModelsEdit = nullptr;
    QWidget*           m_aiCustomModelsCard = nullptr;
    QComboBox*         m_aiModelCombo      = nullptr;
    QPushButton*       m_fetchModelsBtn    = nullptr;
    ElaText*           m_aiErrorLabel      = nullptr;
    QWidget*           m_aiErrorRow        = nullptr;
    QSlider*           m_aiTimeout         = nullptr;
    QSlider*           m_aiTemperature     = nullptr;
    QSlider*           m_aiMaxTokens       = nullptr;
    QSlider*           m_aiTopP            = nullptr;
    QSlider*           m_aiFrequencyPenalty = nullptr;
    QSlider*           m_aiPresencePenalty = nullptr;
    QLineEdit*         m_aiSeedEdit        = nullptr;

    // 模型获取专用客户端；随视图析构自动取消请求。
    std::unique_ptr<ApiClient> m_aiClient;
    // 代次：每次发起获取 / 恢复默认时递增，回调据此丢弃过期结果。
    int m_fetchGen = 0;

    // ---- 翻译页 ----
    QComboBox*         m_aiTargetLangCombo = nullptr;
    ElaToggleSwitch*   m_aiConfirmBefore   = nullptr;
    ElaToggleSwitch*   m_aiStreamThinking  = nullptr;
    QPlainTextEdit*    m_aiPromptEdit      = nullptr;
    QPlainTextEdit*    m_textPromptEdit    = nullptr;

    // ---- 常规页 ----
    QComboBox*         m_themeCombo        = nullptr;
    QComboBox*         m_appModeCombo      = nullptr;
    QComboBox*         m_closeToTrayCombo  = nullptr;
    ElaToggleSwitch*   m_notifyCopy        = nullptr;
    ElaToggleSwitch*   m_notifySaveSuccess = nullptr;
    ElaToggleSwitch*   m_notifySaveFail    = nullptr;
    ElaToggleSwitch*   m_notifyCaptureFail = nullptr;
    ElaToggleSwitch*   m_notifyHotkeyFail  = nullptr;
    ElaToggleSwitch*   m_notifyTransSuccess = nullptr;
    ElaToggleSwitch*   m_notifyTransFail   = nullptr;
    ElaToggleSwitch*   m_notifyOcrFail     = nullptr;
    ElaToggleSwitch*   m_notifyLangDownload = nullptr;
    ElaToggleSwitch*   m_logDebugEnabled   = nullptr;
    ElaToggleSwitch*   m_logInfoEnabled    = nullptr;
    ElaToggleSwitch*   m_logWarningEnabled = nullptr;
    ElaToggleSwitch*   m_logErrorEnabled   = nullptr;
};

#endif // SNAPLENS_UI_SETTINGS_VIEW_H
