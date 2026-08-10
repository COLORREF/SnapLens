#include "settings_dialog.h"
#include "color_picker.h"
#include "../settings.h"
#include "../notify/notify_manager.h"
#include "../ocr/ocr_engine.h"
#include "../translate/providers.h"
#include "../translate/ai_client.h"
#include "../log/log_manager.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QSlider>
#include <QTabWidget>
#include <QTextEdit>
#include <QVBoxLayout>
#include <QUrl>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <cmath>

// ============================================================================
// Helpers
// ============================================================================

QWidget* SettingsDialog::wrapScroll(QWidget* w) {
    auto* scroll = new QScrollArea();
    scroll->setWidgetResizable(true);
    scroll->setWidget(w);
    return scroll;
}

void SettingsDialog::addSep(QFormLayout* f) {
    auto* line = new QFrame();
    line->setFrameShape(QFrame::HLine);
    line->setFrameShadow(QFrame::Sunken);
    f->addRow(line);
}

QPair<QWidget*, QSlider*> SettingsDialog::makeSlider(int val, int lo, int hi,
                                                       const QString& suffix) {
    auto* w = new QWidget();
    auto* row = new QHBoxLayout(w);
    row->setContentsMargins(0, 0, 0, 0);
    auto* slider = new QSlider(Qt::Horizontal);
    slider->setRange(lo, hi);
    slider->setValue(val);
    auto* label = new QLabel(QString::number(val) + suffix);
    label->setFixedWidth(55);
    QObject::connect(slider, &QSlider::valueChanged,
                     label, [label, suffix](int v) { label->setText(QString::number(v) + suffix); });
    row->addWidget(slider);
    row->addWidget(label);
    return {w, slider};
}

QPair<QWidget*, QSlider*> SettingsDialog::makeDecimalSlider(double val, double lo, double hi,
                                                              int decimals) {
    int mult = static_cast<int>(std::pow(10, decimals));
    auto* w = new QWidget();
    auto* row = new QHBoxLayout(w);
    row->setContentsMargins(0, 0, 0, 0);
    auto* slider = new QSlider(Qt::Horizontal);
    slider->setRange(static_cast<int>(lo * mult), static_cast<int>(hi * mult));
    slider->setValue(static_cast<int>(val * mult));
    auto* label = new QLabel(QString::number(val, 'f', decimals));
    label->setFixedWidth(55);
    QObject::connect(slider, &QSlider::valueChanged, label,
        [label, decimals, mult](int v) { label->setText(QString::number(double(v) / mult, 'f', decimals)); });
    row->addWidget(slider);
    row->addWidget(label);
    return {w, slider};
}

// ============================================================================
// OcrLangManager
// ============================================================================

static QSet<QString> scanInstalledLangs(const QString& tessdataDir) {
    QSet<QString> installed;
    QDir dir(tessdataDir);
    if (!dir.exists()) return installed;
    for (const auto& fi : dir.entryInfoList({"*.traineddata"}, QDir::Files)) {
        QString code = fi.completeBaseName();
        if (code != "osd") installed.insert(code);
    }
    return installed;
}

static QMap<QString, QString> knownLangs() {
    return {
        {"chi_sim","中文简体"},{"chi_tra","中文繁体"},{"eng","英文"},{"jpn","日语"},
        {"kor","韩语"},{"fra","法语"},{"deu","德语"},{"spa","西班牙语"},{"rus","俄语"},
        {"ara","阿拉伯语"},{"tha","泰语"},{"vie","越南语"},{"por","葡萄牙语"},
        {"ita","意大利语"},{"nld","荷兰语"},{"pol","波兰语"},{"tur","土耳其语"},
        {"hin","印地语"},{"ind","印尼语"},{"msa","马来语"},{"nor","挪威语"},
        {"swe","瑞典语"},{"dan","丹麦语"},{"fin","芬兰语"},{"hun","匈牙利语"},
        {"ces","捷克语"},{"ron","罗马尼亚语"},{"ukr","乌克兰语"},{"heb","希伯来语"},
        {"ell","希腊语"},
    };
}

// Recursively clear a layout WITHOUT double-free.
//
// IMPORTANT Qt internals (qboxlayout.cpp):
//   * QBoxLayoutItem::~QBoxLayoutItem() does `delete item;` — it deletes the wrapped QLayoutItem.
//   * QBoxLayout::takeAt() returns the wrapped item with ownership transferred, and for a child
//     QLayout it also detaches that layout from its parent (l->setParent(nullptr)).
//   * For a child layout, the returned item IS the QLayout itself (QLayout derives from
//     QLayoutItem, and QLayout::layout() returns `this`).
//
// Therefore the previous code:
//     QLayout* sub = item->layout(); ... delete sub; ... delete item;
//   was deleting the SAME QLayout twice → heap double-free → crash.
//
// Correct approach: for a QLayout item, recursively clear its children first, then
// delete the layout ONCE. Widgets are owned by their parent widget, so schedule
// deleteLater() and let the parent clean them up.
static void clearLayoutRecursively(QLayout* lay) {
    if (!lay) return;
    while (lay->count()) {
        QLayoutItem* item = lay->takeAt(0);
        if (!item) continue;
        if (item->widget()) {
            // Widget owned by its parent widget — schedule deletion
            item->widget()->deleteLater();
        } else if (item->layout()) {
            // item IS the child layout (takeAt transferred ownership).
            // Recursively clear its children, then delete it once below.
            clearLayoutRecursively(item->layout());
        }
        delete item;
    }
}

OcrLangManager::OcrLangManager(const QString& currentLangs, const QString& customTessdataDir,
                               NotifyManager* notifyManager,
                               QWidget* settingsDialogParent, QWidget* parent)
    : QWidget(parent)
    , m_notifyManager(notifyManager)
    , m_settingsDialogParent(settingsDialogParent)
    , m_nam(new QNetworkAccessManager(this))
{
    // 优先使用用户在设置中自定义的语言包目录；未设置时自动检测。
    // （修复：此前始终用 findTessdataDir() 自动检测，自定义目录不生效）
    m_tessdataDir = customTessdataDir.isEmpty()
                        ? OcrEngine::findTessdataDir()
                        : customTessdataDir;
    if (!m_tessdataDir.isEmpty()) QDir().mkpath(m_tessdataDir);
    m_installed = scanInstalledLangs(m_tessdataDir);
    for (const auto& lang : currentLangs.split("+"))
        if (!lang.isEmpty()) m_selected.insert(lang);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);
    rebuildUi();
}

void OcrLangManager::rebuildUi() {
    auto* lay = qobject_cast<QVBoxLayout*>(layout());
    // Clear existing content — safe recursive cleanup (no double-free)
    clearLayoutRecursively(lay);
    m_checks.clear();

    if (m_tessdataDir.isEmpty()) {
        lay->addWidget(new QLabel("未检测到语言包目录，请确认 tessdata 目录可访问。"));
        return;
    }

    // 显示当前语言包目录（自定义目录时便于确认生效）
    lay->addWidget(new QLabel(QStringLiteral("语言包目录：%1").arg(m_tessdataDir)));

    auto allKnown = knownLangs();
    QMap<QString, QString> installedKnown, available;
    QSet<QString> downloadingCodes;
    for (auto it = m_downloads.begin(); it != m_downloads.end(); ++it)
        downloadingCodes.insert(it.key());

    for (auto it = allKnown.begin(); it != allKnown.end(); ++it) {
        if (m_installed.contains(it.key()) || downloadingCodes.contains(it.key()))
            installedKnown[it.key()] = it.value();
        else
            available[it.key()] = it.value();
    }
    QSet<QString> unknownInstalled = m_installed - QSet<QString>(allKnown.keyBegin(), allKnown.keyEnd());

    // Installed (with delete buttons, matching Python)
    if (!installedKnown.isEmpty() || !unknownInstalled.isEmpty() || !m_downloads.isEmpty()) {
        lay->addWidget(new QLabel("已安装（勾选即启用）："));
        QStringList sorted = installedKnown.keys();
        sorted.sort();
        for (const auto& code : sorted) {
            auto* row = new QHBoxLayout();
            bool isDownloading = downloadingCodes.contains(code);

            auto* cb = new QCheckBox(installedKnown[code] + " (" + code + ")");
            cb->setChecked(m_selected.contains(code));
            cb->setEnabled(!isDownloading);
            row->addWidget(cb);
            m_checks[code] = cb;

            if (isDownloading) {
                // Show progress bar during download
                auto* bar = new QProgressBar();
                bar->setRange(0, 100);
                bar->setValue(0);
                bar->setFixedWidth(120);
                bar->setFixedHeight(16);
                bar->setFormat("%p%");
                row->addWidget(bar);
                // Connect to the existing reply's downloadProgress
                auto* reply = m_downloads[code].first;
                connect(reply, &QNetworkReply::downloadProgress, bar,
                        [bar](qint64 received, qint64 total) {
                    if (total > 0) {
                        bar->setRange(0, static_cast<int>(total));
                        bar->setValue(static_cast<int>(received));
                    }
                });
            } else {
                // Delete button for installed langs
                auto* delBtn = new QPushButton("删除");
                delBtn->setFixedWidth(50);
                connect(delBtn, &QPushButton::clicked, this, [this, code, name = installedKnown[code]]() {
                    deleteLang(code, name);
                });
                row->addWidget(delBtn);
            }
            row->addStretch();
            lay->addLayout(row);
        }
        for (const auto& code : unknownInstalled) {
            auto* row = new QHBoxLayout();
            auto* cb = new QCheckBox(code + "（未知语言）");
            cb->setChecked(m_selected.contains(code));
            row->addWidget(cb);
            m_checks[code] = cb;

            auto* delBtn = new QPushButton("删除");
            delBtn->setFixedWidth(50);
            connect(delBtn, &QPushButton::clicked, this, [this, code]() {
                deleteLang(code, code);
            });
            row->addWidget(delBtn);
            row->addStretch();
            lay->addLayout(row);
        }
    }

    // Available
    if (!available.isEmpty()) {
        lay->addSpacing(8);
        lay->addWidget(new QLabel("可下载："));
        QStringList availKeys = available.keys();
        availKeys.sort();
        for (const auto& code : availKeys) {
            auto* row = new QHBoxLayout();
            row->addWidget(new QLabel("  " + available[code] + " (" + code + ")"));
            auto* dlBtn = new QPushButton("下载");
            dlBtn->setFixedWidth(50);
            QString url = QString("https://cdn.jsdelivr.net/gh/tesseract-ocr/tessdata_fast@main/%1.traineddata").arg(code);
            connect(dlBtn, &QPushButton::clicked, this, [this, code, name = available[code], url]() {
                startDownload(code, name, url);
            });
            row->addWidget(dlBtn);
            row->addStretch();
            lay->addLayout(row);
        }
    }

    lay->addStretch();
}

// ============================================================================
// Download / Delete
// ============================================================================

void OcrLangManager::startDownload(const QString& code, const QString& name, const QString& url)
{
    // Confirm dialog — matching Python _confirm_download
    if (m_notifyManager) {
        if (!m_notifyManager->confirm("确认下载",
            QString("即将下载语言包「%1」(%2)\n\n"
                    "下载地址：%3\n\n"
                    "⚠ 安全提示：\n"
                    "· 该文件来自第三方 CDN (jsdelivr)，非 Tesseract 官方直接分发\n"
                    "· 源仓库为 tesseract-ocr/tessdata_fast (GitHub 开源项目)\n"
                    "· 下载后文件将保存到：\n  %4/%2.traineddata\n\n"
                    "是否继续？")
                .arg(name).arg(code).arg(url).arg(m_tessdataDir),
            m_settingsDialogParent))
            return;
    }

    // Matching Python _start_download: insert download row dynamically
    // BEFORE starting the network request, so downloadProgress has a bar to update

    // Create progress bar row (matching Python)
    auto* lay = qobject_cast<QVBoxLayout*>(layout());

    // Find insertion point: before "可下载：" label, or before the last stretch
    int insertIdx = lay->count() - 1; // default: before final stretch
    for (int i = 0; i < lay->count(); ++i) {
        auto* item = lay->itemAt(i);
        if (item->widget()) {
            auto* label = qobject_cast<QLabel*>(item->widget());
            if (label && label->text() == "可下载：") {
                insertIdx = i;
                break;
            }
        }
    }

    auto* dlRow = new QHBoxLayout();
    auto* cb = new QCheckBox(name + " (" + code + ")");
    cb->setEnabled(false);
    dlRow->addWidget(cb);
    m_checks[code] = cb;

    auto* bar = new QProgressBar();
    bar->setRange(0, 100);
    bar->setValue(0);
    bar->setFixedWidth(120);
    bar->setFixedHeight(16);
    bar->setFormat("%p%");
    dlRow->addWidget(bar);
    dlRow->addStretch();
    lay->insertLayout(insertIdx, dlRow);

    // Initiate network request — matching Python
    QUrl qurl(url);
    QNetworkRequest req(qurl);
    req.setRawHeader("User-Agent", "Mozilla/5.0");

    auto* reply = m_nam->get(req);
    m_downloads[code] = {reply, name};

    // Connect progress BEFORE storing (matching Python ordering)
    connect(reply, &QNetworkReply::downloadProgress, bar,
            [bar](qint64 received, qint64 total) {
        if (total > 0) {
            bar->setRange(0, static_cast<int>(total));
            bar->setValue(static_cast<int>(received));
        }
    });

    // Connect finished handler
    connect(reply, &QNetworkReply::finished, this, [this, code, name, reply]() {
        bool ok = (reply->error() == QNetworkReply::NoError);
        QString errStr = reply->errorString();
        QByteArray data = ok ? reply->readAll() : QByteArray();

        m_downloads.remove(code);

        if (ok) {
            QString filePath = QDir(m_tessdataDir).filePath(code + ".traineddata");
            QFile f(filePath);
            if (f.open(QIODevice::WriteOnly)) {
                f.write(data); f.close();
                m_installed.insert(code);
                m_selected.insert(code);
                rebuildUi();
                if (m_notifyManager)
                    m_notifyManager->notify("lang_download_success",
                        "语言包下载", QString("「%1」下载完成，已自动启用。").arg(name));
            } else {
                rebuildUi();
                if (m_notifyManager)
                    m_notifyManager->notify("lang_download_fail",
                        "下载失败", QString("「%1」写入文件失败：%2").arg(name, f.errorString()));
            }
        } else {
            rebuildUi();
            if (m_notifyManager)
                m_notifyManager->notify("lang_download_fail",
                    "下载失败", QString("「%1」下载失败：%2").arg(name, errStr));
        }

        reply->deleteLater();
    });
}

void OcrLangManager::deleteLang(const QString& code, const QString& name)
{
    // Confirm dialog — matching Python _delete_lang
    if (!m_notifyManager || !m_notifyManager->confirm("确认删除",
            QString("确定要删除语言包「%1」(%2) 吗？\n\n"
                    "文件将被永久删除：\n%3/%2.traineddata")
                .arg(name).arg(code).arg(m_tessdataDir),
            m_settingsDialogParent))
        return;

    QString filePath = QDir(m_tessdataDir).filePath(code + ".traineddata");
    if (QFile::remove(filePath)) {
        m_installed.remove(code);
        m_selected.remove(code);
        rebuildUi();
        if (m_notifyManager)
            m_notifyManager->info("已删除", QString("语言包「%1」已删除。").arg(name),
                                  m_settingsDialogParent);
    } else {
        if (m_notifyManager)
            m_notifyManager->warn("删除失败", QString("无法删除文件：%1").arg(filePath),
                                  m_settingsDialogParent);
    }
}

QString OcrLangManager::getSelectedLangs() const {
    if (m_tessdataDir.isEmpty()) return "eng";
    QStringList sel;
    for (auto it = m_checks.begin(); it != m_checks.end(); ++it)
        if (it.value()->isEnabled() && it.value()->isChecked())
            sel.append(it.key());
    return sel.isEmpty() ? "eng" : sel.join("+");
}

// ============================================================================
// SettingsDialog
// ============================================================================

SettingsDialog::SettingsDialog(Settings* settings, NotifyManager* notifyManager, QWidget* parent)
    : QDialog(parent)
    , m_settings(settings)
    , m_notifyManager(notifyManager)
{
    // Init provider key cache
    if (!settings->ai_api_key.isEmpty())
        m_providerKeys[settings->ai_provider] = settings->ai_api_key;
    m_currentProvider = settings->ai_provider;

    setWindowTitle("SnapLens 设置");
    setMinimumWidth(480);
    setMinimumHeight(500);

    auto* layout = new QVBoxLayout(this);
    auto* tabs = new QTabWidget();

    tabs->addTab(wrapScroll(buildScreenshotTab()),  "截 图");
    tabs->addTab(wrapScroll(buildMagnifierTab()),   "放大镜");
    tabs->addTab(wrapScroll(buildLabelsTab()),      "坐标与颜色");
    tabs->addTab(wrapScroll(buildStorageTab()),     "存 储");
    tabs->addTab(wrapScroll(buildOcrTab()),         "OCR 识别");
    tabs->addTab(wrapScroll(buildAiSettingsTab()),  "AI 设置");
    tabs->addTab(wrapScroll(buildTranslateTab()),   "翻 译");
    tabs->addTab(wrapScroll(buildGeneralTab()),     "常 规");

    layout->addWidget(tabs);

    // Bottom buttons
    auto* buttons = new QDialogButtonBox();
    buttons->addButton("保存", QDialogButtonBox::AcceptRole);
    buttons->addButton("取消", QDialogButtonBox::RejectRole);
    auto* resetBtn = buttons->addButton("恢复全部默认", QDialogButtonBox::ResetRole);
    connect(buttons, &QDialogButtonBox::accepted, this, &SettingsDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(resetBtn, &QPushButton::clicked, this, &SettingsDialog::onResetAll);
    layout->addWidget(buttons);

    buildResetTable();  // 表驱动重置注册表（须在全部 build 方法之后）
}

SettingsDialog::~SettingsDialog() = default;  // unique_ptr<ApiClient> 在此处析构（完整类型）

// ================================================================ Screenshot

QWidget* SettingsDialog::buildScreenshotTab() {
    auto* w = new QWidget();
    auto* f = new QFormLayout(w);

    m_hotkeyEdit = new QKeySequenceEdit(QKeySequence(m_settings->hotkey));
    f->addRow("截图快捷键", m_hotkeyEdit);

    auto [tw, _s_toolbar] = makeSlider(m_settings->toolbar_opacity, 1, 100);
    m_toolbarOpacity = _s_toolbar;
    f->addRow("工具条不透明度", tw);

    addSep(f);

    m_crosshairColor = new ColorPickerButton(m_settings->crosshair_color);
    f->addRow("全屏十字线颜色", m_crosshairColor);

    auto [ca, _s_ca] = makeSlider(m_settings->crosshair_alpha, 10, 100);
    m_crosshairAlpha = _s_ca;
    f->addRow("全屏十字线不透明度", ca);

    auto [ct, _s_ct] = makeSlider(m_settings->crosshair_thickness, 1, 10, " px");
    m_crosshairThickness = _s_ct;
    f->addRow("全屏十字线粗细", ct);

    m_crosshairInvert = new QCheckBox("反色十字线");
    m_crosshairInvert->setChecked(m_settings->crosshair_invert);
    connect(m_crosshairInvert, &QCheckBox::toggled, this, &SettingsDialog::toggleCrosshairWidgets);
    f->addRow("", m_crosshairInvert);

    m_crosshairEnabled = new QCheckBox("显示全屏十字线");
    m_crosshairEnabled->setChecked(m_settings->crosshair_enabled);
    f->addRow("", m_crosshairEnabled);

    m_crosshairColorWidgets = {m_crosshairColor, ca};
    toggleCrosshairWidgets(m_settings->crosshair_invert);

    addSep(f);

    m_cursorEnabled = new QCheckBox("启用系统十字光标");
    m_cursorEnabled->setChecked(m_settings->cursor_enabled);
    f->addRow("", m_cursorEnabled);

    addSep(f);
    auto* resetBtn = new QPushButton("恢复本页默认");
    connect(resetBtn, &QPushButton::clicked, this, [this]{ resetTab("screenshot", "截图页面"); });
    auto* rr = new QHBoxLayout(); rr->addStretch(); rr->addWidget(resetBtn);
    f->addRow("", rr);

    return w;
}

// ================================================================ Magnifier

QWidget* SettingsDialog::buildMagnifierTab() {
    auto* w = new QWidget();
    auto* f = new QFormLayout(w);

    m_magnifierEnabled = new QCheckBox("启用像素放大镜");
    m_magnifierEnabled->setChecked(m_settings->magnifier_enabled);
    connect(m_magnifierEnabled, &QCheckBox::toggled, this, &SettingsDialog::toggleMagnifierWidgets);
    f->addRow("", m_magnifierEnabled);

    // Zoom — fixed (wheel_zoom OFF) and default (wheel_zoom ON) are mutually exclusive
    auto* zoomFixedW = new QWidget();
    auto* zfr = new QHBoxLayout(zoomFixedW); zfr->setContentsMargins(0,0,0,0);
    m_magnifierFixedZoom = new QSlider(Qt::Horizontal);
    m_magnifierFixedZoom->setRange(40, 200);
    m_magnifierFixedZoom->setValue(static_cast<int>(m_settings->magnifier_fixed_zoom * 10));
    auto* zfl = new QLabel(QString::number(m_magnifierFixedZoom->value() / 10.0, 'f', 1) + "×");
    zfl->setFixedWidth(55);
    connect(m_magnifierFixedZoom, &QSlider::valueChanged, zfl, [zfl](int v) { zfl->setText(QString::number(v / 10.0, 'f', 1) + "×"); });
    zfr->addWidget(m_magnifierFixedZoom); zfr->addWidget(zfl);
    f->addRow("固定放大镜倍率", zoomFixedW);

    auto* zoomDefaultW = new QWidget();
    auto* zdr = new QHBoxLayout(zoomDefaultW); zdr->setContentsMargins(0,0,0,0);
    m_magnifierZoom = new QSlider(Qt::Horizontal);
    m_magnifierZoom->setRange(40, 200);
    m_magnifierZoom->setValue(static_cast<int>(m_settings->magnifier_zoom * 10));
    auto* zdl = new QLabel(QString::number(m_magnifierZoom->value() / 10.0, 'f', 1) + "×");
    zdl->setFixedWidth(55);
    connect(m_magnifierZoom, &QSlider::valueChanged, zdl, [zdl](int v) { zdl->setText(QString::number(v / 10.0, 'f', 1) + "×"); });
    zdr->addWidget(m_magnifierZoom); zdr->addWidget(zdl);
    f->addRow("默认放大倍率", zoomDefaultW);

    m_magnifierWheelZoom = new QCheckBox("允许鼠标滚轮实时调整放大倍率");
    m_magnifierWheelZoom->setChecked(m_settings->magnifier_wheel_zoom);
    connect(m_magnifierWheelZoom, &QCheckBox::toggled, this, &SettingsDialog::toggleWheelZoomWidgets);
    f->addRow("", m_magnifierWheelZoom);

    auto [sw, _s_sz] = makeSlider(m_settings->magnifier_size, 5, 30, " px");
    m_magnifierSize = _s_sz;
    f->addRow("源区域半径", sw);

    addSep(f);

    m_magCrossColor = new ColorPickerButton(m_settings->magnifier_cross_color);
    f->addRow("十字线颜色", m_magCrossColor);

    auto [mca, _s_mca] = makeSlider(m_settings->magnifier_cross_alpha, 30, 100);
    m_magCrossAlpha = _s_mca;
    f->addRow("十字线不透明度", mca);

    auto [mct, _s_mct] = makeSlider(m_settings->magnifier_cross_thickness, 1, 10, " px");
    m_magCrossThickness = _s_mct;
    f->addRow("十字线粗细", mct);

    m_magCrossInvert = new QCheckBox("反色十字线");
    m_magCrossInvert->setChecked(m_settings->magnifier_cross_invert);
    connect(m_magCrossInvert, &QCheckBox::toggled, this, &SettingsDialog::toggleMagCrossWidgets);
    f->addRow("", m_magCrossInvert);

    m_magCrossColorWidgets = {m_magCrossColor, mca};
    toggleMagCrossWidgets(m_settings->magnifier_cross_invert);

    addSep(f);

    m_gridEnabled = new QCheckBox("绘制像素网格线");
    m_gridEnabled->setChecked(m_settings->grid_enabled);
    connect(m_gridEnabled, &QCheckBox::toggled, this, &SettingsDialog::toggleGridWidgets);
    f->addRow("", m_gridEnabled);

    m_gridColor = new ColorPickerButton(m_settings->grid_color);
    f->addRow("网格线颜色", m_gridColor);

    auto [ga, _s_ga] = makeSlider(m_settings->grid_alpha, 1, 100);
    m_gridAlpha = _s_ga;
    f->addRow("网格线不透明度", ga);

    m_gridSubWidgets = {m_gridColor, ga};
    toggleGridWidgets(m_settings->grid_enabled);

    addSep(f);

    m_edgeModeCombo = new QComboBox();
    m_edgeModeCombo->addItems({"裁剪到屏幕边缘", "保持固定大小（填充边缘）"});
    m_edgeModeCombo->setCurrentIndex(m_settings->edge_mode == "crop" ? 0 : 1);
    connect(m_edgeModeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int idx) { toggleEdgeColorWidgets(idx == 1); });
    f->addRow("屏幕边缘策略", m_edgeModeCombo);

    m_edgeColor = new ColorPickerButton(m_settings->edge_color);
    auto* ecl = new QLabel("填充颜色");
    f->addRow(ecl, m_edgeColor);
    m_edgeColorWidgets = {ecl, m_edgeColor};
    toggleEdgeColorWidgets(m_settings->edge_mode == "pad");

    addSep(f);

    m_zoomLabelEnabled = new QCheckBox("显示放大倍率标签");
    m_zoomLabelEnabled->setChecked(m_settings->zoom_label_enabled);
    connect(m_zoomLabelEnabled, &QCheckBox::toggled, this, &SettingsDialog::toggleZoomLabelWidgets);
    f->addRow("", m_zoomLabelEnabled);

    m_zoomLabelTextColor = new ColorPickerButton(m_settings->zoom_label_text_color);
    f->addRow("文本颜色", m_zoomLabelTextColor);

    auto [zta, _s_zta] = makeSlider(m_settings->zoom_label_text_alpha, 10, 100);
    m_zoomLabelTextAlpha = _s_zta;
    f->addRow("文本不透明度", zta);

    m_zoomLabelBgColor = new ColorPickerButton(m_settings->zoom_label_bg_color);
    f->addRow("背景颜色", m_zoomLabelBgColor);

    auto [zba, _s_zba] = makeSlider(m_settings->zoom_label_bg_alpha, 10, 100);
    m_zoomLabelBgAlpha = _s_zba;
    f->addRow("背景不透明度", zba);

    m_zoomLabelSubWidgets = {m_zoomLabelTextColor, zta, m_zoomLabelBgColor, zba};
    toggleZoomLabelWidgets(m_settings->zoom_label_enabled);

    // Sub-widget groups
    m_magnifierSubWidgets = {zoomFixedW, zoomDefaultW, sw, m_magCrossColor, mca, m_magnifierWheelZoom};
    m_wheelZoomSubWidgets = {zoomDefaultW};
    m_fixedZoomSubWidgets = {zoomFixedW};
    toggleMagnifierWidgets(m_settings->magnifier_enabled);
    toggleWheelZoomWidgets(m_settings->magnifier_wheel_zoom);

    addSep(f);
    auto* resetBtn = new QPushButton("恢复本页默认");
    connect(resetBtn, &QPushButton::clicked, this, [this]{ resetTab("magnifier", "放大镜页面"); });
    auto* rr = new QHBoxLayout(); rr->addStretch(); rr->addWidget(resetBtn);
    f->addRow("", rr);
    return w;
}

// ================================================================ Labels

QWidget* SettingsDialog::buildLabelsTab() {
    auto* w = new QWidget();
    auto* f = new QFormLayout(w);

    m_coordLabelEnabled = new QCheckBox("显示屏幕坐标标签");
    m_coordLabelEnabled->setChecked(m_settings->coord_label_enabled);
    connect(m_coordLabelEnabled, &QCheckBox::toggled, this, &SettingsDialog::toggleCoordLabelWidgets);
    f->addRow("", m_coordLabelEnabled);

    m_coordTextColor = new ColorPickerButton(m_settings->coord_label_text_color);
    f->addRow("坐标文字颜色", m_coordTextColor);

    auto [cta, _s_cta] = makeSlider(m_settings->coord_label_text_alpha, 10, 100);
    m_coordTextAlpha = _s_cta;
    f->addRow("坐标文字不透明度", cta);

    m_coordBgColor = new ColorPickerButton(m_settings->coord_label_bg_color);
    f->addRow("坐标背景颜色", m_coordBgColor);

    auto [cla, _s_cla] = makeSlider(m_settings->coord_label_bg_alpha, 10, 100);
    m_coordBgAlpha = _s_cla;
    f->addRow("坐标背景不透明度", cla);

    m_coordLabelSubWidgets = {m_coordTextColor, cta, m_coordBgColor, cla};
    toggleCoordLabelWidgets(m_settings->coord_label_enabled);

    addSep(f);

    m_colorLabelEnabled = new QCheckBox("显示像素颜色标签");
    m_colorLabelEnabled->setChecked(m_settings->color_label_enabled);
    connect(m_colorLabelEnabled, &QCheckBox::toggled, this, &SettingsDialog::toggleColorLabelWidgets);
    f->addRow("", m_colorLabelEnabled);

    m_colorTextColor = new ColorPickerButton(m_settings->color_label_text_color);
    f->addRow("颜色标签文字颜色", m_colorTextColor);

    auto [cota, _s_cota] = makeSlider(m_settings->color_label_text_alpha, 10, 100);
    m_colorTextAlpha = _s_cota;
    f->addRow("颜色标签文字不透明度", cota);

    m_colorBgColor = new ColorPickerButton(m_settings->color_label_bg_color);
    f->addRow("颜色标签背景颜色", m_colorBgColor);

    auto [cba, _s_cba] = makeSlider(m_settings->color_label_bg_alpha, 10, 100);
    m_colorBgAlpha = _s_cba;
    f->addRow("颜色标签背景不透明度", cba);

    m_colorFormatCombo = new QComboBox();
    m_colorFormatCombo->addItems({"RGB", "十六进制 (Hex)"});
    m_colorFormatCombo->setCurrentIndex(m_settings->color_format == "rgb" ? 0 : 1);
    f->addRow("颜色显示格式", m_colorFormatCombo);

    addSep(f);

    m_copyColorKeyEdit = new QKeySequenceEdit(QKeySequence(m_settings->copy_color_key));
    f->addRow("复制颜色快捷键", m_copyColorKeyEdit);

    m_copyHexPrefix = new QCheckBox("Hex 复制时带 # 号");
    m_copyHexPrefix->setChecked(m_settings->copy_hex_prefix);
    f->addRow("", m_copyHexPrefix);
    m_copyRgbPrefix = new QCheckBox("RGB 复制时带 rgb() 包裹");
    m_copyRgbPrefix->setChecked(m_settings->copy_rgb_prefix);
    f->addRow("", m_copyRgbPrefix);

    m_colorLabelSubWidgets = {m_colorTextColor, cota, m_colorBgColor, cba, m_colorFormatCombo,
                              m_copyColorKeyEdit, m_copyHexPrefix, m_copyRgbPrefix};
    toggleColorLabelWidgets(m_settings->color_label_enabled);

    addSep(f);
    auto* resetBtn = new QPushButton("恢复本页默认");
    connect(resetBtn, &QPushButton::clicked, this, [this]{ resetTab("labels", "坐标与颜色页面"); });
    auto* rr = new QHBoxLayout(); rr->addStretch(); rr->addWidget(resetBtn);
    f->addRow("", rr);
    return w;
}

// ================================================================ Storage

QWidget* SettingsDialog::buildStorageTab() {
    auto* w = new QWidget();
    auto* f = new QFormLayout(w);

    auto* dirW = new QWidget();
    auto* dr = new QHBoxLayout(dirW); dr->setContentsMargins(0,0,0,0);
    m_dirEdit = new QLineEdit(m_settings->save_dir);
    auto* browseBtn = new QPushButton("浏览…");
    connect(browseBtn, &QPushButton::clicked, this, &SettingsDialog::browseSaveDir);
    dr->addWidget(m_dirEdit); dr->addWidget(browseBtn);
    f->addRow("图片保存位置", dirW);

    m_formatCombo = new QComboBox();
    m_formatCombo->addItems({"png", "jpg", "bmp"});
    int fi = m_formatCombo->findText(m_settings->save_format.toLower());
    if (fi >= 0) m_formatCombo->setCurrentIndex(fi);
    f->addRow("图片保存格式", m_formatCombo);

    auto* cfgW = new QWidget();
    auto* cr = new QHBoxLayout(cfgW); cr->setContentsMargins(0,0,0,0);
    m_configEdit = new QLineEdit(m_settings->config_dir.isEmpty() ? "" : m_settings->config_dir);
    if (m_settings->config_dir.isEmpty()) m_configEdit->setPlaceholderText("（程序所在目录）");
    auto* cfgBrowse = new QPushButton("浏览…");
    connect(cfgBrowse, &QPushButton::clicked, this, &SettingsDialog::browseConfigDir);
    auto* cfgReset = new QPushButton("默认");
    cfgReset->setToolTip("恢复为程序所在目录");
    connect(cfgReset, &QPushButton::clicked, this, &SettingsDialog::resetConfigDir);
    cr->addWidget(m_configEdit); cr->addWidget(cfgBrowse); cr->addWidget(cfgReset);
    f->addRow("配置保存位置", cfgW);

    addSep(f);
    auto* resetBtn = new QPushButton("恢复本页默认");
    connect(resetBtn, &QPushButton::clicked, this, [this]{ resetTab("storage", "存储页面"); });
    auto* rr = new QHBoxLayout(); rr->addStretch(); rr->addWidget(resetBtn);
    f->addRow("", rr);
    return w;
}

// ================================================================ OCR

QWidget* SettingsDialog::buildOcrTab() {
    auto* w = new QWidget();
    auto* f = new QFormLayout(w);

    m_ocrLangList = new OcrLangManager(m_settings->ai_ocr_langs,
                                       m_settings->ocr_tessdata_dir,
                                       m_notifyManager, this);
    f->addRow("语言包管理", m_ocrLangList);

    addSep(f);

    m_ocrTessdataEdit = new QLineEdit(m_settings->ocr_tessdata_dir);
    m_ocrTessdataEdit->setPlaceholderText("留空则自动检测");
    auto* tr = new QHBoxLayout();
    tr->addWidget(m_ocrTessdataEdit);
    auto* tb = new QPushButton("浏览...");
    connect(tb, &QPushButton::clicked, this, [this]{ browseDir(m_ocrTessdataEdit, "选择语言包目录"); });
    tr->addWidget(tb);
    f->addRow("语言包目录", tr);

    addSep(f);
    auto* resetBtn = new QPushButton("恢复本页默认");
    connect(resetBtn, &QPushButton::clicked, this, [this]{ resetTab("ocr", "OCR 识别页面"); });
    auto* rr = new QHBoxLayout(); rr->addStretch(); rr->addWidget(resetBtn);
    f->addRow("", rr);
    return w;
}

// ================================================================ AI Settings

QWidget* SettingsDialog::buildAiSettingsTab() {
    auto* w = new QWidget();
    auto* f = new QFormLayout(w);

    f->addRow(new QLabel("AI 连接与参数"));

    m_aiProviderCombo = new QComboBox();
    for (const auto& pid : PROVIDER_CONFIGS.keys())
        m_aiProviderCombo->addItem(PROVIDER_CONFIGS[pid].label, pid);
    int pidx = m_aiProviderCombo->findData(m_settings->ai_provider);
    if (pidx >= 0) m_aiProviderCombo->setCurrentIndex(pidx);
    connect(m_aiProviderCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &SettingsDialog::onProviderChanged);
    f->addRow("服务商", m_aiProviderCombo);

    m_aiApiKeyEdit = new QLineEdit(m_settings->ai_api_key);
    m_aiApiKeyEdit->setPlaceholderText("sk-...");
    f->addRow("API Key", m_aiApiKeyEdit);

    m_aiApiBaseEdit = new QLineEdit(m_settings->ai_api_base);
    m_aiApiBaseEdit->setPlaceholderText("https://api.deepseek.com/v1");
    f->addRow("API 地址", m_aiApiBaseEdit);

    auto* mr = new QHBoxLayout();
    m_aiModelCombo = new QComboBox();
    m_aiModelCombo->setEditable(true);
    m_aiModelCombo->setMinimumWidth(200);
    m_aiModelCombo->setCurrentText(m_settings->ai_model);
    m_aiModelCombo->setPlaceholderText("输入模型名称或点击右侧按钮自动获取");
    mr->addWidget(m_aiModelCombo);
    m_fetchModelsBtn = new QPushButton("获取模型列表");
    m_fetchModelsBtn->setFixedWidth(100);
    m_fetchModelsBtn->setFocusPolicy(Qt::NoFocus);
    connect(m_fetchModelsBtn, &QPushButton::clicked, this, &SettingsDialog::onFetchModels);
    mr->addWidget(m_fetchModelsBtn);
    f->addRow("模型", mr);

    m_aiErrorLabel = new QLabel();
    m_aiErrorLabel->setWordWrap(true);
    m_aiErrorLabel->setVisible(false);
    f->addRow("", m_aiErrorLabel);

    auto [tw2, _s_to] = makeSlider(m_settings->ai_timeout, 5, 120, "秒");
    m_aiTimeout = _s_to;
    f->addRow("请求超时", tw2);

    // Temperature
    auto [tp, _s_tp] = makeDecimalSlider(m_settings->ai_temperature, 0.0, 2.0);
    m_aiTemperature = _s_tp;
    auto* tpr = new QVBoxLayout(); tpr->addWidget(tp);
    tpr->addWidget(new QLabel("控制输出随机性。接近 0 翻译更稳定一致，接近 2 更富变化。翻译建议 ≤0.3"));
    f->addRow("温度 (Temperature)", tpr);

    // Max tokens
    auto [mt, _s_mt] = makeSlider(m_settings->ai_max_tokens, 256, 8192, "");
    m_aiMaxTokens = _s_mt;
    auto* mtr = new QVBoxLayout(); mtr->addWidget(mt);
    mtr->addWidget(new QLabel("单次翻译最大输出长度。1 个中文字 ≈ 2 token。超出将被截断"));
    f->addRow("最大输出 Token", mtr);

    // Top P
    auto [tpp, _s_tpp] = makeDecimalSlider(m_settings->ai_top_p, 0.0, 1.0);
    m_aiTopP = _s_tpp;
    auto* tppr = new QVBoxLayout(); tppr->addWidget(tpp);
    tppr->addWidget(new QLabel("核采样阈值。1.0 考虑所有可能词汇，越低输出越聚焦。翻译建议 1.0"));
    f->addRow("Top P（核采样）", tppr);

    // Frequency penalty
    auto [fp, _s_fp] = makeDecimalSlider(m_settings->ai_frequency_penalty, -2.0, 2.0);
    m_aiFrequencyPenalty = _s_fp;
    auto* fpr = new QVBoxLayout(); fpr->addWidget(fp);
    fpr->addWidget(new QLabel("降低模型重复用词。正值惩罚已出现的词，翻译建议 0.0（不惩罚）"));
    f->addRow("频率惩罚 (Frequency Penalty)", fpr);

    // Presence penalty
    auto [pp, _s_pp] = makeDecimalSlider(m_settings->ai_presence_penalty, -2.0, 2.0);
    m_aiPresencePenalty = _s_pp;
    auto* ppr = new QVBoxLayout(); ppr->addWidget(pp);
    ppr->addWidget(new QLabel("鼓励引入新话题。正值惩罚已讨论过的内容，翻译建议 0.0（不惩罚）"));
    f->addRow("存在惩罚 (Presence Penalty)", ppr);

    // Seed
    m_aiSeedEdit = new QLineEdit(QString::number(m_settings->ai_seed));
    m_aiSeedEdit->setPlaceholderText("0（默认随机）");
    m_aiSeedEdit->setMaximumWidth(120);
    auto* sdr = new QHBoxLayout(); sdr->addWidget(m_aiSeedEdit); sdr->addStretch();
    auto* sdh = new QVBoxLayout(); sdh->addLayout(sdr);
    sdh->addWidget(new QLabel("相同种子 + 相同参数 = 相同结果。设为 0 表示每次随机。调试/对比翻译时有用"));
    f->addRow("随机种子 (Seed)", sdh);

    addSep(f);
    auto* resetBtn = new QPushButton("恢复本页默认");
    connect(resetBtn, &QPushButton::clicked, this, [this]{ resetTab("ai_settings", "AI 设置页面"); });
    auto* rr = new QHBoxLayout(); rr->addStretch(); rr->addWidget(resetBtn);
    f->addRow("", rr);
    return w;
}

// ================================================================ Translate

QWidget* SettingsDialog::buildTranslateTab() {
    auto* w = new QWidget();
    auto* f = new QFormLayout(w);

    m_aiTargetLangCombo = new QComboBox();
    m_aiTargetLangCombo->setEditable(true);
    m_aiTargetLangCombo->addItems({
        "简体中文", "繁体中文（台湾）", "繁体中文（香港）",
        "英语", "日语", "韩语", "法语", "德语",
        "西班牙语", "葡萄牙语", "俄语", "阿拉伯语",
        "泰语", "越南语",
    });
    int tli = m_aiTargetLangCombo->findText(m_settings->ai_target_lang);
    if (tli >= 0) m_aiTargetLangCombo->setCurrentIndex(tli);
    else m_aiTargetLangCombo->setCurrentText(m_settings->ai_target_lang);
    f->addRow("默认目标语言", m_aiTargetLangCombo);

    m_aiConfirmBefore = new QCheckBox("翻译前确认目标语言（弹出窗口后不自动翻译，需手动点击）");
    m_aiConfirmBefore->setChecked(m_settings->ai_confirm_before_translate);
    f->addRow("", m_aiConfirmBefore);

    m_aiStreamThinking = new QCheckBox("流式推送 AI 思考过程（实时显示，推荐开启）");
    m_aiStreamThinking->setChecked(m_settings->ai_stream_thinking);
    f->addRow("", m_aiStreamThinking);

    addSep(f);
    f->addRow(new QLabel("提示词设置"));

    m_aiPromptEdit = new QTextEdit();
    m_aiPromptEdit->setPlainText(m_settings->ai_translation_prompt);
    m_aiPromptEdit->setPlaceholderText(QString::fromUtf8(DEFAULT_TRANSLATION_PROMPT));
    m_aiPromptEdit->setFixedHeight(120);
    auto* spr = new QVBoxLayout(); spr->addWidget(m_aiPromptEdit);
    spr->addWidget(new QLabel("占位符 {target_lang} 会在翻译时替换为目标语言"));
    f->addRow("截图翻译提示词", spr);

    m_textPromptEdit = new QTextEdit();
    m_textPromptEdit->setPlainText(m_settings->text_translation_prompt);
    m_textPromptEdit->setPlaceholderText(QString::fromUtf8(DEFAULT_TEXT_TRANSLATION_PROMPT));
    m_textPromptEdit->setFixedHeight(120);
    auto* tpr2 = new QVBoxLayout(); tpr2->addWidget(m_textPromptEdit);
    tpr2->addWidget(new QLabel("占位符 {source_text} 替换为原文，{target_lang} 替换为目标语言，{scenario} 替换为翻译场景"));
    f->addRow("文本翻译提示词", tpr2);

    addSep(f);
    auto* resetBtn = new QPushButton("恢复本页默认");
    connect(resetBtn, &QPushButton::clicked, this, [this]{ resetTab("translate", "翻译页面"); });
    auto* rr = new QHBoxLayout(); rr->addStretch(); rr->addWidget(resetBtn);
    f->addRow("", rr);
    return w;
}

// ================================================================ General

QWidget* SettingsDialog::buildGeneralTab() {
    auto* w = new QWidget();
    auto* f = new QFormLayout(w);

    m_appModeCombo = new QComboBox();
    m_appModeCombo->addItem("翻译模式 — 启动时显示文本翻译窗口", "translate");
    m_appModeCombo->addItem("截图模式 — 启动时静默在后台", "screenshot");
    m_appModeCombo->setCurrentIndex(m_settings->app_mode == "translate" ? 0 : 1);
    f->addRow("默认启动模式", m_appModeCombo);

    addSep(f);

    m_closeToTrayCombo = new QComboBox();
    m_closeToTrayCombo->addItems({"每次询问", "后台运行（最小化到托盘）", "退出程序"});
    if (m_settings->close_to_tray.toBool() == true)
        m_closeToTrayCombo->setCurrentIndex(1);
    else if (m_settings->close_to_tray.toBool() == false && m_settings->close_to_tray.isValid() && !m_settings->close_to_tray.isNull())
        m_closeToTrayCombo->setCurrentIndex(2);
    else
        m_closeToTrayCombo->setCurrentIndex(0);
    f->addRow("关闭主窗口时", m_closeToTrayCombo);

    addSep(f);

    m_notifyCopy = new QCheckBox("复制到剪贴板时显示通知");
    m_notifyCopy->setChecked(m_settings->notify_copy);
    f->addRow("", m_notifyCopy);
    m_notifySaveSuccess = new QCheckBox("保存到文件成功时显示通知");
    m_notifySaveSuccess->setChecked(m_settings->notify_save_success);
    f->addRow("", m_notifySaveSuccess);
    m_notifySaveFail = new QCheckBox("保存到文件失败时显示通知");
    m_notifySaveFail->setChecked(m_settings->notify_save_fail);
    f->addRow("", m_notifySaveFail);
    m_notifyCaptureFail = new QCheckBox("屏幕抓取失败时显示通知");
    m_notifyCaptureFail->setChecked(m_settings->notify_capture_fail);
    f->addRow("", m_notifyCaptureFail);
    m_notifyHotkeyFail = new QCheckBox("快捷键注册失败时显示通知");
    m_notifyHotkeyFail->setChecked(m_settings->notify_hotkey_fail);
    f->addRow("", m_notifyHotkeyFail);
    m_notifyTransSuccess = new QCheckBox("AI 翻译成功时显示通知");
    m_notifyTransSuccess->setChecked(m_settings->notify_translate_success);
    f->addRow("", m_notifyTransSuccess);
    m_notifyTransFail = new QCheckBox("AI 翻译失败时显示通知");
    m_notifyTransFail->setChecked(m_settings->notify_translate_fail);
    f->addRow("", m_notifyTransFail);
    m_notifyOcrFail = new QCheckBox("OCR 识别失败时显示通知");
    m_notifyOcrFail->setChecked(m_settings->notify_ocr_fail);
    f->addRow("", m_notifyOcrFail);
    m_notifyLangDownload = new QCheckBox("语言包下载成功/失败时显示通知");
    m_notifyLangDownload->setChecked(m_settings->notify_lang_download);
    f->addRow("", m_notifyLangDownload);

    addSep(f);

    f->addRow(new QLabel("日志级别（勾选即启用，重启后生效）："));
    m_logDebugEnabled = new QCheckBox("DEBUG — 调试信息");
    m_logDebugEnabled->setChecked(m_settings->log_debug_enabled);
    f->addRow("", m_logDebugEnabled);
    m_logInfoEnabled = new QCheckBox("INFO — 一般信息");
    m_logInfoEnabled->setChecked(m_settings->log_info_enabled);
    f->addRow("", m_logInfoEnabled);
    m_logWarningEnabled = new QCheckBox("WARNING — 警告信息");
    m_logWarningEnabled->setChecked(m_settings->log_warning_enabled);
    f->addRow("", m_logWarningEnabled);
    m_logErrorEnabled = new QCheckBox("ERROR — 错误信息");
    m_logErrorEnabled->setChecked(m_settings->log_error_enabled);
    f->addRow("", m_logErrorEnabled);

    addSep(f);
    auto* resetBtn = new QPushButton("恢复本页默认");
    connect(resetBtn, &QPushButton::clicked, this, [this]{ resetTab("general", "常规页面"); });
    auto* rr = new QHBoxLayout(); rr->addStretch(); rr->addWidget(resetBtn);
    f->addRow("", rr);
    return w;
}

// ================================================================ Toggle helpers

void SettingsDialog::toggleMagnifierWidgets(bool on)     {
    for (auto* w : m_magnifierSubWidgets) w->setEnabled(on);
    // Re-apply zoom slider mutual exclusion after enabling all
    if (on) toggleWheelZoomWidgets(m_magnifierWheelZoom->isChecked());
}
void SettingsDialog::toggleWheelZoomWidgets(bool on)     {
    for (auto* w : m_wheelZoomSubWidgets) w->setEnabled(on);
    for (auto* w : m_fixedZoomSubWidgets) w->setEnabled(!on);
}
void SettingsDialog::toggleGridWidgets(bool on)           { for (auto* w : m_gridSubWidgets) w->setEnabled(on); }
void SettingsDialog::toggleEdgeColorWidgets(bool on)      { for (auto* w : m_edgeColorWidgets) w->setVisible(on); }
void SettingsDialog::toggleZoomLabelWidgets(bool on)      { for (auto* w : m_zoomLabelSubWidgets) w->setEnabled(on); }
void SettingsDialog::toggleCoordLabelWidgets(bool on)     { for (auto* w : m_coordLabelSubWidgets) w->setEnabled(on); }
void SettingsDialog::toggleColorLabelWidgets(bool on)     { for (auto* w : m_colorLabelSubWidgets) w->setEnabled(on); }
void SettingsDialog::toggleCrosshairWidgets(bool invert)  { for (auto* w : m_crosshairColorWidgets) w->setEnabled(!invert); }
void SettingsDialog::toggleMagCrossWidgets(bool invert)   { for (auto* w : m_magCrossColorWidgets) w->setEnabled(!invert); }

// ================================================================ Actions

void SettingsDialog::browseSaveDir() {
    QString d = QFileDialog::getExistingDirectory(this, "选择图片保存目录", m_dirEdit->text());
    if (!d.isEmpty()) m_dirEdit->setText(d);
}

void SettingsDialog::browseConfigDir() {
    QString d = QFileDialog::getExistingDirectory(this, "选择配置保存目录", m_configEdit->text());
    if (!d.isEmpty()) m_configEdit->setText(d);
}

void SettingsDialog::resetConfigDir() { m_configEdit->clear(); m_configEdit->setPlaceholderText("（程序所在目录）"); }

void SettingsDialog::browseDir(QLineEdit* edit, const QString& title) {
    QString d = QFileDialog::getExistingDirectory(this, title, edit->text());
    if (!d.isEmpty()) edit->setText(d);
}

void SettingsDialog::onProviderChanged(int) {
    QString newPid = m_aiProviderCombo->currentData().toString();
    auto cfg = getProviderConfig(newPid);
    if (cfg.apiBase.isEmpty()) return;
    if (!m_currentProvider.isEmpty() && m_currentProvider != newPid)
        m_providerKeys[m_currentProvider] = m_aiApiKeyEdit->text().trimmed();
    m_aiApiBaseEdit->setText(cfg.apiBase);
    if (newPid != m_currentProvider)
        m_aiApiKeyEdit->setText(m_providerKeys.value(newPid));
    m_currentProvider = newPid;
}

void SettingsDialog::onFetchModels() {
    QString apiKey = m_aiApiKeyEdit->text().trimmed();
    QString apiBase = m_aiApiBaseEdit->text().trimmed();
    m_aiErrorLabel->setVisible(false);
    if (apiKey.isEmpty()) { m_aiErrorLabel->setText("请先填写 API Key"); m_aiErrorLabel->setVisible(true); return; }

    // L11 异步化：发起请求后立即返回，UI 不再阻塞（原同步嵌套事件循环最长 10s，
    // 期间点保存/重置/关闭会重入产生怪癖）。结果通过回调在主线程回填；
    // 回调先校验代次与可见性，丢弃过期/关闭后的结果（详见回调内注释）。
    m_fetchModelsBtn->setEnabled(false);
    m_fetchModelsBtn->setText("获取中…");
    m_aiModelCombo->clear();
    const int gen = ++m_fetchGen;

    if (!m_aiClient) m_aiClient = std::make_unique<ApiClient>();
    m_aiClient->listModelsAsync(apiKey, apiBase, 10,
        [this, gen](const ApiClient::ModelsResult& result) {
            // 无论结果是否被采用，先恢复按钮，保证 UI 状态一致
            m_fetchModelsBtn->setEnabled(true);
            m_fetchModelsBtn->setText("获取模型列表");

            // 过期结果丢弃：期间用户已恢复本页默认/恢复全部默认/再次发起获取
            if (gen != m_fetchGen) {
                SNAP_LOG_DEBUG("SettingsDialog: 丢弃过期的模型列表结果（代次不匹配）");
                return;
            }
            // 对话框已关闭（取消/关窗）：不再回填隐藏控件
            if (!isVisible()) {
                SNAP_LOG_DEBUG("SettingsDialog: 对话框已关闭，丢弃模型列表结果");
                return;
            }

            if (result.errorCode == 0 && !result.modelIds.isEmpty()) {
                m_aiModelCombo->addItems(result.modelIds);
            } else if (result.errorCode == -2) {
                m_aiModelCombo->addItem("（请求超时）");
                m_aiErrorLabel->setText("请求超时，请稍后重试"); m_aiErrorLabel->setVisible(true);
            } else {
                m_aiModelCombo->addItem("（获取失败）");
                QString hint = result.errorMsg.contains("鉴权") ? "API Key 鉴权失败" : "获取模型列表失败";
                m_aiErrorLabel->setText(hint + "\n" + result.errorMsg); m_aiErrorLabel->setVisible(true);
            }
        });
}

void SettingsDialog::accept() {
    if (m_hotkeyEdit->keySequence().isEmpty()) {
        if (m_notifyManager) m_notifyManager->warn("设置", "请先录入截图快捷键。", this);
        return;
    }
    if (m_dirEdit->text().trimmed().isEmpty()) {
        if (m_notifyManager) m_notifyManager->warn("设置", "保存目录不能为空。", this);
        return;
    }

    // Write all settings
    m_settings->hotkey = m_hotkeyEdit->keySequence().toString(QKeySequence::PortableText);
    m_settings->toolbar_opacity = m_toolbarOpacity->value();
    m_settings->crosshair_color = m_crosshairColor->color();
    m_settings->crosshair_alpha = m_crosshairAlpha->value();
    m_settings->crosshair_thickness = m_crosshairThickness->value();
    m_settings->crosshair_invert = m_crosshairInvert->isChecked();
    m_settings->crosshair_enabled = m_crosshairEnabled->isChecked();
    m_settings->cursor_enabled = m_cursorEnabled->isChecked();

    m_settings->magnifier_enabled = m_magnifierEnabled->isChecked();
    m_settings->magnifier_zoom = m_magnifierZoom->value() / 10.0;
    m_settings->magnifier_fixed_zoom = m_magnifierFixedZoom->value() / 10.0;
    m_settings->magnifier_wheel_zoom = m_magnifierWheelZoom->isChecked();
    m_settings->magnifier_size = m_magnifierSize->value();
    m_settings->magnifier_cross_color = m_magCrossColor->color();
    m_settings->magnifier_cross_alpha = m_magCrossAlpha->value();
    m_settings->magnifier_cross_thickness = m_magCrossThickness->value();
    m_settings->magnifier_cross_invert = m_magCrossInvert->isChecked();
    m_settings->grid_enabled = m_gridEnabled->isChecked();
    m_settings->grid_color = m_gridColor->color();
    m_settings->grid_alpha = m_gridAlpha->value();
    m_settings->edge_mode = m_edgeModeCombo->currentIndex() == 0 ? "crop" : "pad";
    m_settings->edge_color = m_edgeColor->color();
    m_settings->zoom_label_enabled = m_zoomLabelEnabled->isChecked();
    m_settings->zoom_label_text_color = m_zoomLabelTextColor->color();
    m_settings->zoom_label_text_alpha = m_zoomLabelTextAlpha->value();
    m_settings->zoom_label_bg_color = m_zoomLabelBgColor->color();
    m_settings->zoom_label_bg_alpha = m_zoomLabelBgAlpha->value();

    m_settings->coord_label_enabled = m_coordLabelEnabled->isChecked();
    m_settings->coord_label_text_color = m_coordTextColor->color();
    m_settings->coord_label_text_alpha = m_coordTextAlpha->value();
    m_settings->coord_label_bg_color = m_coordBgColor->color();
    m_settings->coord_label_bg_alpha = m_coordBgAlpha->value();
    m_settings->color_label_enabled = m_colorLabelEnabled->isChecked();
    m_settings->color_label_text_color = m_colorTextColor->color();
    m_settings->color_label_text_alpha = m_colorTextAlpha->value();
    m_settings->color_label_bg_color = m_colorBgColor->color();
    m_settings->color_label_bg_alpha = m_colorBgAlpha->value();
    m_settings->color_format = m_colorFormatCombo->currentIndex() == 0 ? "rgb" : "hex";
    m_settings->copy_color_key = m_copyColorKeyEdit->keySequence().toString(QKeySequence::PortableText);
    m_settings->copy_hex_prefix = m_copyHexPrefix->isChecked();
    m_settings->copy_rgb_prefix = m_copyRgbPrefix->isChecked();

    m_settings->save_dir = m_dirEdit->text().trimmed();
    m_settings->save_format = m_formatCombo->currentText();
    m_settings->config_dir = m_configEdit->text().trimmed();

    m_settings->ai_ocr_langs = m_ocrLangList->getSelectedLangs();
    m_settings->ocr_tessdata_dir = m_ocrTessdataEdit->text().trimmed();

    m_settings->ai_provider = m_aiProviderCombo->currentData().toString();
    m_settings->ai_api_key = m_aiApiKeyEdit->text().trimmed();
    m_settings->ai_api_base = m_aiApiBaseEdit->text().trimmed();
    m_settings->ai_model = m_aiModelCombo->currentText().trimmed();
    m_settings->ai_timeout = m_aiTimeout->value();
    m_settings->ai_temperature = m_aiTemperature->value() / 100.0;
    m_settings->ai_max_tokens = m_aiMaxTokens->value();
    m_settings->ai_top_p = m_aiTopP->value() / 100.0;
    m_settings->ai_frequency_penalty = m_aiFrequencyPenalty->value() / 100.0;
    m_settings->ai_presence_penalty = m_aiPresencePenalty->value() / 100.0;
    // 随机种子输入验证：非法输入（非整数/空）提示并回退默认 0（0 = 不指定，随机），
    // 避免 toInt() 无验证静默吞掉非法值后用户无从感知。
    bool seedOk = false;
    const int seedVal = m_aiSeedEdit->text().trimmed().toInt(&seedOk);
    if (!seedOk) {
        m_aiSeedEdit->setText("0");
        if (m_notifyManager) {
            m_notifyManager->warn("设置", "随机种子必须为整数，已重置为 0（默认随机）。", this);
        }
    }
    m_settings->ai_seed = seedOk ? seedVal : 0;

    m_settings->ai_target_lang = m_aiTargetLangCombo->currentText();
    m_settings->ai_confirm_before_translate = m_aiConfirmBefore->isChecked();
    m_settings->ai_stream_thinking = m_aiStreamThinking->isChecked();
    m_settings->ai_translation_prompt = m_aiPromptEdit->toPlainText().trimmed();
    m_settings->text_translation_prompt = m_textPromptEdit->toPlainText().trimmed();

    m_settings->app_mode = m_appModeCombo->currentData().toString();
    int ctIdx = m_closeToTrayCombo->currentIndex();
    m_settings->close_to_tray = (ctIdx == 1) ? QVariant(true) : (ctIdx == 2) ? QVariant(false) : QVariant();

    m_settings->notify_copy = m_notifyCopy->isChecked();
    m_settings->notify_save_success = m_notifySaveSuccess->isChecked();
    m_settings->notify_save_fail = m_notifySaveFail->isChecked();
    m_settings->notify_capture_fail = m_notifyCaptureFail->isChecked();
    m_settings->notify_hotkey_fail = m_notifyHotkeyFail->isChecked();
    m_settings->notify_translate_success = m_notifyTransSuccess->isChecked();
    m_settings->notify_translate_fail = m_notifyTransFail->isChecked();
    m_settings->notify_ocr_fail = m_notifyOcrFail->isChecked();
    m_settings->notify_lang_download = m_notifyLangDownload->isChecked();
    m_settings->log_debug_enabled = m_logDebugEnabled->isChecked();
    m_settings->log_info_enabled = m_logInfoEnabled->isChecked();
    m_settings->log_warning_enabled = m_logWarningEnabled->isChecked();
    m_settings->log_error_enabled = m_logErrorEnabled->isChecked();

    m_settings->save();
    QDialog::accept();
}

// ================================================================ Reset helpers

bool SettingsDialog::confirmReset(const QString& scope) {
    if (m_notifyManager)
        return m_notifyManager->confirm("恢复默认设置",
            QString("确定要将%1设置恢复为默认值吗？\n\n此操作不可撤销。").arg(scope), this);
    return QMessageBox::question(this, "恢复默认", "确定恢复？") == QMessageBox::Yes;
}

void SettingsDialog::onResetAll() {
    if (!confirmReset("全部")) return;
    ++m_fetchGen;  // 使进行中的模型获取失效（防止重置后结果覆盖界面）
    Settings defaults(Settings::defaultsDict());
    m_settings->updateFromDict(defaults.toDict());
    m_settings->save();
    QDialog::accept();
}

// ================================================================ 表驱动重置
// 对齐 Python `_reset_map`：一张表 (tab → 重置动作) 驱动所有"恢复本页默认"。
// 新增设置项只需在 buildResetTable() 加一行，重置自动生效，无需再写 reset 方法。

void SettingsDialog::buildResetTable() {
    m_resetItems = {
        // ---- 截图 ----
        {"screenshot", [this](const Settings& d){ m_hotkeyEdit->setKeySequence(QKeySequence(d.hotkey)); }},
        {"screenshot", [this](const Settings& d){ m_toolbarOpacity->setValue(d.toolbar_opacity); }},
        {"screenshot", [this](const Settings& d){ m_crosshairColor->setColor(d.crosshair_color); }},
        {"screenshot", [this](const Settings& d){ m_crosshairAlpha->setValue(d.crosshair_alpha); }},
        {"screenshot", [this](const Settings& d){ m_crosshairThickness->setValue(d.crosshair_thickness); }},
        {"screenshot", [this](const Settings& d){ m_crosshairInvert->setChecked(d.crosshair_invert); }},
        {"screenshot", [this](const Settings& d){ m_crosshairEnabled->setChecked(d.crosshair_enabled); }},
        {"screenshot", [this](const Settings& d){ m_cursorEnabled->setChecked(d.cursor_enabled); }},

        // ---- 放大镜 ----
        {"magnifier", [this](const Settings& d){ m_magnifierEnabled->setChecked(d.magnifier_enabled); }},
        {"magnifier", [this](const Settings& d){ m_magnifierZoom->setValue(static_cast<int>(d.magnifier_zoom * 10)); }},
        {"magnifier", [this](const Settings& d){ m_magnifierFixedZoom->setValue(static_cast<int>(d.magnifier_fixed_zoom * 10)); }},
        {"magnifier", [this](const Settings& d){ m_magnifierWheelZoom->setChecked(d.magnifier_wheel_zoom); }},
        {"magnifier", [this](const Settings& d){ m_magnifierSize->setValue(d.magnifier_size); }},
        {"magnifier", [this](const Settings& d){ m_magCrossColor->setColor(d.magnifier_cross_color); }},
        {"magnifier", [this](const Settings& d){ m_magCrossAlpha->setValue(d.magnifier_cross_alpha); }},
        {"magnifier", [this](const Settings& d){ m_magCrossThickness->setValue(d.magnifier_cross_thickness); }},
        {"magnifier", [this](const Settings& d){ m_magCrossInvert->setChecked(d.magnifier_cross_invert); }},
        {"magnifier", [this](const Settings& d){ m_gridEnabled->setChecked(d.grid_enabled); }},
        {"magnifier", [this](const Settings& d){ m_gridColor->setColor(d.grid_color); }},
        {"magnifier", [this](const Settings& d){ m_gridAlpha->setValue(d.grid_alpha); }},
        {"magnifier", [this](const Settings& d){ m_edgeModeCombo->setCurrentIndex(d.edge_mode == "crop" ? 0 : 1); }},
        {"magnifier", [this](const Settings& d){ m_edgeColor->setColor(d.edge_color); }},
        {"magnifier", [this](const Settings& d){ m_zoomLabelEnabled->setChecked(d.zoom_label_enabled); }},
        {"magnifier", [this](const Settings& d){ m_zoomLabelTextColor->setColor(d.zoom_label_text_color); }},
        {"magnifier", [this](const Settings& d){ m_zoomLabelTextAlpha->setValue(d.zoom_label_text_alpha); }},
        {"magnifier", [this](const Settings& d){ m_zoomLabelBgColor->setColor(d.zoom_label_bg_color); }},
        {"magnifier", [this](const Settings& d){ m_zoomLabelBgAlpha->setValue(d.zoom_label_bg_alpha); }},

        // ---- 坐标与颜色 ----
        {"labels", [this](const Settings& d){ m_coordLabelEnabled->setChecked(d.coord_label_enabled); }},
        {"labels", [this](const Settings& d){ m_coordTextColor->setColor(d.coord_label_text_color); }},
        {"labels", [this](const Settings& d){ m_coordTextAlpha->setValue(d.coord_label_text_alpha); }},
        {"labels", [this](const Settings& d){ m_coordBgColor->setColor(d.coord_label_bg_color); }},
        {"labels", [this](const Settings& d){ m_coordBgAlpha->setValue(d.coord_label_bg_alpha); }},
        {"labels", [this](const Settings& d){ m_colorLabelEnabled->setChecked(d.color_label_enabled); }},
        {"labels", [this](const Settings& d){ m_colorTextColor->setColor(d.color_label_text_color); }},
        {"labels", [this](const Settings& d){ m_colorTextAlpha->setValue(d.color_label_text_alpha); }},
        {"labels", [this](const Settings& d){ m_colorBgColor->setColor(d.color_label_bg_color); }},
        {"labels", [this](const Settings& d){ m_colorBgAlpha->setValue(d.color_label_bg_alpha); }},
        {"labels", [this](const Settings& d){ m_colorFormatCombo->setCurrentIndex(d.color_format == "rgb" ? 0 : 1); }},
        {"labels", [this](const Settings& d){ m_copyColorKeyEdit->setKeySequence(QKeySequence(d.copy_color_key)); }},
        {"labels", [this](const Settings& d){ m_copyHexPrefix->setChecked(d.copy_hex_prefix); }},
        {"labels", [this](const Settings& d){ m_copyRgbPrefix->setChecked(d.copy_rgb_prefix); }},

        // ---- 存储 ----
        {"storage", [this](const Settings&){ m_dirEdit->setText(QString()); }},          // save_dir 动态默认，清空由用户重选
        {"storage", [this](const Settings&){ m_formatCombo->setCurrentIndex(0); }},      // 默认 png
        {"storage", [this](const Settings&){ m_configEdit->clear(); m_configEdit->setPlaceholderText("（程序所在目录）"); }},

        // ---- OCR ----
        {"ocr", [this](const Settings&){ m_ocrTessdataEdit->clear(); m_ocrTessdataEdit->setPlaceholderText("留空则自动检测"); }},

        // ---- AI 设置 ----
        // 先恢复默认服务商（触发 onProviderChanged 同步其默认 API 地址/Key），
        // 避免"服务商保持用户所选、地址却恢复为默认"的错配（对齐 Python _reset_map）
        {"ai_settings", [this](const Settings& d){
            int idx = m_aiProviderCombo->findData(d.ai_provider);
            if (idx >= 0) m_aiProviderCombo->setCurrentIndex(idx);
        }},
        {"ai_settings", [this](const Settings&){ m_aiApiKeyEdit->clear(); }},
        // 恢复默认 API Base（而非清空），与 Python _reset_map 的"默认值回填"语义一致
        {"ai_settings", [this](const Settings& d){ m_aiApiBaseEdit->setText(d.ai_api_base); }},
        {"ai_settings", [this](const Settings&){ m_aiModelCombo->clearEditText(); }},
        {"ai_settings", [this](const Settings& d){ m_aiTimeout->setValue(d.ai_timeout); }},
        {"ai_settings", [this](const Settings& d){ m_aiTemperature->setValue(static_cast<int>(d.ai_temperature * 100)); }},
        {"ai_settings", [this](const Settings& d){ m_aiMaxTokens->setValue(d.ai_max_tokens); }},
        {"ai_settings", [this](const Settings& d){ m_aiTopP->setValue(static_cast<int>(d.ai_top_p * 100)); }},
        {"ai_settings", [this](const Settings& d){ m_aiFrequencyPenalty->setValue(static_cast<int>(d.ai_frequency_penalty * 100)); }},
        {"ai_settings", [this](const Settings& d){ m_aiPresencePenalty->setValue(static_cast<int>(d.ai_presence_penalty * 100)); }},
        {"ai_settings", [this](const Settings&){ m_aiSeedEdit->setText("0"); }},

        // ---- 翻译 ----
        {"translate", [this](const Settings& d){
            int idx = m_aiTargetLangCombo->findText(d.ai_target_lang);
            if (idx >= 0) m_aiTargetLangCombo->setCurrentIndex(idx);
        }},
        {"translate", [this](const Settings& d){ m_aiConfirmBefore->setChecked(d.ai_confirm_before_translate); }},
        {"translate", [this](const Settings& d){ m_aiStreamThinking->setChecked(d.ai_stream_thinking); }},
        {"translate", [this](const Settings& d){ m_aiPromptEdit->setPlainText(d.ai_translation_prompt); }},
        {"translate", [this](const Settings& d){ m_textPromptEdit->setPlainText(d.text_translation_prompt); }},

        // ---- 常规 ----
        {"general", [this](const Settings& d){ m_appModeCombo->setCurrentIndex(d.app_mode == "translate" ? 0 : 1); }},
        {"general", [this](const Settings&){ m_closeToTrayCombo->setCurrentIndex(0); }},  // 默认"询问"
        {"general", [this](const Settings& d){ m_notifyCopy->setChecked(d.notify_copy); }},
        {"general", [this](const Settings& d){ m_notifySaveSuccess->setChecked(d.notify_save_success); }},
        {"general", [this](const Settings& d){ m_notifySaveFail->setChecked(d.notify_save_fail); }},
        {"general", [this](const Settings& d){ m_notifyCaptureFail->setChecked(d.notify_capture_fail); }},
        {"general", [this](const Settings& d){ m_notifyHotkeyFail->setChecked(d.notify_hotkey_fail); }},
        {"general", [this](const Settings& d){ m_notifyTransSuccess->setChecked(d.notify_translate_success); }},
        {"general", [this](const Settings& d){ m_notifyTransFail->setChecked(d.notify_translate_fail); }},
        {"general", [this](const Settings& d){ m_notifyOcrFail->setChecked(d.notify_ocr_fail); }},
        {"general", [this](const Settings& d){ m_notifyLangDownload->setChecked(d.notify_lang_download); }},
        {"general", [this](const Settings& d){ m_logDebugEnabled->setChecked(d.log_debug_enabled); }},
        {"general", [this](const Settings& d){ m_logInfoEnabled->setChecked(d.log_info_enabled); }},
        {"general", [this](const Settings& d){ m_logWarningEnabled->setChecked(d.log_warning_enabled); }},
        {"general", [this](const Settings& d){ m_logErrorEnabled->setChecked(d.log_error_enabled); }},
    };
}

void SettingsDialog::resetTab(const QString& tab, const QString& scope) {
    if (!confirmReset(scope)) return;
    ++m_fetchGen;  // 使进行中的模型获取失效（防止"恢复本页默认"后被请求结果覆盖）
    Settings d(Settings::defaultsDict());
    for (const auto& item : m_resetItems)
        if (item.tab == tab) item.apply(d);
    refreshToggleStates();  // 重置后统一刷新依赖控件的启用/可见状态
}

void SettingsDialog::refreshToggleStates() {
    toggleCrosshairWidgets(m_crosshairInvert->isChecked());
    toggleMagnifierWidgets(m_magnifierEnabled->isChecked());
    toggleWheelZoomWidgets(m_magnifierWheelZoom->isChecked());
    toggleGridWidgets(m_gridEnabled->isChecked());
    toggleEdgeColorWidgets(m_edgeModeCombo->currentIndex() == 1);  // 1 = pad
    toggleZoomLabelWidgets(m_zoomLabelEnabled->isChecked());
    toggleCoordLabelWidgets(m_coordLabelEnabled->isChecked());
    toggleColorLabelWidgets(m_colorLabelEnabled->isChecked());
    toggleMagCrossWidgets(m_magCrossInvert->isChecked());
}
