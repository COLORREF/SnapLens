#ifndef SNAPLENS_UI_SETTINGS_DIALOG_H
#define SNAPLENS_UI_SETTINGS_DIALOG_H

#include <QDialog>
#include <QMap>
#include <QVariant>
#include <QString>
#include <QStringList>
#include <QVector>
#include <functional>
#include <memory>

class QCheckBox;
class QComboBox;
class QFormLayout;
class QKeySequenceEdit;
class QLineEdit;
class QSlider;
class QLabel;
class QPushButton;
class QTextEdit;
class QScrollArea;
class Settings;
class NotifyManager;
class ColorPickerButton;
class ApiClient;

// ============================================================================
// OcrLangManager — OCR language pack management widget
// ============================================================================

class NotifyManager;
class QNetworkAccessManager;
class QNetworkReply;
class QProgressBar;

class OcrLangManager : public QWidget {
    Q_OBJECT
public:
    // customTessdataDir：用户在设置中自定义的语言包目录（非空优先使用），
    // 为空时自动检测（findTessdataDir）。
    explicit OcrLangManager(const QString& currentLangs, const QString& customTessdataDir,
                            NotifyManager* notifyManager,
                            QWidget* settingsDialogParent = nullptr, QWidget* parent = nullptr);

    QString getSelectedLangs() const;

private:
    void rebuildUi();
    void startDownload(const QString& code, const QString& name, const QString& url);
    void deleteLang(const QString& code, const QString& name);

    QString        m_tessdataDir;
    QSet<QString>  m_selected;
    QSet<QString>  m_installed;
    NotifyManager* m_notifyManager = nullptr;
    QWidget*       m_settingsDialogParent = nullptr;
    QMap<QString, QCheckBox*> m_checks;

    // Download tracking — key → (reply, name)
    QMap<QString, QPair<QNetworkReply*, QString>> m_downloads;
    QNetworkAccessManager* m_nam = nullptr;
};

// ============================================================================
// SettingsDialog
// ============================================================================

class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit SettingsDialog(Settings* settings, NotifyManager* notifyManager,
                            QWidget* parent = nullptr);

    ~SettingsDialog() override;  // 在 .cpp 定义（unique_ptr<ApiClient> 需完整类型）

    void accept() override;

private slots:
    void onProviderChanged(int index);
    void onFetchModels();
    void onResetAll();

private:
    // Tab builders
    QWidget* buildScreenshotTab();
    QWidget* buildMagnifierTab();
    QWidget* buildLabelsTab();
    QWidget* buildStorageTab();
    QWidget* buildOcrTab();
    QWidget* buildAiSettingsTab();
    QWidget* buildTranslateTab();
    QWidget* buildGeneralTab();

    // Helpers
    static QWidget* wrapScroll(QWidget* w);
    static void     addSep(QFormLayout* f);
    static QPair<QWidget*, QSlider*> makeSlider(int val, int lo, int hi, const QString& suffix = "%");
    static QPair<QWidget*, QSlider*> makeDecimalSlider(double val, double lo, double hi, int decimals = 2);

    // Toggle helpers
    void toggleMagnifierWidgets(bool on);
    void toggleWheelZoomWidgets(bool on);
    void toggleGridWidgets(bool on);
    void toggleEdgeColorWidgets(bool on);
    void toggleZoomLabelWidgets(bool on);
    void toggleCoordLabelWidgets(bool on);
    void toggleColorLabelWidgets(bool on);
    void toggleCrosshairWidgets(bool invert);
    void toggleMagCrossWidgets(bool invert);

    // ---- 表驱动重置（对齐 Python `_reset_map`）----
    // 每个条目 = (所属 tab, 用默认值设置控件的动作)。
    // 新增设置项只需在 buildResetTable() 加一行，重置自动生效，无需再写 reset 方法。
    struct ResetItem {
        QString                       tab;
        std::function<void(const Settings&)> apply;  // 参数为默认值 Settings
    };

    bool confirmReset(const QString& scope);
    void resetTab(const QString& tab, const QString& scope);
    void buildResetTable();
    void refreshToggleStates();

    // Browse helpers
    void browseSaveDir();
    void browseConfigDir();
    void resetConfigDir();
    void browseDir(QLineEdit* edit, const QString& title);

    Settings*      m_settings       = nullptr;
    NotifyManager* m_notifyManager  = nullptr;

    QMap<QString, QString> m_providerKeys;
    QString                m_currentProvider;

    QVector<ResetItem>     m_resetItems;  // 表驱动重置注册表

    // ---- Screenshot tab ----
    QKeySequenceEdit* m_hotkeyEdit          = nullptr;
    QSlider*          m_toolbarOpacity      = nullptr;
    ColorPickerButton* m_crosshairColor     = nullptr;
    QSlider*          m_crosshairAlpha       = nullptr;
    QSlider*          m_crosshairThickness   = nullptr;
    QCheckBox*        m_crosshairInvert      = nullptr;
    QCheckBox*        m_crosshairEnabled     = nullptr;
    QCheckBox*        m_cursorEnabled        = nullptr;
    QList<QWidget*>   m_crosshairColorWidgets;

    // ---- Magnifier tab ----
    QCheckBox*        m_magnifierEnabled    = nullptr;
    QSlider*          m_magnifierZoom        = nullptr;
    QSlider*          m_magnifierFixedZoom   = nullptr;
    QCheckBox*        m_magnifierWheelZoom   = nullptr;
    QSlider*          m_magnifierSize        = nullptr;
    ColorPickerButton* m_magCrossColor       = nullptr;
    QSlider*          m_magCrossAlpha        = nullptr;
    QSlider*          m_magCrossThickness    = nullptr;
    QCheckBox*        m_magCrossInvert       = nullptr;
    QCheckBox*        m_gridEnabled          = nullptr;
    ColorPickerButton* m_gridColor           = nullptr;
    QSlider*          m_gridAlpha            = nullptr;
    QComboBox*        m_edgeModeCombo        = nullptr;
    ColorPickerButton* m_edgeColor           = nullptr;
    QCheckBox*        m_zoomLabelEnabled     = nullptr;
    ColorPickerButton* m_zoomLabelTextColor   = nullptr;
    QSlider*          m_zoomLabelTextAlpha    = nullptr;
    ColorPickerButton* m_zoomLabelBgColor     = nullptr;
    QSlider*          m_zoomLabelBgAlpha      = nullptr;

    QList<QWidget*>   m_magnifierSubWidgets;
    QList<QWidget*>   m_wheelZoomSubWidgets;
    QList<QWidget*>   m_fixedZoomSubWidgets;
    QList<QWidget*>   m_gridSubWidgets;
    QList<QWidget*>   m_edgeColorWidgets;
    QList<QWidget*>   m_zoomLabelSubWidgets;
    QList<QWidget*>   m_magCrossColorWidgets;

    // ---- Labels tab ----
    QCheckBox*        m_coordLabelEnabled    = nullptr;
    ColorPickerButton* m_coordTextColor      = nullptr;
    QSlider*          m_coordTextAlpha       = nullptr;
    ColorPickerButton* m_coordBgColor        = nullptr;
    QSlider*          m_coordBgAlpha         = nullptr;
    QCheckBox*        m_colorLabelEnabled    = nullptr;
    ColorPickerButton* m_colorTextColor      = nullptr;
    QSlider*          m_colorTextAlpha       = nullptr;
    ColorPickerButton* m_colorBgColor        = nullptr;
    QSlider*          m_colorBgAlpha         = nullptr;
    QComboBox*        m_colorFormatCombo     = nullptr;
    QKeySequenceEdit* m_copyColorKeyEdit     = nullptr;
    QCheckBox*        m_copyHexPrefix        = nullptr;
    QCheckBox*        m_copyRgbPrefix        = nullptr;

    QList<QWidget*>   m_coordLabelSubWidgets;
    QList<QWidget*>   m_colorLabelSubWidgets;

    // ---- Storage tab ----
    QLineEdit*        m_dirEdit              = nullptr;
    QComboBox*        m_formatCombo          = nullptr;
    QLineEdit*        m_configEdit           = nullptr;

    // ---- OCR tab ----
    OcrLangManager*   m_ocrLangList          = nullptr;
    QLineEdit*        m_ocrTessdataEdit      = nullptr;

    // ---- AI Settings tab ----
    QComboBox*        m_aiProviderCombo      = nullptr;
    QLineEdit*        m_aiApiKeyEdit         = nullptr;
    QLineEdit*        m_aiApiBaseEdit        = nullptr;
    QComboBox*        m_aiModelCombo         = nullptr;
    QPushButton*      m_fetchModelsBtn       = nullptr;
    QLabel*           m_aiErrorLabel         = nullptr;
    QSlider*          m_aiTimeout            = nullptr;
    QSlider*          m_aiTemperature        = nullptr;
    QSlider*          m_aiMaxTokens          = nullptr;
    QSlider*          m_aiTopP               = nullptr;
    QSlider*          m_aiFrequencyPenalty   = nullptr;
    QSlider*          m_aiPresencePenalty    = nullptr;
    QLineEdit*        m_aiSeedEdit           = nullptr;

    // L11 异步化：模型获取专用客户端。
    // unique_ptr 成员生命周期与对话框一致 —— 对话框析构时 m_aiClient 析构，
    // 底层请求自动取消、回调连接自动断开（绝无 use-after-free）；
    // 回调仍在主线程派发，无跨线程问题。
    std::unique_ptr<ApiClient> m_aiClient;
    // 模型获取代次：每次发起获取 / 恢复本页默认 / 恢复全部默认时递增，
    // 回调校验代次以丢弃过期结果（防止"重置后被进行中的请求覆盖"）。
    int m_fetchGen = 0;

    // ---- Translate tab ----
    QComboBox*        m_aiTargetLangCombo    = nullptr;
    QCheckBox*        m_aiConfirmBefore      = nullptr;
    QCheckBox*        m_aiStreamThinking     = nullptr;
    QTextEdit*        m_aiPromptEdit         = nullptr;
    QTextEdit*        m_textPromptEdit       = nullptr;

    // ---- General tab ----
    QComboBox*        m_appModeCombo         = nullptr;
    QComboBox*        m_closeToTrayCombo     = nullptr;
    QCheckBox*        m_notifyCopy           = nullptr;
    QCheckBox*        m_notifySaveSuccess    = nullptr;
    QCheckBox*        m_notifySaveFail       = nullptr;
    QCheckBox*        m_notifyCaptureFail    = nullptr;
    QCheckBox*        m_notifyHotkeyFail     = nullptr;
    QCheckBox*        m_notifyTransSuccess   = nullptr;
    QCheckBox*        m_notifyTransFail      = nullptr;
    QCheckBox*        m_notifyOcrFail        = nullptr;
    QCheckBox*        m_notifyLangDownload   = nullptr;
    QCheckBox*        m_logDebugEnabled      = nullptr;
    QCheckBox*        m_logInfoEnabled       = nullptr;
    QCheckBox*        m_logWarningEnabled    = nullptr;
    QCheckBox*        m_logErrorEnabled      = nullptr;
};

#endif
