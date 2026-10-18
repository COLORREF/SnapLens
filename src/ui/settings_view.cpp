#include <QHeaderView>
#include <QJsonDocument>
#include <QSignalBlocker>
#include <QDateTime>
#include "settings_view.h"
#include "color_picker.h"
#include "snapkit.h"
#include "../settings.h"
#include "../notify/notify_manager.h"
#include "../ocr/ocr_engine.h"
#include "../translate/providers.h"
#include "../translate/ai_client.h"
#include "../translate/ai_request_options.h"
#include "../translate/model_capabilities.h"
#include "../log/log_manager.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QRegularExpression>
#include <QPushButton>
#include <QSpinBox>
#include <QStackedWidget>
#include <QUrl>
#include <QVBoxLayout>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>

using namespace SnapUi;

// ============================================================================
// 局部辅助
// ============================================================================

namespace {

/// WinUI3 的错误语义色（ElaTheme 未提供，按主题给一对）。
QColor errorColor()
{
    return eTheme->getThemeMode() == ElaThemeType::Dark
               ? QColor(QStringLiteral("#FF99A4"))
               : QColor(QStringLiteral("#C42B1C"));
}

/// 与 ElaLineEdit 观感对齐的热键录入框。
/// QKeySequenceEdit 是原生控件、无法跟随 ElaTheme，因此套一层 Fluent 外壳。
/// out 返回内层的 QKeySequenceEdit，供读写快捷键序列。
QWidget* makeHotkeyField(const QString& sequence, QKeySequenceEdit** out, QWidget* parent)
{
    auto* edit = new QKeySequenceEdit(QKeySequence(sequence));
    edit->setClearButtonEnabled(true);
    edit->setToolTip("点击后直接按下想要的组合键");
    if (out) *out = edit;
    auto* host = inputShell(edit, parent);
    host->setMinimumWidth(240);
    return host;
}

ElaText* makeNoteLabel(const QString& text, QWidget* parent)
{
    auto* label = new ElaText(text, parent);
    label->setTextPixelSize(12);
    label->setIsWrapAnywhere(true);
    makeSecondaryText(label);
    return label;
}

/// 卡片内部的小分组标题（比页面分区标题低一级）。
ElaText* makeGroupLabel(const QString& text, QWidget* parent)
{
    auto* label = new ElaText(text, parent);
    label->setTextPixelSize(13);
    QFont font = label->font();
    font.setWeight(QFont::DemiBold);
    label->setFont(font);
    return label;
}

/// ElaToggleSwitch 程序化赋值：静默设置，避免把「回填/重置」误当成用户操作。
void setToggle(ElaToggleSwitch* toggle, bool value)
{
    if (!toggle) return;
    const bool blocked = toggle->blockSignals(true);
    toggle->setIsToggled(value);
    toggle->blockSignals(blocked);
}

} // namespace

// ============================================================================
// OcrLangManager — OCR 语言包管理
// ============================================================================

static QSet<QString> scanInstalledLangs(const QString& tessdataDir) {
    QSet<QString> installed;
    if (tessdataDir.isEmpty()) return installed;
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
            item->widget()->deleteLater();
        } else if (item->layout()) {
            clearLayoutRecursively(item->layout());
        }
        delete item;
    }
}

OcrLangManager::OcrLangManager(const QString& currentLangs, const QString& customTessdataDir,
                               NotifyManager* notifyManager,
                               QWidget* settingsViewParent, QWidget* parent)
    : QWidget(parent)
    , m_notifyManager(notifyManager)
    , m_settingsViewParent(settingsViewParent)
    , m_nam(new QNetworkAccessManager(this))
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);
    reload(currentLangs, customTessdataDir);
}

void OcrLangManager::reload(const QString& languages, const QString& directory)
{
    // A directory change must not redirect an in-flight download to another path.
    for (const auto& download : std::as_const(m_downloads)) {
        disconnect(download.first, nullptr, this, nullptr);
        download.first->abort();
        download.first->deleteLater();
    }
    m_downloads.clear();
    m_tessdataDir = directory.trimmed().isEmpty() ? OcrEngine::findTessdataDir() : directory.trimmed();
    if (scanInstalledLangs(m_tessdataDir).isEmpty() && QDir(m_tessdataDir + "/tessdata").exists())
        m_tessdataDir += "/tessdata";
    m_selected.clear();
    for (const auto& language : languages.split('+', Qt::SkipEmptyParts)) m_selected.insert(language);
    m_installed = scanInstalledLangs(m_tessdataDir);
    rebuildUi();
}

void OcrLangManager::rebuildUi() {
    auto* lay = qobject_cast<QVBoxLayout*>(layout());
    clearLayoutRecursively(lay);
    m_checks.clear();

    if (m_tessdataDir.isEmpty()) {
        lay->addWidget(makeNoteLabel(
            "未检测到语言包目录，请确认 tessdata 目录可访问。", this));
        return;
    }

    lay->addWidget(makeNoteLabel(
        QStringLiteral("语言包目录：%1").arg(m_tessdataDir), this));

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

    if (!installedKnown.isEmpty() || !unknownInstalled.isEmpty() || !m_downloads.isEmpty()) {
        lay->addSpacing(6);
        lay->addWidget(makeGroupLabel("已安装", this));
        QStringList sorted = installedKnown.keys();
        sorted.sort();
        for (const auto& code : sorted) {
            auto* row = new QHBoxLayout();
            bool isDownloading = downloadingCodes.contains(code);

            auto* cb = new ElaCheckBox(installedKnown[code] + " (" + code + ")");
            cb->setChecked(m_selected.contains(code));
            connect(cb, &QCheckBox::toggled, this, [this, code](bool checked) {
                if (checked) m_selected.insert(code); else m_selected.remove(code);
            });
            cb->setEnabled(!isDownloading);
            row->addWidget(cb);
            m_checks[code] = cb;

            if (isDownloading) {
                auto* bar = new ElaProgressBar();
                bar->setRange(0, 100);
                bar->setValue(0);
                bar->setFixedWidth(120);
                bar->setFixedHeight(16);
                bar->setFormat("%p%");
                row->addWidget(bar);
                auto* reply = m_downloads[code].first;
                connect(reply, &QNetworkReply::downloadProgress, bar,
                        [bar](qint64 received, qint64 total) {
                    if (total > 0) {
                        bar->setRange(0, static_cast<int>(total));
                        bar->setValue(static_cast<int>(received));
                    }
                });
            } else {
                auto* delBtn = subtleButton("删除", this);
                delBtn->setFixedWidth(56);
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
            auto* cb = new ElaCheckBox(code + "（未知语言）");
            cb->setChecked(m_selected.contains(code));
            connect(cb, &QCheckBox::toggled, this, [this, code](bool checked) {
                if (checked) m_selected.insert(code); else m_selected.remove(code);
            });
            row->addWidget(cb);
            m_checks[code] = cb;

            auto* delBtn = subtleButton("删除", this);
            delBtn->setFixedWidth(56);
            connect(delBtn, &QPushButton::clicked, this, [this, code]() {
                deleteLang(code, code);
            });
            row->addWidget(delBtn);
            row->addStretch();
            lay->addLayout(row);
        }
    }

    if (!available.isEmpty()) {
        lay->addSpacing(8);
        lay->addWidget(makeGroupLabel("可下载：", this));
        QStringList availKeys = available.keys();
        availKeys.sort();
        for (const auto& code : availKeys) {
            auto* row = new QHBoxLayout();
            row->addWidget(cardTitle(QStringLiteral("%1 (%2)").arg(available[code], code),
                                     this));
            row->addStretch();
            auto* dlBtn = subtleButton("下载", this);
            dlBtn->setFixedWidth(56);
            QString url = QString("https://cdn.jsdelivr.net/gh/tesseract-ocr/tessdata_fast@main/%1.traineddata").arg(code);
            connect(dlBtn, &QPushButton::clicked, this, [this, code, name = available[code], url]() {
                startDownload(code, name, url);
            });
            row->addWidget(dlBtn);
            lay->addLayout(row);
        }
    }

    lay->addStretch();
}

void OcrLangManager::startDownload(const QString& code, const QString& name, const QString& url)
{
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
            m_settingsViewParent))
            return;
    }

    // Matching Python _start_download: insert download row dynamically
    // BEFORE starting the network request, so downloadProgress has a bar to update
    auto* lay = qobject_cast<QVBoxLayout*>(layout());

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
    auto* cb = new ElaCheckBox(name + " (" + code + ")");
    cb->setEnabled(false);
    dlRow->addWidget(cb);
    m_checks[code] = cb;

    auto* bar = new ElaProgressBar();
    bar->setRange(0, 100);
    bar->setValue(0);
    bar->setFixedWidth(120);
    bar->setFixedHeight(16);
    bar->setFormat("%p%");
    dlRow->addWidget(bar);
    dlRow->addStretch();
    lay->insertLayout(insertIdx, dlRow);

    QDir().mkpath(m_tessdataDir);
    QUrl qurl(url);
    QNetworkRequest req(qurl);
    req.setRawHeader("User-Agent", "Mozilla/5.0");

    auto* reply = m_nam->get(req);
    m_downloads[code] = {reply, name};

    connect(reply, &QNetworkReply::downloadProgress, bar,
            [bar](qint64 received, qint64 total) {
        if (total > 0) {
            bar->setRange(0, static_cast<int>(total));
            bar->setValue(static_cast<int>(received));
        }
    });

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
    if (!m_notifyManager || !m_notifyManager->confirm("确认删除",
            QString("确定要删除语言包「%1」(%2) 吗？\n\n"
                    "文件将被永久删除：\n%3/%2.traineddata")
                .arg(name).arg(code).arg(m_tessdataDir),
            m_settingsViewParent))
        return;

    QString filePath = QDir(m_tessdataDir).filePath(code + ".traineddata");
    if (QFile::remove(filePath)) {
        m_installed.remove(code);
        m_selected.remove(code);
        rebuildUi();
        if (m_notifyManager)
            m_notifyManager->info("已删除", QString("语言包「%1」已删除。").arg(name),
                                  m_settingsViewParent);
    } else {
        if (m_notifyManager)
            m_notifyManager->warn("删除失败", QString("无法删除文件：%1").arg(filePath),
                                  m_settingsViewParent);
    }
}

QString OcrLangManager::getSelectedLangs() const {
    // 默认配置可能包含尚未安装的语言；这些语言没有复选框，不能隐式保存为启用。
    // 重新扫描也避免将已被外部删除的语言包保存到下一次 OCR 任务。
    QStringList languages = (m_selected & scanInstalledLangs(m_tessdataDir)).values();
    languages.sort();
    return languages.join('+');
}

// ============================================================================
// SettingsView — 外壳
// ============================================================================

SettingsView::SettingsView(Settings* settings, NotifyManager* notifyManager, QWidget* parent)
    : QWidget(parent)
    , m_settings(settings)
    , m_notifyManager(notifyManager)
{
    if (!settings->ai_api_key.isEmpty())
        m_providerKeys[settings->ai_provider] = settings->ai_api_key;
    m_currentProvider = settings->ai_provider;

    buildUi();
    buildResetTable();
    loadFromSettings();
    polishPopup(this);
}

SettingsView::~SettingsView() = default;  // unique_ptr<ApiClient> 在此处析构（完整类型）

void SettingsView::buildUi()
{
    // 版式（对齐 WinUI3 设置页）：页面不套外层卡片，设置行直接落在页面底色上，
    // 避免「卡片套卡片再套卡片」的割裂感。
    //   ┌ 页头（大标题 + 说明）
    //   ├ 分类栏（扁平） │ 内容区（可滚动）
    //   └ 全宽分隔线 + 操作栏
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    auto* head = new QWidget(this);
    auto* headLayout = new QVBoxLayout(head);
    headLayout->setContentsMargins(Dim::PageHMargin, 20, Dim::PageHMargin, 12);
    headLayout->setSpacing(2);
    headLayout->addWidget(pageTitle("设置", head));
    headLayout->addWidget(pageSubtitle(
        "管理应用偏好、共享服务与各工具的专属设置；修改在点击「保存设置」后生效。", head));
    root->addWidget(head);

    m_body = buildBody();
    root->addWidget(m_body, 1);

    root->addWidget(separator(Qt::Horizontal, this));

    // ── 底部操作栏 ──
    auto* footer = new QWidget(this);
    footer->setFixedHeight(60);
    auto* actions = new QHBoxLayout(footer);
    actions->setContentsMargins(Dim::PageHMargin, 0, Dim::PageHMargin, 0);
    actions->setSpacing(8);
    auto* resetAllBtn = secondaryButton("恢复全部默认", footer);
    resetAllBtn->setToolTip("把全部设置项恢复为出厂默认值");
    connect(resetAllBtn, &QPushButton::clicked, this, &SettingsView::onResetAll);
    actions->addWidget(resetAllBtn);
    actions->addStretch();
    actions->addWidget(caption("修改后需点击「保存设置」才会生效", footer));
    actions->addSpacing(8);
    auto* revertBtn = secondaryButton("放弃修改", footer);
    revertBtn->setToolTip("丢弃尚未保存的修改，恢复为当前已保存的配置");
    connect(revertBtn, &QPushButton::clicked, this, &SettingsView::onRevert);
    auto* saveBtn = primaryButton("保存设置", footer);
    connect(saveBtn, &QPushButton::clicked, this, &SettingsView::onSave);
    actions->addWidget(revertBtn);
    actions->addWidget(saveBtn);
    root->addWidget(footer);

    // 标题栏的主题按钮会绕过 applyThemeMode 直接改 ElaTheme，
    // 这里订阅广播，保持「主题」下拉与实际外观一致。
    connect(ThemeNotifier::instance(), &ThemeNotifier::themeModeChanged, this,
            [this](ThemeMode mode) {
                if (!m_themeCombo) return;
                const int idx = m_themeCombo->findData(themeModeToString(mode));
                if (idx < 0 || idx == m_themeCombo->currentIndex()) return;
                const bool blocked = m_themeCombo->blockSignals(true);
                m_themeCombo->setCurrentIndex(idx);
                m_themeCombo->blockSignals(blocked);
            });
}

QWidget* SettingsView::buildBody()
{
    auto* body = new QWidget(this);
    auto* bodyLayout = new QHBoxLayout(body);
    bodyLayout->setContentsMargins(Dim::PageHMargin, 0, Dim::PageHMargin, 0);
    bodyLayout->setSpacing(0);

    auto* sidebar = new QWidget(body);
    sidebar->setFixedWidth(226);
    auto* sidebarLayout = new QVBoxLayout(sidebar);
    sidebarLayout->setContentsMargins(0, 0, 0, 0);
    auto* search = new ElaLineEdit(sidebar);
    m_navSearch = search;
    search->setPlaceholderText("搜索设置分类");
    sidebarLayout->addWidget(search);
    m_nav = new ElaNavigationBar(sidebar);
    m_nav->setNavigationBarWidth(226);
    m_nav->setUserInfoCardVisible(false);
    m_nav->setIsAllowPageOpenInNewWindow(false);
    sidebarLayout->addWidget(m_nav);
    m_stack = new QStackedWidget(body);
    m_stack->setStyleSheet("QStackedWidget { background: transparent; }");

    struct PageDef {
        const char* id;
        const char* group;
        const char* label;
        std::function<QWidget*()> build;
    };
    const QList<PageDef> pages = {
        {"general", "应用", "常规", [this] { return buildGeneralTab(); }},
        {"storage", "应用", "存储", [this] { return buildStorageTab(); }},
        {"ai", "共享服务", "AI 服务", [this] { return buildAiSettingsTab(); }},
        {"ocr", "共享服务", "OCR 识别", [this] { return buildOcrTab(); }},
        {"screenshot", "截图", "操作", [this] { return buildScreenshotTab(); }},
        {"magnifier", "截图", "放大镜", [this] { return buildMagnifierTab(); }},
        {"labels", "截图", "坐标与颜色", [this] { return buildLabelsTab(); }},
        {"translate", "翻译", "翻译偏好", [this] { return buildTranslateTab(); }},
    };
    QMap<QString, QString> groups;
    for (const auto& def : pages) {
        const QString group = QString::fromUtf8(def.group);
        QString groupKey = groups.value(group);
        if (groupKey.isEmpty()) {
            m_nav->addExpanderNode(group, groupKey);
            groups.insert(group, groupKey);
        }
        auto* page = def.build();
        const int index = m_stack->addWidget(page);
        m_nav->addPageNode(QString::fromUtf8(def.label), page, groupKey);
        m_navPages.append(page->property("ElaPageKey").toString());
        m_navGroups.append(groupKey);
        m_categories.insert(QString::fromUtf8(def.id), index);
    }
    for (const auto& key : groups) m_nav->expandNode(key);
    connect(m_nav, &ElaNavigationBar::navigationNodeClicked, this,
            [this](ElaNavigationType::NavigationNodeType, const QString& key, bool) {
                const int index = m_navPages.indexOf(key);
                if (index >= 0) m_stack->setCurrentIndex(index);
            });
    connect(search, &QLineEdit::textChanged, this, [this](const QString& query) {
        // 当前 SDK 没有节点过滤接口，仅借用内部视图隐藏行；绘制、选择和动画仍由 Ela 管理。
        auto* view = m_nav->findChild<ElaNavigationView*>();
        if (!view) return;
        auto* model = view->model();
        for (int i = 0; i < model->rowCount(); ++i) {
            const auto group = model->index(i, 0);
            const auto* groupNode = static_cast<ElaNavigationNode*>(group.internalPointer());
            bool any = false;
            for (int j = 0; j < model->rowCount(group); ++j) {
                const auto child = model->index(j, 0, group);
                const auto* node = static_cast<ElaNavigationNode*>(child.internalPointer());
                const bool match = (groupNode->getNodeTitle() + node->getNodeTitle())
                                       .contains(query.trimmed(), Qt::CaseInsensitive);
                view->setRowHidden(j, group, !match);
                any |= match;
            }
            view->setRowHidden(i, {}, !any);
            if (any) m_nav->expandNode(groupNode->getNodeKey());
        }
    });
    selectPage(0);

    bodyLayout->addWidget(sidebar);
    // 分类栏与内容区之间只留间距（不加分隔线）：WinUI3 设置页靠间距与层级区分
    bodyLayout->addSpacing(28);
    bodyLayout->addWidget(m_stack, 1);

    return body;
}

void SettingsView::selectPage(int index)
{
    if (index < 0 || index >= m_navPages.size()) return;
    // 外部入口跳转清除过滤，并恢复当前页被折叠的父分类。
    m_navSearch->clear();
    m_nav->expandNode(m_navGroups.at(index));
    m_nav->navigation(m_navPages.at(index), false);
    m_stack->setCurrentIndex(index);
}
void SettingsView::selectCategory(const QString& id)
{
    if (m_categories.contains(id)) selectPage(m_categories.value(id));
}
int SettingsView::pageCount() const { return m_stack->count(); }

// ============================================================================
// 页面装配辅助
// ============================================================================

void SettingsView::appendPageReset(SettingsPage* page, const QString& tabId,
                                   const QString& tabLabel)
{
    page->addSection("重置");
    auto* host = new QWidget(page);
    auto* layout = new QHBoxLayout(host);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(makeNoteLabel(
        QStringLiteral("把本页的全部选项恢复为默认值（其他页面不受影响）。"), host));
    layout->addStretch();
    auto* button = secondaryButton("恢复本页默认", host);
    layout->addWidget(button);
    connect(button, &QPushButton::clicked, this,
            [this, tabId, tabLabel] { resetTab(tabId, tabLabel); });
    page->addBlock(host);
}

SettingsCard* SettingsView::colorRow(SettingsPage* page, const QString& title,
                                     const QString& desc, const QString& hex,
                                     ColorPickerButton** out)
{
    auto* picker = new ColorPickerButton(hex, page);
    auto* card = row(title, desc, picker, page);
    page->addBlock(card);
    if (out) *out = picker;
    return card;
}

// ============================================================================
// 页面：截图
// ============================================================================

QWidget* SettingsView::buildScreenshotTab()
{
    auto* page = new SettingsPage("截图", "全局热键、十字准星与截图工具条的外观。");

    page->addSection("快捷键与工具条");
    m_hotkeyEdit = nullptr;
    page->addBlock(row("截图快捷键", "任意程序前台时均可触发",
                       makeHotkeyField(m_settings->hotkey, &m_hotkeyEdit, page), page));
    page->addBlock(sliderRow("工具条不透明度", "浮动工具条的不透明度",
                             m_settings->toolbar_opacity, 1, 100, "%",
                             &m_toolbarOpacity, page));

    page->addSection("全屏十字准星", "拖动截图时贯穿整屏的辅助线，用于对齐。");
    auto* crosshairSwitch = toggleRow("显示全屏十字线", "关闭后只显示鼠标",
                                      m_settings->crosshair_enabled,
                                      &m_crosshairEnabled, page);
    page->addBlock(crosshairSwitch);

    // 控制项先于从属项，显隐切换时保持从上到下的依赖顺序。
    auto* invertCard = toggleRow("反色十字线",
                                 "用差值混合绘制，深浅背景都清晰",
                                 m_settings->crosshair_invert, &m_crosshairInvert, page);
    page->addBlock(invertCard);

    auto* crosshairColorCard = colorRow(page, "十字线颜色", "深色背景下建议用亮色",
                                        m_settings->crosshair_color, &m_crosshairColor);
    auto* alphaCard = sliderRow("不透明度", "越低越不遮挡画面",
                                m_settings->crosshair_alpha, 10, 100, "%",
                                &m_crosshairAlpha, page);
    page->addBlock(alphaCard);
    auto* thicknessCard = sliderRow("线条粗细", "线条宽度（物理像素）",
                                    m_settings->crosshair_thickness, 1, 10, " px",
                                    &m_crosshairThickness, page);
    page->addBlock(thicknessCard);

    m_screenshotCrosshairRows = {crosshairColorCard, alphaCard, thicknessCard, invertCard};
    m_screenshotCrosshairColorRows = {crosshairColorCard, alphaCard};
    connect(m_crosshairEnabled, &ElaToggleSwitch::toggled, this,
            [this](bool) { refreshScreenshotPage(); });
    connect(m_crosshairInvert, &ElaToggleSwitch::toggled, this,
            [this](bool) { refreshScreenshotPage(); });
    refreshScreenshotPage();

    page->addSection("鼠标光标");
    page->addBlock(toggleRow("键盘微调准星", "每次按下移动 1 个屏幕物理像素，长按不连发",
        m_settings->cursor_keyboard_enabled, &m_cursorKeyboardEnabled, page));
    page->addBlock(comboRow("准星移动按键", "不按住 Ctrl、Shift 或 Alt 时生效",
        {"方向键", "WASD", "方向键和 WASD"},
        m_settings->cursor_keyboard_mode == "wasd" ? 1 : m_settings->cursor_keyboard_mode == "both" ? 2 : 0,
        &m_cursorKeyboardMode, page));
    m_cursorKeyboardEnabled->setObjectName("cursorKeyboardEnabled");
    m_cursorKeyboardMode->setObjectName("cursorKeyboardMode");
    m_cursorKeyboardMode->setEnabled(m_settings->cursor_keyboard_enabled);
    connect(m_cursorKeyboardEnabled, &ElaToggleSwitch::toggled, this,
        [this](bool enabled) { m_cursorKeyboardMode->setEnabled(enabled); });
    page->addBlock(toggleRow("使用系统十字光标",
                             "关闭后截图时隐藏光标，仅由十字准星定位",
                             m_settings->cursor_enabled, &m_cursorEnabled, page));

    appendPageReset(page, "screenshot", "截图页面");
    return page;
}

// ============================================================================
// 页面：放大镜
// ============================================================================

QWidget* SettingsView::buildMagnifierTab()
{
    auto* page = new SettingsPage("放大镜", "截图时跟随光标的像素级放大镜。");
    QList<QWidget*> rows;   // 总开关关闭时隐藏从属卡片与分区标题。

    page->addSection("基本");
    auto* enableCard = toggleRow("启用像素放大镜",
                                 "关闭后在光标旁改为显示坐标与色值标签",
                                 m_settings->magnifier_enabled, &m_magnifierEnabled, page);
    page->addBlock(enableCard);

    auto* wheelZoomCard = toggleRow("允许滚轮实时调节倍率",
                                    "截图过程中滚动滚轮即可缩放放大镜",
                                    m_settings->magnifier_wheel_zoom, &m_magnifierWheelZoom, page);
    page->addBlock(wheelZoomCard);
    rows << wheelZoomCard;

    auto* defaultZoomCard = decimalSliderRow(
        "默认放大倍率", "开启滚轮调节时使用的初始倍率",
        m_settings->magnifier_zoom, 4.0, 20.0, 1, "×", &m_magnifierZoom, page);
    page->addBlock(defaultZoomCard);
    rows << defaultZoomCard;

    auto* fixedZoomCard = decimalSliderRow(
        "固定放大镜倍率", "滚轮调节关闭时使用的倍率",
        m_settings->magnifier_fixed_zoom, 4.0, 20.0, 1, "×", &m_magnifierFixedZoom, page);
    page->addBlock(fixedZoomCard);
    rows << fixedZoomCard;

    auto* sizeCard = sliderRow("源区域半径",
                               "从光标处向四周取样的像素范围；半径越大视野越广、倍率越低",
                               m_settings->magnifier_size, 5, 30, " px",
                               &m_magnifierSize, page);
    page->addBlock(sizeCard);
    rows << sizeCard;

    rows << page->addSection("放大镜十字线", "标记放大区域正中心的像素。");
    auto* magCrossInvertCard = toggleRow("反色十字线", {},
                                         m_settings->magnifier_cross_invert,
                                         &m_magCrossInvert, page);
    page->addBlock(magCrossInvertCard);
    auto* magCrossColorCard = colorRow(page, "十字线颜色", {},
                                       m_settings->magnifier_cross_color, &m_magCrossColor);
    auto* magCrossAlphaCard = sliderRow("不透明度", {},
                                        m_settings->magnifier_cross_alpha, 30, 100, "%",
                                        &m_magCrossAlpha, page);
    page->addBlock(magCrossAlphaCard);
    auto* magCrossThicknessCard = sliderRow("线条粗细", {},
                                            m_settings->magnifier_cross_thickness, 1, 10, " px",
                                            &m_magCrossThickness, page);
    page->addBlock(magCrossThicknessCard);
    m_magCrossColorRows = {magCrossColorCard, magCrossAlphaCard};
    rows << magCrossColorCard << magCrossAlphaCard << magCrossThicknessCard << magCrossInvertCard;

    rows << page->addSection("像素网格", "在放大镜上覆盖网格线，便于逐像素取色。");
    auto* gridCard = toggleRow("绘制像素网格线", {},
                               m_settings->grid_enabled, &m_gridEnabled, page);
    page->addBlock(gridCard);
    auto* gridColorCard = colorRow(page, "网格线颜色", {},
                                   m_settings->grid_color, &m_gridColor);
    auto* gridAlphaCard = sliderRow("网格线不透明度", {},
                                    m_settings->grid_alpha, 1, 100, "%",
                                    &m_gridAlpha, page);
    page->addBlock(gridAlphaCard);
    m_gridRows = {gridColorCard, gridAlphaCard};
    rows << gridCard << gridColorCard << gridAlphaCard;

    rows << page->addSection("屏幕边缘策略", "光标贴近屏幕边缘时如何填充放大镜。");
    auto* edgeModeCard = comboRow("边缘处理", "裁剪：只显示屏幕内区域；填充：保持固定大小并补色",
                                 {"裁剪到屏幕边缘", "保持固定大小（填充边缘）"},
                                 m_settings->edge_mode == "crop" ? 0 : 1,
                                 &m_edgeModeCombo, page);
    page->addBlock(edgeModeCard);
    rows << edgeModeCard;
    auto* edgeColorCard = colorRow(page, "填充颜色", "边缘填充时使用的底色",
                                   m_settings->edge_color, &m_edgeColor);
    m_edgeColorRows = {edgeColorCard};
    rows << edgeColorCard;

    rows << page->addSection("倍率标签", "显示当前放大倍率的提示气泡。");
    auto* zoomLabelCard = toggleRow("显示放大倍率标签", {},
                                    m_settings->zoom_label_enabled,
                                    &m_zoomLabelEnabled, page);
    page->addBlock(zoomLabelCard);
    auto* zlTextColorCard = colorRow(page, "文本颜色", {},
                                     m_settings->zoom_label_text_color, &m_zoomLabelTextColor);
    auto* zlTextAlphaCard = sliderRow("文本不透明度", {},
                                      m_settings->zoom_label_text_alpha, 10, 100, "%",
                                      &m_zoomLabelTextAlpha, page);
    page->addBlock(zlTextAlphaCard);
    auto* zlBgColorCard = colorRow(page, "背景颜色", {},
                                   m_settings->zoom_label_bg_color, &m_zoomLabelBgColor);
    auto* zlBgAlphaCard = sliderRow("背景不透明度", {},
                                    m_settings->zoom_label_bg_alpha, 10, 100, "%",
                                    &m_zoomLabelBgAlpha, page);
    page->addBlock(zlBgAlphaCard);
    m_zoomLabelRows = {zlTextColorCard, zlTextAlphaCard, zlBgColorCard, zlBgAlphaCard};
    rows << zoomLabelCard << zlTextColorCard << zlTextAlphaCard
         << zlBgColorCard << zlBgAlphaCard;

    // 滚轮调节开启时显示默认倍率，否则显示固定倍率。
    m_wheelZoomRows = {defaultZoomCard};
    m_fixedZoomRows = {fixedZoomCard};
    m_magnifierRows = rows;

    connect(m_magnifierEnabled, &ElaToggleSwitch::toggled, this,
            [this](bool) { refreshMagnifierPage(); });
    connect(m_magnifierWheelZoom, &ElaToggleSwitch::toggled, this,
            [this](bool) { refreshMagnifierPage(); });
    connect(m_gridEnabled, &ElaToggleSwitch::toggled, this,
            [this](bool) { refreshMagnifierPage(); });
    connect(m_zoomLabelEnabled, &ElaToggleSwitch::toggled, this,
            [this](bool) { refreshMagnifierPage(); });
    connect(m_magCrossInvert, &ElaToggleSwitch::toggled, this,
            [this](bool) { refreshMagnifierPage(); });
    connect(m_edgeModeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int) { refreshMagnifierPage(); });
    refreshMagnifierPage();

    appendPageReset(page, "magnifier", "放大镜页面");
    return page;
}

// ============================================================================
// 页面：坐标与颜色
// ============================================================================

QWidget* SettingsView::buildLabelsTab()
{
    auto* page = new SettingsPage("坐标与颜色", "光标旁的坐标标签、像素色值标签与取色复制规则。");

    page->addSection("屏幕坐标标签");
    auto* coordCard = toggleRow("显示屏幕坐标标签", "显示光标的物理屏幕坐标",
                                m_settings->coord_label_enabled, &m_coordLabelEnabled, page);
    page->addBlock(coordCard);
    auto* coordTextColorCard = colorRow(page, "文字颜色", {},
                                        m_settings->coord_label_text_color, &m_coordTextColor);
    auto* coordTextAlphaCard = sliderRow("文字不透明度", {},
                                         m_settings->coord_label_text_alpha, 10, 100, "%",
                                         &m_coordTextAlpha, page);
    page->addBlock(coordTextAlphaCard);
    auto* coordBgColorCard = colorRow(page, "背景颜色", {},
                                      m_settings->coord_label_bg_color, &m_coordBgColor);
    auto* coordBgAlphaCard = sliderRow("背景不透明度", {},
                                       m_settings->coord_label_bg_alpha, 10, 100, "%",
                                       &m_coordBgAlpha, page);
    page->addBlock(coordBgAlphaCard);
    m_coordLabelRows = {coordTextColorCard, coordTextAlphaCard,
                        coordBgColorCard, coordBgAlphaCard};
    connect(m_coordLabelEnabled, &ElaToggleSwitch::toggled, this,
            [this](bool) { refreshLabelsPage(); });

    page->addSection("像素颜色标签");
    auto* colorCard = toggleRow("显示像素颜色标签", "显示光标所在像素的颜色",
                                m_settings->color_label_enabled, &m_colorLabelEnabled, page);
    page->addBlock(colorCard);
    auto* colorTextColorCard = colorRow(page, "文字颜色", {},
                                        m_settings->color_label_text_color, &m_colorTextColor);
    auto* colorTextAlphaCard = sliderRow("文字不透明度", {},
                                         m_settings->color_label_text_alpha, 10, 100, "%",
                                         &m_colorTextAlpha, page);
    page->addBlock(colorTextAlphaCard);
    auto* colorBgColorCard = colorRow(page, "背景颜色", {},
                                      m_settings->color_label_bg_color, &m_colorBgColor);
    auto* colorBgAlphaCard = sliderRow("背景不透明度", {},
                                       m_settings->color_label_bg_alpha, 10, 100, "%",
                                       &m_colorBgAlpha, page);
    page->addBlock(colorBgAlphaCard);
    auto* formatCard = comboRow("颜色显示格式", "默认表示法；截图时可用下方设置的快捷键临时切换",
                                {"RGB", "十六进制 (Hex)"},
                                m_settings->color_format == "rgb" ? 0 : 1,
                                &m_colorFormatCombo, page);
    page->addBlock(formatCard);
    page->addBlock(toggleRow("显示颜色方块", "色值左侧显示当前像素颜色，方块始终不透明",
                             m_settings->color_swatch_enabled, &m_colorSwatchEnabled, page));
    // 颜色格式同时决定「复制取色」的文本形式，因此始终显示。
    m_colorLabelRows = {colorTextColorCard, colorTextAlphaCard, colorBgColorCard,
                        colorBgAlphaCard};
    connect(m_colorLabelEnabled, &ElaToggleSwitch::toggled, this,
            [this](bool) { refreshLabelsPage(); });

    page->addSection("取色复制", "截图状态下按快捷键即可把光标下的颜色放进剪贴板。");
    m_copyColorKeyEdit = nullptr;
    page->addBlock(row("复制颜色快捷键", {},
                       makeHotkeyField(m_settings->copy_color_key,
                                       &m_copyColorKeyEdit, page), page));
    page->addBlock(comboRow("切换颜色格式快捷键", "同步切换标签与复制格式（RGB / Hex）",
        {"Shift（单独按下）", "自定义快捷键"}, m_settings->color_format_key == "Shift" ? 0 : 1,
        &m_colorFormatKeyMode, page));
    auto* formatKeyRow = row("自定义切换快捷键", {},
        makeHotkeyField(m_settings->color_format_key == "Shift" ? QString() : m_settings->color_format_key,
                        &m_colorFormatKeyEdit, page), page);
    m_colorFormatKeyEdit->setMaximumSequenceLength(1);
    m_colorFormatKeyEdit->setObjectName("colorFormatKeyEdit");
    m_colorFormatKeyMode->setObjectName("colorFormatKeyMode");
    page->addBlock(formatKeyRow);
    formatKeyRow->setVisible(m_colorFormatKeyMode->currentIndex() == 1);
    connect(m_colorFormatKeyMode, &QComboBox::currentIndexChanged, formatKeyRow,
        [formatKeyRow](int index) { formatKeyRow->setVisible(index == 1); });
    page->addBlock(toggleRow("Hex 复制时带 # 号", "例如 #1A2B3C",
                             m_settings->copy_hex_prefix, &m_copyHexPrefix, page));
    page->addBlock(toggleRow("RGB 复制时带 rgb() 包裹", "例如 rgb(26, 43, 60)",
                             m_settings->copy_rgb_prefix, &m_copyRgbPrefix, page));

    refreshLabelsPage();
    appendPageReset(page, "labels", "坐标与颜色页面");
    return page;
}

// ============================================================================
// 页面：存储
// ============================================================================

QWidget* SettingsView::buildStorageTab()
{
    auto* page = new SettingsPage("存储", "截图文件与配置文件的落盘位置、格式。");

    page->addSection("截图文件");
    auto* dirCard = pathRow("图片保存位置", "点击「保存」按钮时的默认目录",
                            m_settings->save_dir, {}, {"浏览…"}, page);
    page->addBlock(dirCard);
    m_dirEdit = pathRowEdit(dirCard);
    connect(pathRowButton(dirCard, 0), &QPushButton::clicked,
            this, &SettingsView::browseSaveDir);

    page->addBlock(comboRow("图片保存格式", "PNG 无损体积大，JPG 有损体积小，BMP 无压缩",
                            {"png", "jpg", "bmp"},
                            qMax(0, QStringList{"png", "jpg", "bmp"}
                                        .indexOf(m_settings->save_format.toLower())),
                            &m_formatCombo, page));

    page->addSection("配置文件");
    auto* configCard = pathRow("配置保存位置",
                               "留空表示与程序同目录",
                               m_settings->config_dir, "（程序所在目录）",
                               {"浏览…", "默认"}, page);
    page->addBlock(configCard);
    m_configEdit = pathRowEdit(configCard);
    connect(pathRowButton(configCard, 0), &QPushButton::clicked,
            this, &SettingsView::browseConfigDir);
    auto* defaultBtn = pathRowButton(configCard, 1);
    defaultBtn->setToolTip("恢复为程序所在目录");
    connect(defaultBtn, &QPushButton::clicked, this, &SettingsView::resetConfigDir);

    appendPageReset(page, "storage", "存储页面");
    return page;
}

// ============================================================================
// 页面：OCR 识别
// ============================================================================

QWidget* SettingsView::buildOcrTab()
{
    auto* page = new SettingsPage("OCR 识别", "Tesseract 语言包管理与数据目录。");

    page->addSection("语言包");
    auto* panel = new PanelCard("已安装与可下载的语言",
                                "勾选的语言会参与识别，语言越多速度越慢。", page);
    m_ocrLangList = new OcrLangManager(m_settings->ai_ocr_langs,
                                       m_settings->ocr_tessdata_dir,
                                       m_notifyManager, this, panel);
    panel->setContent(m_ocrLangList);
    page->addBlock(panel, Dim::SectionGap - Dim::RowGap);

    page->addSection("数据目录");
    auto* tessdataCard = pathRow("语言包目录", "留空则在程序目录中自动查找",
                                 m_settings->ocr_tessdata_dir, "留空则自动检测",
                                 {"浏览…"}, page);
    page->addBlock(tessdataCard);
    m_ocrTessdataEdit = pathRowEdit(tessdataCard);
    connect(pathRowButton(tessdataCard, 0), &QPushButton::clicked, this, [this] {
        browseDir(m_ocrTessdataEdit, "选择语言包目录");
        m_ocrLangList->reload(m_ocrLangList->getSelectedLangs(), m_ocrTessdataEdit->text());
    });

    connect(m_ocrTessdataEdit, &QLineEdit::editingFinished, this, [this] {
        m_ocrLangList->reload(m_ocrLangList->getSelectedLangs(), m_ocrTessdataEdit->text());
    });
    appendPageReset(page, "ocr", "OCR 识别页面");
    return page;
}

// ============================================================================
// 页面：AI 服务
// ============================================================================

QWidget* SettingsView::buildAiSettingsTab()
{
    auto* page = new SettingsPage("AI 服务",
                                  "选择预设服务，或连接本地、代理及反代的 OpenAI 兼容接口。");

    page->addSection("连接");
    auto* providerCard = new SettingsCard("服务商", "自定义服务可填写本机或代理地址", page);
    m_aiProviderCombo = new ElaComboBox(providerCard);
    m_aiProviderCombo->setObjectName("aiProviderCombo");
    for (const auto& pid : PROVIDER_CONFIGS.keys())
        m_aiProviderCombo->addItem(PROVIDER_CONFIGS[pid].label, pid);
    const int pidx = m_aiProviderCombo->findData(m_settings->ai_provider);
    if (pidx >= 0) m_aiProviderCombo->setCurrentIndex(pidx);
    m_aiProviderCombo->setMinimumWidth(190);
    providerCard->addControl(m_aiProviderCombo);
    // 连接放在初始下标设置之后：避免构造期把已保存的 API 地址/Key 覆盖成服务商默认值
    connect(m_aiProviderCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &SettingsView::onProviderChanged);
    page->addBlock(providerCard);

    auto* keyCard = new SettingsCard("API Key", "自定义服务可留空；仅保存在本机 settings.json 中", page);
    keyCard->setControlStretch(1);
    m_aiApiKeyEdit = new ElaLineEdit(keyCard);
    m_aiApiKeyEdit->setObjectName("aiApiKeyEdit");
    m_aiApiKeyEdit->setText(m_settings->ai_api_key);
    m_aiApiKeyEdit->setPlaceholderText("sk-...");
    m_aiApiKeyEdit->setEchoMode(QLineEdit::PasswordEchoOnEdit);
    m_aiApiKeyEdit->setMinimumWidth(240);
    keyCard->addControl(m_aiApiKeyEdit);
    page->addBlock(keyCard);

    auto* baseCard = new SettingsCard("接口根地址", "例如 http://127.0.0.1:5001/v1；自动请求 /models 和 /chat/completions", page);
    baseCard->setControlStretch(1);
    m_aiApiBaseEdit = new ElaLineEdit(baseCard);
    m_aiApiBaseEdit->setObjectName("aiApiBaseEdit");
    m_aiApiBaseEdit->setText(m_settings->ai_api_base);
    m_aiApiBaseEdit->setPlaceholderText("http://127.0.0.1:5001/v1");
    m_aiApiBaseEdit->setMinimumWidth(240);
    baseCard->addControl(m_aiApiBaseEdit);
    page->addBlock(baseCard);

    auto* customModelsCard = new SettingsCard("手动模型列表", "仅自定义服务：用逗号分隔模型 ID；也可直接在下方输入模型", page);
    customModelsCard->setControlStretch(1);
    m_aiCustomModelsEdit = new ElaLineEdit(customModelsCard);
    m_aiCustomModelsEdit->setObjectName("aiCustomModelsEdit");
    m_aiCustomModelsEdit->setText(m_settings->ai_custom_models);
    m_aiCustomModelsEdit->setPlaceholderText("model-a, model-b");
    m_aiCustomModelsEdit->setMinimumWidth(240);
    customModelsCard->addControl(m_aiCustomModelsEdit);
    m_aiCustomModelsCard = customModelsCard;
    page->addBlock(customModelsCard);
    customModelsCard->setVisible(m_settings->ai_provider == "custom");

    auto* modelCard = new SettingsCard("模型", "可直接输入，也可从服务商拉取", page);
    modelCard->setControlStretch(1);
    auto* modelHost = new QWidget(modelCard);
    auto* modelRow = new QHBoxLayout(modelHost);
    modelRow->setContentsMargins(0, 0, 0, 0);
    modelRow->setSpacing(8);
    m_aiModelCombo = new ElaComboBox(modelHost);
    m_aiModelCombo->setObjectName("aiModelCombo");
    m_aiModelCombo->setEditable(true);
    m_aiModelCombo->setMinimumWidth(200);
    m_aiModelCombo->setCurrentText(m_settings->ai_model);
    m_aiModelCombo->setPlaceholderText("输入模型名称，或点击右侧按钮获取");
    connect(m_aiCustomModelsEdit, &QLineEdit::editingFinished, this, [this] {
        if (m_aiProviderCombo->currentData().toString() != "custom") return;
        const QSignalBlocker blocker(m_aiModelCombo);
        const QString selected = m_aiModelCombo->currentText();
        m_aiModelCombo->clear();
        for (const QString& value : m_aiCustomModelsEdit->text().split(QRegularExpression("[,，;；]"), Qt::SkipEmptyParts)) {
            const QString model = value.trimmed();
            if (!model.isEmpty() && m_aiModelCombo->findText(model) < 0) m_aiModelCombo->addItem(model);
        }
        m_aiModelCombo->setCurrentText(selected);
    });
    modelRow->addWidget(m_aiModelCombo, 1);
    m_fetchModelsBtn = secondaryButton("获取模型与能力", modelHost);
    m_fetchModelsBtn->setMinimumWidth(120);
    m_fetchModelsBtn->setFocusPolicy(Qt::NoFocus);
    connect(m_fetchModelsBtn, &QPushButton::clicked, this, &SettingsView::onFetchModels);
    modelRow->addWidget(m_fetchModelsBtn);
    modelCard->addControl(modelHost);
    page->addBlock(modelCard);

    // 拉取失败时的说明条（默认隐藏）
    m_aiErrorRow = new QWidget(page);
    auto* errLayout = new QHBoxLayout(m_aiErrorRow);
    errLayout->setContentsMargins(12, 8, 12, 8);
    m_aiErrorLabel = new ElaText(m_aiErrorRow);
    m_aiErrorLabel->setTextPixelSize(12);
    m_aiErrorLabel->setIsWrapAnywhere(true);
    bindTheme(m_aiErrorLabel, [this] {
        m_aiErrorLabel->setStyleSheet(
            QStringLiteral("background: transparent; color: %1;")
                .arg(errorColor().name(QColor::HexArgb)));
    });
    errLayout->addWidget(m_aiErrorLabel);
    m_aiErrorRow->setVisible(false);
    page->addBlock(m_aiErrorRow, 0);

    // 服务商能力与文档来源提示：参数是否支持、区间上下限都随服务商变化，
    // 在页面顶部说明依据，避免用户按经验乱填后收到 400。
    m_aiParamHintRow = new QWidget(page);
    auto* hintLayout = new QHBoxLayout(m_aiParamHintRow);
    hintLayout->setContentsMargins(12, 8, 12, 8);
    m_aiParamHint = new ElaText(m_aiParamHintRow);
    m_aiParamHint->setTextPixelSize(12);
    m_aiParamHint->setIsWrapAnywhere(true);
    bindTheme(m_aiParamHint, [this] {
        m_aiParamHint->setStyleSheet(QStringLiteral("background: transparent; color: %1;")
                                         .arg(eTheme->getThemeMode() == ElaThemeType::Dark
                                                  ? QStringLiteral("#9DA5B2")
                                                  : QStringLiteral("#5A5F6A")));
    });
    hintLayout->addWidget(m_aiParamHint);
    page->addBlock(m_aiParamHintRow, 0);

    // 常用参数：普通用户真正会调的两项，直接平铺。
    page->addSection("常用参数", "参数按接口地址和模型分别保存；可选参数默认不发送。");

    auto* maxTokensCard = spinRow(
        "最大输出 Token",
        "单次输出预算，不是上下文窗口；关闭自定义时由模型决定",
        m_settings->ai_max_tokens, 1, 2147483647, 256, QString(),
        &m_aiMaxTokens, page);
    page->addBlock(maxTokensCard);
    m_aiMaxTokensCard = maxTokensCard;

    auto* timeoutCard = spinRow("连接与发送超时", "限制建立连接和发送请求的时间，不限制 AI 思考与生成总时长",
                                m_settings->ai_timeout, 5, 120, 5, " 秒",
                                &m_aiTimeout, page);
    page->addBlock(timeoutCard);
    page->addBlock(spinRow("响应空闲超时", "连续未收到响应数据才计时；思考文本和心跳会重置。0 表示不限，适合长推理及非流式请求",
        m_settings->ai_response_idle_timeout, 0, 3600, 30, " 秒", &m_aiResponseIdleTimeout, page));
    m_aiResponseIdleTimeout->setSpecialValueText("不限");

    page->addSection("思考与推理",
                     "显式设置前请确认当前模型支持；自动表示不发送，未知模型可使用高级 JSON。");

    auto* thinkingCard = new SettingsCard(
        "思考模式", "开启后模型先推理再作答；自动表示不发送该字段", page);
    m_aiThinkingCombo = new ElaComboBox(thinkingCard);
    m_aiThinkingCombo->setObjectName(QStringLiteral("aiThinkingCombo"));
    m_aiThinkingCombo->addItem("自动（不发送）", "auto");
    m_aiThinkingCombo->addItem("开启思考", "on");
    m_aiThinkingCombo->addItem("关闭思考", "off");
    m_aiThinkingCombo->setMinimumWidth(200);
    {
        const int idx = m_aiThinkingCombo->findData(m_settings->ai_thinking);
        if (idx >= 0) m_aiThinkingCombo->setCurrentIndex(idx);
    }
    thinkingCard->addControl(m_aiThinkingCombo);
    page->addBlock(thinkingCard);
    m_aiThinkingCard = thinkingCard;

    auto* effortCard = new SettingsCard(
        "推理强度", "优先显示上游声明的档位；无资料时显示通用候选，可手动输入", page);
    m_aiEffortCombo = new ElaComboBox(effortCard);
    m_aiEffortCombo->setObjectName(QStringLiteral("aiEffortCombo"));
    m_aiEffortCombo->setMinimumWidth(200);
    effortCard->addControl(m_aiEffortCombo);
    page->addBlock(effortCard);
    m_aiEffortCard = effortCard;

    // 高级设置：采样、惩罚项、种子、停止序列等细粒度参数默认收起，
    // 普通用户无需关心；留「自动」即不发送，交给服务商默认值。
    m_aiAdvanced = new AdvancedSection(
        "高级设置",
        "采样、惩罚项与停止序列等细粒度参数。留「自动」即不发送，使用服务商默认值；"
        "显式值原样发送，是否支持由当前模型决定。",
        page);
    m_aiAdvanced->setObjectName(QStringLiteral("aiAdvancedSection"));
    page->addBlock(m_aiAdvanced);

    auto* doSampleCard = new SettingsCard(
        "启用采样", "显式发送 do_sample；是否支持及与温度的关系由当前模型决定", m_aiAdvanced);
    m_aiDoSampleCombo = new ElaComboBox(doSampleCard);
    m_aiDoSampleCombo->setObjectName(QStringLiteral("aiDoSampleCombo"));
    m_aiDoSampleCombo->addItem("自动（不发送，用服务商默认）", "auto");
    m_aiDoSampleCombo->addItem("开启采样", "on");
    m_aiDoSampleCombo->addItem("关闭采样（发送 false）", "off");
    m_aiDoSampleCombo->setMinimumWidth(220);
    {
        const int idx = m_aiDoSampleCombo->findData(m_settings->ai_do_sample);
        if (idx >= 0) m_aiDoSampleCombo->setCurrentIndex(idx);
    }
    doSampleCard->addControl(m_aiDoSampleCombo);
    m_aiAdvanced->addRow(doSampleCard);
    m_aiDoSampleCard = doSampleCard;

    auto* tempCard = decimalSliderRow(
        "温度 Temperature",
        "可选采样参数；只有勾选自定义才发送",
        m_settings->ai_temperature, 0.0, 2.0, 2, QString(), &m_aiTemperature, m_aiAdvanced);
    m_aiAdvanced->addRow(tempCard);

    auto* topPCard = decimalSliderRow(
        "Top P（核采样）",
        "通常与温度二选一调整；只有勾选自定义才发送",
        m_settings->ai_top_p, 0.0, 1.0, 2, QString(), &m_aiTopP, m_aiAdvanced);
    m_aiAdvanced->addRow(topPCard);

    auto* freqCard = decimalSliderRow(
        "频率惩罚 Frequency Penalty",
        "正值抑制重复用词；0 为不惩罚",
        m_settings->ai_frequency_penalty, -2.0, 2.0, 2, QString(),
        &m_aiFrequencyPenalty, m_aiAdvanced);
    m_aiAdvanced->addRow(freqCard);

    auto* presCard = decimalSliderRow(
        "存在惩罚 Presence Penalty",
        "正值鼓励引入新内容；0 为不惩罚",
        m_settings->ai_presence_penalty, -2.0, 2.0, 2, QString(),
        &m_aiPresencePenalty, m_aiAdvanced);
    m_aiAdvanced->addRow(presCard);

    auto* seedCard = new SettingsCard("随机种子 Seed",
                                      "勾选自定义才发送；0 也可作为显式种子",
                                      m_aiAdvanced);
    m_aiSeedEdit = new ElaLineEdit(seedCard);
    m_aiSeedEdit->setText(QString::number(m_settings->ai_seed));
    m_aiSeedEdit->setPlaceholderText("整数种子");
    m_aiSeedEdit->setFixedWidth(120);
    seedCard->addControl(m_aiSeedEdit);
    m_aiAdvanced->addRow(seedCard);

    auto* stopPanel = new PanelCard("停止序列",
                                    "留空不发送；每行一项，数量限制由当前模型决定", m_aiAdvanced);
    m_aiStopEdit = new ElaPlainTextEdit(stopPanel);
    m_aiStopEdit->setObjectName(QStringLiteral("aiStopEdit"));
    m_aiStopEdit->setPlainText(m_settings->ai_stop);
    m_aiStopEdit->setPlaceholderText("每行一个停止词，例如：\n###\n</answer>");
    m_aiStopEdit->setMinimumHeight(84);
    stopPanel->setContent(m_aiStopEdit);
    m_aiAdvanced->addRow(stopPanel);
    m_aiStopRows = {stopPanel};

    auto* streamUsageCard = toggleRow(
        "流式请求附带用量统计",
        "在流式响应末尾返回 token 用量（stream_options.include_usage）",
        m_settings->ai_stream_usage, &m_aiStreamUsage, m_aiAdvanced);
    if (m_aiStreamUsage) m_aiStreamUsage->setObjectName(QStringLiteral("aiStreamUsageToggle"));
    m_aiAdvanced->addRow(streamUsageCard);
    m_aiStreamUsageRows = {streamUsageCard};

    m_aiTemperatureRows = {tempCard, topPCard};
    m_aiPenaltyRows = {freqCard, presCard};
    m_aiSeedRows = {seedCard};

    for (const auto& entry : QList<QPair<QString, QWidget*>>{
            {"max_tokens", maxTokensCard}, {"temperature", tempCard},
            {"top_p", topPCard}, {"frequency_penalty", freqCard},
            {"presence_penalty", presCard}, {"seed", seedCard}}) {
        auto* check = new ElaCheckBox("自定义（发送）", entry.second);
        check->setObjectName("aiSend_" + entry.first);
        static_cast<SettingsCard*>(entry.second)->addControl(check);
        m_aiParameterChecks[entry.first] = check;
        QWidget* editor = entry.first == "max_tokens" ? static_cast<QWidget*>(m_aiMaxTokens)
            : entry.first == "temperature" ? static_cast<QWidget*>(m_aiTemperature)
            : entry.first == "top_p" ? static_cast<QWidget*>(m_aiTopP)
            : entry.first == "frequency_penalty" ? static_cast<QWidget*>(m_aiFrequencyPenalty)
            : entry.first == "presence_penalty" ? static_cast<QWidget*>(m_aiPresencePenalty)
            : static_cast<QWidget*>(m_aiSeedEdit);
        editor->setEnabled(false);
        connect(check, &QCheckBox::toggled, editor, &QWidget::setEnabled);
    }
    auto* outputFieldCard = new SettingsCard("输出长度字段", "预设只建议字段形式；上限取决于实际模型", m_aiAdvanced);
    m_aiOutputField = new ElaComboBox(outputFieldCard);
    m_aiOutputField->addItem("自动（上游声明 → 预设）", "auto");
    m_aiOutputField->addItem("max_tokens", "max_tokens");
    m_aiOutputField->addItem("max_completion_tokens", "max_completion_tokens");
    outputFieldCard->addControl(m_aiOutputField);
    m_aiAdvanced->addRow(outputFieldCard);
    auto* thinkingFormatCard = new SettingsCard("思考开关字段", "其他形式请留思考模式为自动，在下方填写 JSON", m_aiAdvanced);
    m_aiThinkingFormat = new ElaComboBox(thinkingFormatCard);
    m_aiThinkingFormat->addItem("自动（上游声明 → 预设）", "auto");
    m_aiThinkingFormat->addItem("thinking.type", "thinking");
    m_aiThinkingFormat->addItem("enable_thinking", "enable_thinking");
    thinkingFormatCard->addControl(m_aiThinkingFormat);
    m_aiAdvanced->addRow(thinkingFormatCard);

    m_aiEffortCombo->setEditable(true);
    for (const QString& value : {"auto", "none", "minimal", "low", "medium", "high", "xhigh", "max"})
        m_aiEffortCombo->addItem(value, value);
    m_aiEffortCombo->setToolTip("auto 表示不发送。其他值为常见示例，不代表当前模型支持；可直接输入。");
    auto* extraCard = new PanelCard("自定义请求参数（当前模型）",
        "JSON 对象覆盖上述字段；嵌套对象整体替换；顶层 null 删除字段。不能覆盖 model/messages/stream。不要填写密钥。",
        m_aiAdvanced);
    m_aiExtraBody = new ElaPlainTextEdit(extraCard);
    m_aiExtraBody->setObjectName("aiExtraBody");
    m_aiExtraBody->setPlaceholderText("{\n  \"reasoning_effort\": \"high\"\n}");
    m_aiExtraBody->setMinimumHeight(130);
    extraCard->setContent(m_aiExtraBody);
    m_aiAdvanced->addRow(extraCard);
    auto* previewCard = new PanelCard("请求参数预览",
        "点击刷新查看实际字段。消息为示例，不含 API Key；预览不发送网络请求。",
        m_aiAdvanced);
    auto* previewHost = new QWidget(previewCard);
    auto* previewLayout = new QVBoxLayout(previewHost);
    auto* previewButton = new ElaPushButton("刷新请求预览", previewHost);
    m_aiRequestPreview = new ElaPlainTextEdit(previewHost);
    m_aiRequestPreview->setReadOnly(true);
    m_aiRequestPreview->setMinimumHeight(160);
    previewLayout->addWidget(previewButton);
    previewLayout->addWidget(m_aiRequestPreview);
    previewCard->setContent(previewHost);
    m_aiAdvanced->addRow(previewCard);
    auto* metadataCard = new PanelCard("上游模型资料（缓存）",
        "获取模型与能力时刷新，失败保留缓存。仅展示接口实际返回的内容；资料可能过期或不完整。",
        m_aiAdvanced);
    m_aiMetadataPreview = new ElaPlainTextEdit(metadataCard);
    m_aiMetadataPreview->setObjectName("aiMetadataPreview");
    m_aiMetadataPreview->setReadOnly(true);
    m_aiMetadataPreview->setMinimumHeight(150);
    metadataCard->setContent(m_aiMetadataPreview);
    m_aiAdvanced->addRow(metadataCard);
    connect(previewButton, &QPushButton::clicked, this, &SettingsView::previewAiRequest);
    for (auto* slider : {m_aiTemperature, m_aiTopP, m_aiFrequencyPenalty, m_aiPresencePenalty})
        connect(slider, &QSlider::valueChanged, m_aiRequestPreview, &QPlainTextEdit::clear);
    for (auto* combo : {m_aiThinkingCombo, m_aiDoSampleCombo, m_aiEffortCombo, m_aiThinkingFormat, m_aiOutputField})
        connect(combo, &QComboBox::currentTextChanged, m_aiRequestPreview, &QPlainTextEdit::clear);
    for (auto* check : m_aiParameterChecks)
        connect(check, &QCheckBox::toggled, m_aiRequestPreview, &QPlainTextEdit::clear);
    connect(m_aiExtraBody, &QPlainTextEdit::textChanged, m_aiRequestPreview, &QPlainTextEdit::clear);
    connect(m_aiStopEdit, &QPlainTextEdit::textChanged, m_aiRequestPreview, &QPlainTextEdit::clear);
    connect(m_aiSeedEdit, &QLineEdit::textChanged, m_aiRequestPreview, &QPlainTextEdit::clear);
    connect(m_aiMaxTokens, &QSpinBox::valueChanged, m_aiRequestPreview, &QPlainTextEdit::clear);
    connect(m_aiStreamUsage, &ElaToggleSwitch::toggled, m_aiRequestPreview, &QPlainTextEdit::clear);
    connect(m_aiModelCombo, &QComboBox::currentTextChanged, this, [this] { switchAiProfile(); });
    connect(m_aiApiBaseEdit, &QLineEdit::editingFinished, this, [this] { ++m_fetchGen; switchAiProfile(); });
    m_aiProfiles = QJsonDocument::fromJson(m_settings->ai_model_profiles.toUtf8()).object();
    m_aiMetadata = QJsonDocument::fromJson(m_settings->ai_model_metadata.toUtf8()).object();
    applyAiProfile(m_settings->toDict());
    switchAiProfile();
    appendPageReset(page, "ai_settings", "AI 设置页面");
    refreshAiPage();
    return page;
}

// ============================================================================
// 页面：翻译
// ============================================================================

QWidget* SettingsView::buildTranslateTab()
{
    auto* page = new SettingsPage("翻译", "默认目标语言、翻译行为与提示词模板。");

    page->addSection("默认行为");
    auto* langCard = new SettingsCard("默认目标语言", "截图翻译窗口打开时预选的语言", page);
    m_aiTargetLangCombo = new ElaComboBox(langCard);
    m_aiTargetLangCombo->setEditable(true);
    m_aiTargetLangCombo->addItems({
        "简体中文", "繁体中文（台湾）", "繁体中文（香港）",
        "英语", "日语", "韩语", "法语", "德语",
        "西班牙语", "葡萄牙语", "俄语", "阿拉伯语",
        "泰语", "越南语",
    });
    const int tli = m_aiTargetLangCombo->findText(m_settings->ai_target_lang);
    if (tli >= 0) m_aiTargetLangCombo->setCurrentIndex(tli);
    else m_aiTargetLangCombo->setCurrentText(m_settings->ai_target_lang);
    m_aiTargetLangCombo->setMinimumWidth(190);
    langCard->addControl(m_aiTargetLangCombo);
    page->addBlock(langCard);

    page->addBlock(toggleRow("翻译前先确认目标语言",
                             "开启后截图翻译窗口不会自动开始，需要手动点击「开始翻译」",
                             m_settings->ai_confirm_before_translate,
                             &m_aiConfirmBefore, page));
    page->addBlock(toggleRow("流式显示 AI 思考过程",
                             "实时推送模型的思考内容到「AI 思考」页签",
                             m_settings->ai_stream_thinking,
                             &m_aiStreamThinking, page));
    connect(m_aiStreamThinking, &ElaToggleSwitch::toggled, m_aiRequestPreview, &QPlainTextEdit::clear);

    page->addSection("提示词", "占位符会在翻译时被替换，可按需微调语气与术语偏好。");
    {
        auto* panel = new PanelCard("截图翻译提示词",
                                    "占位符：{target_lang} 目标语言", page);
        m_aiPromptEdit = new ElaPlainTextEdit(panel);
        m_aiPromptEdit->setPlainText(m_settings->ai_translation_prompt);
        m_aiPromptEdit->setPlaceholderText(QString::fromUtf8(DEFAULT_TRANSLATION_PROMPT));
        m_aiPromptEdit->setMinimumHeight(140);
        panel->setContent(m_aiPromptEdit);
        page->addBlock(panel, Dim::SectionGap - Dim::RowGap);
    }
    {
        auto* panel = new PanelCard(
            "文本翻译提示词",
            "占位符：{source_text} 原文、{target_lang} 目标语言、{scenario} 翻译场景", page);
        m_textPromptEdit = new ElaPlainTextEdit(panel);
        m_textPromptEdit->setPlainText(m_settings->text_translation_prompt);
        m_textPromptEdit->setPlaceholderText(QString::fromUtf8(DEFAULT_TEXT_TRANSLATION_PROMPT));
        m_textPromptEdit->setMinimumHeight(140);
        panel->setContent(m_textPromptEdit);
        page->addBlock(panel, Dim::SectionGap - Dim::RowGap);
    }

    appendPageReset(page, "translate", "翻译页面");
    return page;
}

// ============================================================================
// 页面：常规
// ============================================================================

QWidget* SettingsView::buildGeneralTab()
{
    auto* page = new SettingsPage("常规", "外观主题、启动方式、关闭行为、通知与日志级别。");

    page->addSection("外观");
    auto* themeCard = new SettingsCard("主题", "默认跟随系统的深浅色设置", page);
    m_themeCombo = new ElaComboBox(themeCard);
    m_themeCombo->addItem("跟随系统", QStringLiteral("system"));
    m_themeCombo->addItem("浅色",     QStringLiteral("light"));
    m_themeCombo->addItem("深色",     QStringLiteral("dark"));
    const int themeIdx = m_themeCombo->findData(m_settings->theme_mode);
    if (themeIdx >= 0) m_themeCombo->setCurrentIndex(themeIdx);
    m_themeCombo->setMinimumWidth(190);
    themeCard->addControl(m_themeCombo);
    page->addBlock(themeCard);
    // 主题即时生效（便于边看边调），实际取值随「保存设置」落盘。
    connect(m_themeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int) {
                applyThemeMode(themeModeFromString(
                    m_themeCombo->currentData().toString()));
            });

    page->addSection("启动与关闭");
    page->addBlock(comboRowData("启动方式",
                            "选择启动时打开工具箱或在后台运行",
                            {{"打开工具箱首页", QStringLiteral("translate")},
                             {"后台运行（系统托盘）", QStringLiteral("screenshot")}},
                            m_settings->app_mode == "translate" ? 0 : 1,
                            &m_appModeCombo, page));

    auto* closeCard = new SettingsCard("关闭主窗口时",
                                       "选择「每次询问」会在关闭时确认", page);
    m_closeToTrayCombo = new ElaComboBox(closeCard);
    m_closeToTrayCombo->addItems({"每次询问", "后台运行（最小化到托盘）", "退出程序"});
    m_closeToTrayCombo->setMinimumWidth(190);
    closeCard->addControl(m_closeToTrayCombo);
    page->addBlock(closeCard);

    page->addSection("通知", "关闭后不再弹出系统通知（日志不受影响）");
    page->addBlock(toggleRow("复制到剪贴板", {}, m_settings->notify_copy,
                             &m_notifyCopy, page));
    page->addBlock(toggleRow("截图保存成功", {}, m_settings->notify_save_success,
                             &m_notifySaveSuccess, page));
    page->addBlock(toggleRow("截图保存失败", {}, m_settings->notify_save_fail,
                             &m_notifySaveFail, page));
    page->addBlock(toggleRow("屏幕抓取失败", {}, m_settings->notify_capture_fail,
                             &m_notifyCaptureFail, page));
    page->addBlock(toggleRow("快捷键注册失败", {}, m_settings->notify_hotkey_fail,
                             &m_notifyHotkeyFail, page));
    page->addBlock(toggleRow("AI 翻译成功", {}, m_settings->notify_translate_success,
                             &m_notifyTransSuccess, page));
    page->addBlock(toggleRow("AI 翻译失败", {}, m_settings->notify_translate_fail,
                             &m_notifyTransFail, page));
    page->addBlock(toggleRow("OCR 识别失败", {}, m_settings->notify_ocr_fail,
                             &m_notifyOcrFail, page));
    page->addBlock(toggleRow("语言包下载结果", {}, m_settings->notify_lang_download,
                             &m_notifyLangDownload, page));

    page->addSection("日志级别", "勾选即启用，保存后立即生效");
    page->addBlock(toggleRow("DEBUG 调试信息", "包含每次操作细节，日志量最大",
                             m_settings->log_debug_enabled, &m_logDebugEnabled, page));
    page->addBlock(toggleRow("INFO 一般信息", "关键流程与结果",
                             m_settings->log_info_enabled, &m_logInfoEnabled, page));
    page->addBlock(toggleRow("WARNING 警告", "可恢复的异常情况",
                             m_settings->log_warning_enabled, &m_logWarningEnabled, page));
    page->addBlock(toggleRow("ERROR 错误", "失败与异常，建议始终开启",
                             m_settings->log_error_enabled, &m_logErrorEnabled, page));

    appendPageReset(page, "general", "常规页面");
    return page;
}

// ============================================================================
// 联动：从属设置不适用时隐藏卡片，保留值供再次启用。
//
// 每个 refresh* 都从控件当前状态完整重算一遍，而不是「按事件增量改状态」——
// 后者在多个开关互相影响时会因执行顺序不同留下错误状态。
// 空指针防护保证页面构建过程中的提前调用安全。
// ============================================================================

namespace {
/** @brief 同步从属卡片的可见性，不修改控件值或持久化设置。 */
void setRowsVisible(const QList<QWidget*>& rows, bool on)
{
    for (QWidget* row : rows) {
        if (row) row->setVisible(on);
    }
}
} // namespace

void SettingsView::refreshScreenshotPage()
{
    if (!m_crosshairEnabled || !m_crosshairInvert) return;
    const bool master = m_crosshairEnabled->getIsToggled();
    const bool invert = m_crosshairInvert->getIsToggled();
    setRowsVisible(m_screenshotCrosshairRows, master);
    // 反色仍使用线宽，但不使用自定义颜色和不透明度。
    setRowsVisible(m_screenshotCrosshairColorRows, master && !invert);
}

void SettingsView::refreshMagnifierPage()
{
    if (!m_magnifierEnabled || !m_magnifierWheelZoom || !m_gridEnabled
        || !m_zoomLabelEnabled || !m_magCrossInvert || !m_edgeModeCombo) {
        return;
    }
    const bool master = m_magnifierEnabled->getIsToggled();
    setRowsVisible(m_magnifierRows, master);

    // 倍率行互斥：滚轮调节开启时用「默认倍率」，否则用「固定倍率」
    const bool wheel = m_magnifierWheelZoom->getIsToggled();
    setRowsVisible(m_wheelZoomRows, master && wheel);
    setRowsVisible(m_fixedZoomRows, master && !wheel);

    // 子分组的从属行
    setRowsVisible(m_gridRows, master && m_gridEnabled->getIsToggled());
    setRowsVisible(m_zoomLabelRows, master && m_zoomLabelEnabled->getIsToggled());
    setRowsVisible(m_magCrossColorRows, master && !m_magCrossInvert->getIsToggled());

    // 边缘填充色仅在选择「填充」策略时才有意义
    const bool pad = (m_edgeModeCombo->currentIndex() == 1);
    for (QWidget* row : m_edgeColorRows) {
        if (row) row->setVisible(master && pad);
    }
}

void SettingsView::refreshLabelsPage()
{
    if (m_coordLabelEnabled)
        setRowsVisible(m_coordLabelRows, m_coordLabelEnabled->getIsToggled());
    if (m_colorLabelEnabled)
        setRowsVisible(m_colorLabelRows, m_colorLabelEnabled->getIsToggled());
}

// ============================================================================
// 浏览辅助
// ============================================================================

void SettingsView::browseSaveDir()
{
    QString d = QFileDialog::getExistingDirectory(this, "选择图片保存目录", m_dirEdit->text());
    if (!d.isEmpty()) m_dirEdit->setText(d);
}

void SettingsView::browseConfigDir()
{
    QString d = QFileDialog::getExistingDirectory(this, "选择配置保存目录", m_configEdit->text());
    if (!d.isEmpty()) m_configEdit->setText(d);
}

void SettingsView::resetConfigDir()
{
    m_configEdit->clear();
    m_configEdit->setPlaceholderText("（程序所在目录）");
}

void SettingsView::browseDir(QLineEdit* edit, const QString& title)
{
    QString d = QFileDialog::getExistingDirectory(this, title, edit->text());
    if (!d.isEmpty()) edit->setText(d);
}

// ============================================================================
// AI 服务
// ============================================================================

void SettingsView::onProviderChanged(int)
{
    const QSignalBlocker modelBlocker(m_aiModelCombo);
    ++m_fetchGen;
    QString newPid = m_aiProviderCombo->currentData().toString();
    auto cfg = getProviderConfig(newPid);
    if (!m_currentProvider.isEmpty() && m_currentProvider != newPid) {
        m_providerKeys[m_currentProvider] = m_aiApiKeyEdit->text().trimmed();
        m_providerBases[m_currentProvider] = m_aiApiBaseEdit->text().trimmed();
        m_providerModels[m_currentProvider] = m_aiModelCombo->currentText().trimmed();
    }
    if (newPid != m_currentProvider) {
        m_aiApiBaseEdit->setText(m_providerBases.value(newPid, cfg.apiBase));
        m_aiApiKeyEdit->setText(m_providerKeys.value(newPid));
        m_aiModelCombo->clear();
        if (newPid == "custom") {
            for (const QString& value : m_aiCustomModelsEdit->text().split(QRegularExpression("[,，;；]"), Qt::SkipEmptyParts)) {
                const QString model = value.trimmed();
                if (!model.isEmpty() && m_aiModelCombo->findText(model) < 0) m_aiModelCombo->addItem(model);
            }
        }
        m_aiModelCombo->setCurrentText(m_providerModels.value(newPid, cfg.defaultModel));
    }
    m_aiCustomModelsCard->setVisible(newPid == "custom");
    m_currentProvider = newPid;
    switchAiProfile();
}

void SettingsView::refreshAiPage()
{
    if (!m_aiParamHint || !m_aiModelCombo) return;
    QString hint = "接口格式：OpenAI Chat Completions。自动参数不发送；显式值不按服务商静默裁剪。";
    const auto endpoint = providerEndpoint(m_aiApiBaseEdit->text(), "models").toString();
    const auto cache = m_aiMetadata.value(endpoint).toObject();
    const auto info = ModelCapabilities::cachedModel(m_aiMetadata, m_aiApiBaseEdit->text(),
                                                    m_aiModelCombo->currentText());
    const auto caps = ModelCapabilities::suggestions(info, m_aiApiBaseEdit->text(), m_aiModelCombo->currentText());
    const bool upstreamEfforts = caps.effortSource == ModelCapabilities::Source::Upstream;
    const bool upstreamThinkingModes = caps.thinkingSource == ModelCapabilities::Source::Upstream;
    const bool documentedEfforts = caps.effortSource == ModelCapabilities::Source::Documentation;
    const bool documentedThinkingModes = caps.thinkingSource == ModelCapabilities::Source::Documentation;
    if (m_aiMetadataPreview) m_aiMetadataPreview->setPlainText(info.isEmpty()
        ? QStringLiteral("尚未获取该端点/模型的资料，可点击“获取模型与能力”。")
        : QStringLiteral("获取时间：%1\n来源：%2\n\n%3").arg(
            cache.value("fetched_at").toString(), endpoint,
            QString::fromUtf8(QJsonDocument(info).toJson(QJsonDocument::Indented))));
    auto number = [](qint64 value) { return value > 0 ? QString::number(value) : QStringLiteral("未知"); };
    hint += QString("\n上游资料：上下文窗口 %1；输入上限 %2；输出上限 %3。")
        .arg(number(caps.contextWindow), number(caps.maxInputTokens), number(caps.maxOutputTokens));
    if (!info.isEmpty()) hint += " 获取时间：" + cache.value("fetched_at").toString();
    if (caps.parameters)
        hint += "\n上游声明的参数：" + (caps.parameters->isEmpty() ? QStringLiteral("空列表") : caps.parameters->join(", "));
    const QString effortSource = upstreamEfforts ? QStringLiteral("上游缓存中的档位声明")
        : (documentedEfforts ? QStringLiteral("GLM 模型作者文档候选（列表接口未返回档位）")
        : (caps.reasoning && !*caps.reasoning ? QStringLiteral("上游声明不支持推理")
                                            : QStringLiteral("通用候选（当前模型未声明档位）")));
    if (m_aiEffortCombo) {
        const QSignalBlocker blocker(m_aiEffortCombo);
        const QString selected = m_aiEffortCombo->currentText().trimmed();
        m_aiEffortCombo->clear();
        m_aiEffortCombo->addItem("auto", "auto");
        for (const auto& effort : caps.effortChoices())
            if (effort != "auto") m_aiEffortCombo->addItem(effort, effort);
        // Refresh suggestions without converting, discarding or activating a saved choice.
        if (!selected.isEmpty() && m_aiEffortCombo->findText(selected) < 0)
            m_aiEffortCombo->addItem(selected, selected);
        m_aiEffortCombo->setCurrentText(selected.isEmpty() ? QStringLiteral("auto") : selected);
        m_aiEffortCombo->setToolTip(effortSource + "。auto 不发送；其他值按 reasoning_effort 原样发送。"
            "目录中的内部档位 ID 不一定是 API 取值；不同字段或关闭语义请使用高级 JSON。"
            + (caps.defaultEffort.isEmpty() ? QString{} : (upstreamEfforts ? " 上游声明的默认档位：" : " 文档给出的默认档位：") + caps.defaultEffort));
        if (caps.efforts && selected != "auto" && !selected.isEmpty() && !caps.efforts->contains(selected))
            hint += upstreamEfforts ? "\n当前档位不在上游声明中，已保留你的设置；请确认该通道支持此值。"
                : "\n当前档位不在文档候选中，已保留你的设置；请确认该通道支持此值。";
    }
    if (m_aiThinkingCombo) {
        const QSignalBlocker blocker(m_aiThinkingCombo);
        const QString selected = m_aiThinkingCombo->currentData().toString();
        const QStringList modes = caps.thinkingModes.value_or(
            caps.reasoning && !*caps.reasoning ? QStringList{} : QStringList{"on", "off"});
        m_aiThinkingCombo->clear();
        m_aiThinkingCombo->addItem("自动（不发送）", "auto");
        for (const auto& mode : modes)
            m_aiThinkingCombo->addItem(mode == "on" ? "开启思考" : "关闭思考", mode);
        if (!selected.isEmpty() && m_aiThinkingCombo->findData(selected) < 0)
            m_aiThinkingCombo->addItem(selected == "on" ? "开启思考（已保存）" : "关闭思考（已保存）", selected);
        m_aiThinkingCombo->setCurrentIndex(qMax(0, m_aiThinkingCombo->findData(selected)));
        m_aiThinkingCombo->setToolTip(upstreamThinkingModes ? "选项来自上游明确声明的开关形式。"
            : documentedThinkingModes ? "选项来自 GLM 模型作者文档，非模型列表接口声明；此模型不能关闭思考。"
            : "上游未声明可用的开关形式。通用选项是否可用取决于模型；可在高级设置指定字段。易混淆：支持思考不代表允许关闭思考。");
        if (documentedThinkingModes && selected == "off")
            hint += "\n该 GLM 模型文档不支持关闭思考；已保留你的旧设置，发送前请改为自动或开启。";
    }
    hint += "\n推理档位来源：" + effortSource;
    if (!caps.defaultEffort.isEmpty()) hint += (upstreamEfforts ? "；声明的默认值：" : "；文档默认值：")
        + caps.defaultEffort + "（不会自动发送）";
    if (caps.reasoning && !*caps.reasoning)
        hint += "\n上游声明不支持推理；如仍需发送，请手动设置并确认该通道的实际支持情况。";
    if (caps.maxOutputTokens > 0 && m_aiParameterChecks.value("max_tokens")
        && m_aiParameterChecks.value("max_tokens")->isChecked() && m_aiMaxTokens->value() > caps.maxOutputTokens)
        hint += "\n当前输出预算超过缓存声明的上限，已保留你的设置；请核对上限或刷新模型资料。";
    hint += "\n资料仅供参考；支持推理不代表允许关闭。上下文容量、输入上限和输出预算分别处理。";
    m_aiParamHint->setText(hint);
}

QJsonObject SettingsView::captureAiProfile() const
{
    QStringList enabled;
    for (auto it = m_aiParameterChecks.begin(); it != m_aiParameterChecks.end(); ++it)
        if (it.value()->isChecked()) enabled.append(it.key());
    return {
        {"ai_enabled_parameters", enabled.join(',')},
        {"ai_temperature", m_aiTemperature->value() / 100.0},
        {"ai_top_p", m_aiTopP->value() / 100.0},
        {"ai_frequency_penalty", m_aiFrequencyPenalty->value() / 100.0},
        {"ai_presence_penalty", m_aiPresencePenalty->value() / 100.0},
        {"ai_max_tokens", m_aiMaxTokens->value()},
        {"ai_seed", m_aiSeedEdit->text()},
        {"ai_do_sample", m_aiDoSampleCombo->currentData().toString()},
        {"ai_thinking", m_aiThinkingCombo->currentData().toString()},
        {"ai_reasoning_effort", m_aiEffortCombo->currentText().trimmed()},
        {"ai_thinking_format", m_aiThinkingFormat->currentData().toString()},
        {"ai_max_tokens_field", m_aiOutputField->currentData().toString()},
        {"ai_stop", m_aiStopEdit->toPlainText()},
        {"ai_stream_usage", m_aiStreamUsage->getIsToggled()},
        {"ai_extra_body", m_aiExtraBody->toPlainText()}
    };
}

void SettingsView::applyAiProfile(const QJsonObject& profile)
{
    const Settings s(profile);
    const auto enabled = s.ai_enabled_parameters.split(',');
    for (auto it = m_aiParameterChecks.begin(); it != m_aiParameterChecks.end(); ++it)
        it.value()->setChecked(enabled.contains(it.key()));
    m_aiTemperature->setValue(qRound(s.ai_temperature * 100));
    m_aiTopP->setValue(qRound(s.ai_top_p * 100));
    m_aiFrequencyPenalty->setValue(qRound(s.ai_frequency_penalty * 100));
    m_aiPresencePenalty->setValue(qRound(s.ai_presence_penalty * 100));
    m_aiMaxTokens->setValue(s.ai_max_tokens);
    m_aiSeedEdit->setText(profile.value("ai_seed").isString()
        ? profile.value("ai_seed").toString() : QString::number(s.ai_seed));
    m_aiDoSampleCombo->setCurrentIndex(qMax(0, m_aiDoSampleCombo->findData(s.ai_do_sample)));
    if (m_aiThinkingCombo->findData(s.ai_thinking) < 0)
        m_aiThinkingCombo->addItem(s.ai_thinking == "on" ? "开启思考" : "关闭思考", s.ai_thinking);
    m_aiThinkingCombo->setCurrentIndex(qMax(0, m_aiThinkingCombo->findData(s.ai_thinking)));
    m_aiEffortCombo->setCurrentText(s.ai_reasoning_effort);
    m_aiThinkingFormat->setCurrentIndex(qMax(0, m_aiThinkingFormat->findData(s.ai_thinking_format)));
    m_aiOutputField->setCurrentIndex(qMax(0, m_aiOutputField->findData(s.ai_max_tokens_field)));
    m_aiStopEdit->setPlainText(s.ai_stop);
    setToggle(m_aiStreamUsage, s.ai_stream_usage);
    m_aiExtraBody->setPlainText(s.ai_extra_body);
    m_aiRequestPreview->clear();
}

void SettingsView::switchAiProfile()
{
    if (m_loadingAiProfile || !m_aiExtraBody) return;
    // JSON 数组编码避免地址/模型 ID 中分隔符引起键冲突；末尾斜杠统一。
    const QString key = aiModelProfileKey(m_aiApiBaseEdit->text(), m_aiModelCombo->currentText());
    if (key != m_aiProfileKey) {
        if (!m_aiProfileKey.isEmpty()) m_aiProfiles[m_aiProfileKey] = captureAiProfile();
        // 仅首次升级且无模型配置时保留旧配置；未知模型使用自动参数。
        if (!m_aiProfileKey.isEmpty() || !m_aiProfiles.isEmpty())
            applyAiProfile(m_aiProfiles.value(key).toObject());
        m_aiProfileKey = key;
    }
    refreshAiPage();
}

static ApiClient::ChatParams previewParams(const QJsonObject& profile)
{
    return AiRequestOptions::fromSettings(Settings(profile)).toChatParams({});
}

void SettingsView::previewAiRequest()
{
    switchAiProfile();
    auto p = previewParams(captureAiProfile());
    p.provider = m_aiProviderCombo->currentData().toString();
    p.model = m_aiModelCombo->currentText().trimmed();
    p.modelMetadata = ModelCapabilities::cachedModel(m_aiMetadata, m_aiApiBaseEdit->text(), p.model);
    p.messages = QJsonArray{QJsonObject{{"role", "user"}, {"content", "（示例消息）"}}};
    QString error;
    const auto body = ApiClient::buildRequestBody(p, m_aiStreamThinking->getIsToggled(), &error);
    m_aiRequestPreview->setPlainText(error.isEmpty()
        ? QString::fromUtf8(QJsonDocument(body).toJson(QJsonDocument::Indented)) : error);
}

void SettingsView::onFetchModels()
{
    QString apiKey = m_aiApiKeyEdit->text().trimmed();
    QString apiBase = m_aiApiBaseEdit->text().trimmed();
    m_aiErrorRow->setVisible(false);
    if (apiKey.isEmpty() && providerNeedsApiKey(m_aiProviderCombo->currentData().toString())) {
        m_aiErrorLabel->setText("请先填写 API Key");
        m_aiErrorRow->setVisible(true);
        m_aiApiKeyEdit->setFocus();
        return;
    }
    if (providerEndpoint(apiBase, QStringLiteral("models")).isEmpty()) {
        m_aiErrorLabel->setText("请填写有效的 http:// 或 https:// 接口根地址");
        m_aiErrorRow->setVisible(true);
        m_aiApiBaseEdit->setFocus();
        return;
    }

    // 异步获取；回调校验请求代次，避免切换服务后旧结果覆盖新配置。
    m_fetchModelsBtn->setEnabled(false);
    m_fetchModelsBtn->setText("获取中…");
    const int gen = ++m_fetchGen;

    if (!m_aiClient) m_aiClient = std::make_unique<ApiClient>();
    m_aiClient->listModelsAsync(apiKey, apiBase, 10,
        [this, gen, apiBase](const ApiClient::ModelsResult& result) {
            m_fetchModelsBtn->setEnabled(true);
            m_fetchModelsBtn->setText("获取模型与能力");

            // 过期结果丢弃：期间用户已恢复默认/再次发起获取
            if (gen != m_fetchGen || apiBase != m_aiApiBaseEdit->text().trimmed()) {
                SNAP_LOG_DEBUG("SettingsView: 丢弃过期的模型列表结果（代次不匹配）");
                return;
            }
            // 视图已不可见（切到别的页面/窗口隐藏）：不再回填隐藏控件
            if (!isVisible()) {
                SNAP_LOG_DEBUG("SettingsView: 视图不可见，丢弃模型列表结果");
                return;
            }

            if (result.errorCode == 0 && !result.modelIds.isEmpty()) {
                m_aiMetadata[providerEndpoint(apiBase, "models").toString()] = QJsonObject{
                    {"models", result.metadata},
                    {"fetched_at", QDateTime::currentDateTimeUtc().toString(Qt::ISODate)}};
                m_aiRequestPreview->clear();

                const QSignalBlocker blocker(m_aiModelCombo);
                const QString selected = m_aiModelCombo->currentText();
                m_aiModelCombo->clear();
                m_aiModelCombo->addItems(result.modelIds);
                if (!selected.isEmpty()) m_aiModelCombo->setCurrentText(selected);
                switchAiProfile();
            } else if (result.errorCode == -2) {
                m_aiErrorLabel->setText("请求超时。继续使用已有缓存；没有缓存时使用通用候选，已选参数保持不变。");
                m_aiErrorRow->setVisible(true);
            } else {
                QString hint = result.errorMsg.contains("鉴权")
                                   ? "API Key 鉴权失败，请确认 Key 是否正确"
                                   : "获取模型列表失败，可手动输入模型名称";
                m_aiErrorLabel->setText(hint + "\n" + result.errorMsg
                    + "\n继续使用已有缓存；没有缓存时使用通用候选，已选参数保持不变。");
                m_aiErrorRow->setVisible(true);
            }
        });
}

// ============================================================================
// 数据流：控件 ↔ 配置
// ============================================================================

bool SettingsView::saveToSettings()
{
    const Settings previous = *m_settings;
    const QString formatKey = m_colorFormatKeyMode->currentIndex() == 0 ? QStringLiteral("Shift")
        : m_colorFormatKeyEdit->keySequence().toString(QKeySequence::PortableText);
    if (formatKey.isEmpty() || formatKey == "Esc"
        || (formatKey != "Shift" && QKeySequence(formatKey) == m_copyColorKeyEdit->keySequence())) {
        if (m_notifyManager) m_notifyManager->warn("设置", "请选择颜色格式切换快捷键，且不能与复制颜色或 Esc 取消键相同。", this);
        return false;
    }
    if (m_hotkeyEdit->keySequence().isEmpty()) {
        if (m_notifyManager) m_notifyManager->warn("设置", "请先录入截图快捷键。", this);
        return false;
    }
    if (m_dirEdit->text().trimmed().isEmpty()) {
        if (m_notifyManager) m_notifyManager->warn("设置", "保存目录不能为空。", this);
        return false;
    }
    const QString aiBase = m_aiApiBaseEdit->text().trimmed();
    if (!aiBase.isEmpty() && providerEndpoint(aiBase, QStringLiteral("models")).isEmpty()) {
        if (m_notifyManager) m_notifyManager->warn("AI 服务", "接口根地址无效，请填写 http:// 或 https:// 地址。", this);
        m_aiApiBaseEdit->setFocus();
        return false;
    }

    switchAiProfile();
    const auto aiProfile = captureAiProfile();
    auto validationParams = previewParams(aiProfile);
    validationParams.provider = m_aiProviderCombo->currentData().toString();
    QString parameterError;
    ApiClient::buildRequestBody(validationParams, true, &parameterError);
    bool validSeed = false;
    m_aiSeedEdit->text().trimmed().toInt(&validSeed);
    if (m_aiParameterChecks.value("seed")->isChecked() && !validSeed)
        parameterError = "随机种子必须为整数";
    if (!parameterError.isEmpty()) {
        if (m_notifyManager) m_notifyManager->warn("AI 参数", parameterError, this);
        return false;
    }

    m_settings->hotkey = m_hotkeyEdit->keySequence().toString(QKeySequence::PortableText);
    m_settings->toolbar_opacity = m_toolbarOpacity->value();
    m_settings->crosshair_color = m_crosshairColor->color();
    m_settings->crosshair_alpha = m_crosshairAlpha->value();
    m_settings->crosshair_thickness = m_crosshairThickness->value();
    m_settings->crosshair_invert = m_crosshairInvert->getIsToggled();
    m_settings->crosshair_enabled = m_crosshairEnabled->getIsToggled();
    m_settings->cursor_enabled = m_cursorEnabled->getIsToggled();
    m_settings->cursor_keyboard_enabled = m_cursorKeyboardEnabled->getIsToggled();
    m_settings->cursor_keyboard_mode = QStringList{"arrows", "wasd", "both"}.at(m_cursorKeyboardMode->currentIndex());

    m_settings->magnifier_enabled = m_magnifierEnabled->getIsToggled();
    m_settings->magnifier_zoom = m_magnifierZoom->value() / 10.0;
    m_settings->magnifier_fixed_zoom = m_magnifierFixedZoom->value() / 10.0;
    m_settings->magnifier_wheel_zoom = m_magnifierWheelZoom->getIsToggled();
    m_settings->magnifier_size = m_magnifierSize->value();
    m_settings->magnifier_cross_color = m_magCrossColor->color();
    m_settings->magnifier_cross_alpha = m_magCrossAlpha->value();
    m_settings->magnifier_cross_thickness = m_magCrossThickness->value();
    m_settings->magnifier_cross_invert = m_magCrossInvert->getIsToggled();
    m_settings->grid_enabled = m_gridEnabled->getIsToggled();
    m_settings->grid_color = m_gridColor->color();
    m_settings->grid_alpha = m_gridAlpha->value();
    m_settings->edge_mode = m_edgeModeCombo->currentIndex() == 0 ? "crop" : "pad";
    m_settings->edge_color = m_edgeColor->color();
    m_settings->zoom_label_enabled = m_zoomLabelEnabled->getIsToggled();
    m_settings->zoom_label_text_color = m_zoomLabelTextColor->color();
    m_settings->zoom_label_text_alpha = m_zoomLabelTextAlpha->value();
    m_settings->zoom_label_bg_color = m_zoomLabelBgColor->color();
    m_settings->zoom_label_bg_alpha = m_zoomLabelBgAlpha->value();

    m_settings->coord_label_enabled = m_coordLabelEnabled->getIsToggled();
    m_settings->coord_label_text_color = m_coordTextColor->color();
    m_settings->coord_label_text_alpha = m_coordTextAlpha->value();
    m_settings->coord_label_bg_color = m_coordBgColor->color();
    m_settings->coord_label_bg_alpha = m_coordBgAlpha->value();
    m_settings->color_label_enabled = m_colorLabelEnabled->getIsToggled();
    m_settings->color_label_text_color = m_colorTextColor->color();
    m_settings->color_label_text_alpha = m_colorTextAlpha->value();
    m_settings->color_label_bg_color = m_colorBgColor->color();
    m_settings->color_label_bg_alpha = m_colorBgAlpha->value();
    m_settings->color_format = m_colorFormatCombo->currentIndex() == 0 ? "rgb" : "hex";
    m_settings->copy_color_key = m_copyColorKeyEdit->keySequence().toString(QKeySequence::PortableText);
    m_settings->color_format_key = formatKey;
    m_settings->color_swatch_enabled = m_colorSwatchEnabled->getIsToggled();
    m_settings->copy_hex_prefix = m_copyHexPrefix->getIsToggled();
    m_settings->copy_rgb_prefix = m_copyRgbPrefix->getIsToggled();

    m_settings->save_dir = m_dirEdit->text().trimmed();
    m_settings->save_format = m_formatCombo->currentText();
    m_settings->config_dir = m_configEdit->text().trimmed();

    m_settings->ai_ocr_langs = m_ocrLangList->getSelectedLangs();
    m_settings->ocr_tessdata_dir = m_ocrTessdataEdit->text().trimmed();

    m_settings->ai_provider = m_aiProviderCombo->currentData().toString();
    m_settings->ai_api_key = m_aiApiKeyEdit->text().trimmed();
    m_settings->ai_api_base = m_aiApiBaseEdit->text().trimmed();
    m_settings->ai_model = m_aiModelCombo->currentText().trimmed();
    m_settings->ai_custom_models = m_aiCustomModelsEdit->text().trimmed();
    m_settings->ai_timeout = m_aiTimeout->value();
    m_settings->ai_response_idle_timeout = m_aiResponseIdleTimeout->value();
    m_settings->ai_temperature = m_aiTemperature->value() / 100.0;
    m_settings->ai_max_tokens = m_aiMaxTokens->value();
    m_settings->ai_top_p = m_aiTopP->value() / 100.0;
    m_settings->ai_frequency_penalty = m_aiFrequencyPenalty->value() / 100.0;
    m_settings->ai_presence_penalty = m_aiPresencePenalty->value() / 100.0;
    m_settings->ai_seed = m_aiSeedEdit->text().trimmed().toInt();
    m_settings->ai_do_sample = m_aiDoSampleCombo->currentData().toString();
    m_settings->ai_thinking = m_aiThinkingCombo->currentData().toString();
    m_settings->ai_reasoning_effort = m_aiEffortCombo->currentText().trimmed();
    // 停止序列按行拆分、去空；不静默截断用户配置。
    m_settings->ai_stop = AiRequestOptions::joinStop(
        AiRequestOptions::parseStop(m_aiStopEdit->toPlainText()));
    m_settings->ai_stream_usage = m_aiStreamUsage->getIsToggled();
    m_settings->ai_enabled_parameters = aiProfile.value("ai_enabled_parameters").toString();
    m_settings->ai_extra_body = m_aiExtraBody->toPlainText();
    m_settings->ai_thinking_format = m_aiThinkingFormat->currentData().toString();
    m_settings->ai_max_tokens_field = m_aiOutputField->currentData().toString();
    m_aiProfiles[m_aiProfileKey] = aiProfile;
    m_settings->ai_model_profiles = QString::fromUtf8(QJsonDocument(m_aiProfiles).toJson(QJsonDocument::Compact));
    m_settings->ai_model_metadata = QString::fromUtf8(QJsonDocument(m_aiMetadata).toJson(QJsonDocument::Compact));

    m_settings->ai_target_lang = m_aiTargetLangCombo->currentText();
    m_settings->ai_confirm_before_translate = m_aiConfirmBefore->getIsToggled();
    m_settings->ai_stream_thinking = m_aiStreamThinking->getIsToggled();
    m_settings->ai_translation_prompt = m_aiPromptEdit->toPlainText().trimmed();
    m_settings->text_translation_prompt = m_textPromptEdit->toPlainText().trimmed();

    m_settings->theme_mode = m_themeCombo->currentData().toString();
    m_settings->app_mode = m_appModeCombo->currentData().toString();
    const int ctIdx = m_closeToTrayCombo->currentIndex();
    m_settings->close_to_tray = (ctIdx == 1) ? QVariant(true)
                                             : (ctIdx == 2) ? QVariant(false) : QVariant();

    m_settings->notify_copy = m_notifyCopy->getIsToggled();
    m_settings->notify_save_success = m_notifySaveSuccess->getIsToggled();
    m_settings->notify_save_fail = m_notifySaveFail->getIsToggled();
    m_settings->notify_capture_fail = m_notifyCaptureFail->getIsToggled();
    m_settings->notify_hotkey_fail = m_notifyHotkeyFail->getIsToggled();
    m_settings->notify_translate_success = m_notifyTransSuccess->getIsToggled();
    m_settings->notify_translate_fail = m_notifyTransFail->getIsToggled();
    m_settings->notify_ocr_fail = m_notifyOcrFail->getIsToggled();
    m_settings->notify_lang_download = m_notifyLangDownload->getIsToggled();
    m_settings->log_debug_enabled = m_logDebugEnabled->getIsToggled();
    m_settings->log_info_enabled = m_logInfoEnabled->getIsToggled();
    m_settings->log_warning_enabled = m_logWarningEnabled->getIsToggled();
    m_settings->log_error_enabled = m_logErrorEnabled->getIsToggled();

    if (!m_settings->save()) {
        *m_settings = previous;
        if (m_notifyManager) m_notifyManager->warn("保存失败", "无法写入配置文件或配置目录指向，请检查目录权限。", this);
        return false;
    }
    return true;
}

void SettingsView::loadFromSettings()
{
    if (!m_settings) return;
    const Settings& s = *m_settings;

    // 截图
    m_hotkeyEdit->setKeySequence(QKeySequence(s.hotkey));
    m_toolbarOpacity->setValue(s.toolbar_opacity);
    m_crosshairColor->setColor(s.crosshair_color);
    m_crosshairAlpha->setValue(s.crosshair_alpha);
    m_crosshairThickness->setValue(s.crosshair_thickness);
    setToggle(m_crosshairInvert, s.crosshair_invert);
    setToggle(m_crosshairEnabled, s.crosshair_enabled);
    setToggle(m_cursorEnabled, s.cursor_enabled);
    setToggle(m_cursorKeyboardEnabled, s.cursor_keyboard_enabled);
    m_cursorKeyboardMode->setEnabled(s.cursor_keyboard_enabled);
    m_cursorKeyboardMode->setCurrentIndex(s.cursor_keyboard_mode == "wasd" ? 1 : s.cursor_keyboard_mode == "both" ? 2 : 0);

    // 放大镜
    setToggle(m_magnifierEnabled, s.magnifier_enabled);
    m_magnifierZoom->setValue(static_cast<int>(s.magnifier_zoom * 10));
    m_magnifierFixedZoom->setValue(static_cast<int>(s.magnifier_fixed_zoom * 10));
    setToggle(m_magnifierWheelZoom, s.magnifier_wheel_zoom);
    m_magnifierSize->setValue(s.magnifier_size);
    m_magCrossColor->setColor(s.magnifier_cross_color);
    m_magCrossAlpha->setValue(s.magnifier_cross_alpha);
    m_magCrossThickness->setValue(s.magnifier_cross_thickness);
    setToggle(m_magCrossInvert, s.magnifier_cross_invert);
    setToggle(m_gridEnabled, s.grid_enabled);
    m_gridColor->setColor(s.grid_color);
    m_gridAlpha->setValue(s.grid_alpha);
    m_edgeModeCombo->setCurrentIndex(s.edge_mode == "crop" ? 0 : 1);
    m_edgeColor->setColor(s.edge_color);
    setToggle(m_zoomLabelEnabled, s.zoom_label_enabled);
    m_zoomLabelTextColor->setColor(s.zoom_label_text_color);
    m_zoomLabelTextAlpha->setValue(s.zoom_label_text_alpha);
    m_zoomLabelBgColor->setColor(s.zoom_label_bg_color);
    m_zoomLabelBgAlpha->setValue(s.zoom_label_bg_alpha);

    // 坐标与颜色
    setToggle(m_coordLabelEnabled, s.coord_label_enabled);
    m_coordTextColor->setColor(s.coord_label_text_color);
    m_coordTextAlpha->setValue(s.coord_label_text_alpha);
    m_coordBgColor->setColor(s.coord_label_bg_color);
    m_coordBgAlpha->setValue(s.coord_label_bg_alpha);
    setToggle(m_colorLabelEnabled, s.color_label_enabled);
    m_colorTextColor->setColor(s.color_label_text_color);
    m_colorTextAlpha->setValue(s.color_label_text_alpha);
    m_colorBgColor->setColor(s.color_label_bg_color);
    m_colorBgAlpha->setValue(s.color_label_bg_alpha);
    m_colorFormatCombo->setCurrentIndex(s.color_format == "rgb" ? 0 : 1);
    m_copyColorKeyEdit->setKeySequence(QKeySequence(s.copy_color_key));
    m_colorFormatKeyMode->setCurrentIndex(s.color_format_key == "Shift" ? 0 : 1);
    m_colorFormatKeyEdit->setKeySequence(s.color_format_key == "Shift" ? QKeySequence() : QKeySequence(s.color_format_key));
    setToggle(m_colorSwatchEnabled, s.color_swatch_enabled);
    setToggle(m_copyHexPrefix, s.copy_hex_prefix);
    setToggle(m_copyRgbPrefix, s.copy_rgb_prefix);

    // 存储
    m_dirEdit->setText(s.save_dir);
    const int fmtIdx = m_formatCombo->findText(s.save_format.toLower());
    if (fmtIdx >= 0) m_formatCombo->setCurrentIndex(fmtIdx);
    m_configEdit->setText(s.config_dir);
    if (s.config_dir.isEmpty()) m_configEdit->setPlaceholderText("（程序所在目录）");

    // OCR
    m_ocrLangList->reload(s.ai_ocr_langs, s.ocr_tessdata_dir);
    m_ocrTessdataEdit->setText(s.ocr_tessdata_dir);
    if (s.ocr_tessdata_dir.isEmpty()) m_ocrTessdataEdit->setPlaceholderText("留空则自动检测");

    m_loadingAiProfile = true;
    // AI 服务：先静默恢复服务商，再显式回填地址/Key，
    // 避免 onProviderChanged 用服务商默认值覆盖已保存的自定义配置。
    {
        const bool blocked = m_aiProviderCombo->blockSignals(true);
        const int pidx = m_aiProviderCombo->findData(s.ai_provider);
        if (pidx >= 0) m_aiProviderCombo->setCurrentIndex(pidx);
        m_aiProviderCombo->blockSignals(blocked);
    }
    m_currentProvider = s.ai_provider;
    m_providerKeys.clear();
    m_providerBases.clear();
    m_providerModels.clear();
    if (!s.ai_api_key.isEmpty()) m_providerKeys[s.ai_provider] = s.ai_api_key;
    m_providerBases[s.ai_provider] = s.ai_api_base;
    m_providerModels[s.ai_provider] = s.ai_model;
    m_aiApiKeyEdit->setText(s.ai_api_key);
    m_aiApiBaseEdit->setText(s.ai_api_base);
    m_aiCustomModelsEdit->setText(s.ai_custom_models);
    m_aiCustomModelsCard->setVisible(s.ai_provider == "custom");
    m_aiModelCombo->clear();
    if (s.ai_provider == "custom") {
        for (const QString& value : s.ai_custom_models.split(QRegularExpression("[,，;；]"), Qt::SkipEmptyParts)) {
            const QString model = value.trimmed();
            if (!model.isEmpty() && m_aiModelCombo->findText(model) < 0) m_aiModelCombo->addItem(model);
        }
    }
    m_aiModelCombo->setCurrentText(s.ai_model);
    m_aiTimeout->setValue(s.ai_timeout);
    m_aiResponseIdleTimeout->setValue(s.ai_response_idle_timeout);
    m_aiTemperature->setValue(static_cast<int>(s.ai_temperature * 100));
    m_aiMaxTokens->setValue(s.ai_max_tokens);
    m_aiTopP->setValue(static_cast<int>(s.ai_top_p * 100));
    m_aiFrequencyPenalty->setValue(static_cast<int>(s.ai_frequency_penalty * 100));
    m_aiPresencePenalty->setValue(static_cast<int>(s.ai_presence_penalty * 100));
    m_aiSeedEdit->setText(QString::number(s.ai_seed));
    {
        const int idx = m_aiDoSampleCombo->findData(s.ai_do_sample);
        m_aiDoSampleCombo->setCurrentIndex(idx >= 0 ? idx : 0);
    }
    {
        const int idx = m_aiThinkingCombo->findData(s.ai_thinking);
        m_aiThinkingCombo->setCurrentIndex(idx >= 0 ? idx : 0);
    }
    m_aiStopEdit->setPlainText(s.ai_stop);
    setToggle(m_aiStreamUsage, s.ai_stream_usage);
    m_aiErrorRow->setVisible(false);
    m_aiProfiles = QJsonDocument::fromJson(s.ai_model_profiles.toUtf8()).object();
    m_aiMetadata = QJsonDocument::fromJson(s.ai_model_metadata.toUtf8()).object();
    m_aiProfileKey.clear();
    applyAiProfile(s.toDict());
    m_loadingAiProfile = false;
    switchAiProfile();

    // 翻译
    const int tli = m_aiTargetLangCombo->findText(s.ai_target_lang);
    if (tli >= 0) m_aiTargetLangCombo->setCurrentIndex(tli);
    else m_aiTargetLangCombo->setCurrentText(s.ai_target_lang);
    setToggle(m_aiConfirmBefore, s.ai_confirm_before_translate);
    setToggle(m_aiStreamThinking, s.ai_stream_thinking);
    m_aiPromptEdit->setPlainText(s.ai_translation_prompt);
    m_textPromptEdit->setPlainText(s.text_translation_prompt);

    // 常规（主题放最后：它会触发全局重绘）
    const int themeIdx = m_themeCombo->findData(s.theme_mode);
    if (themeIdx >= 0) m_themeCombo->setCurrentIndex(themeIdx);
    m_appModeCombo->setCurrentIndex(s.app_mode == "translate" ? 0 : 1);
    if (s.close_to_tray.toBool() == true)
        m_closeToTrayCombo->setCurrentIndex(1);
    else if (s.close_to_tray.toBool() == false
             && s.close_to_tray.isValid() && !s.close_to_tray.isNull())
        m_closeToTrayCombo->setCurrentIndex(2);
    else
        m_closeToTrayCombo->setCurrentIndex(0);

    setToggle(m_notifyCopy, s.notify_copy);
    setToggle(m_notifySaveSuccess, s.notify_save_success);
    setToggle(m_notifySaveFail, s.notify_save_fail);
    setToggle(m_notifyCaptureFail, s.notify_capture_fail);
    setToggle(m_notifyHotkeyFail, s.notify_hotkey_fail);
    setToggle(m_notifyTransSuccess, s.notify_translate_success);
    setToggle(m_notifyTransFail, s.notify_translate_fail);
    setToggle(m_notifyOcrFail, s.notify_ocr_fail);
    setToggle(m_notifyLangDownload, s.notify_lang_download);
    setToggle(m_logDebugEnabled, s.log_debug_enabled);
    setToggle(m_logInfoEnabled, s.log_info_enabled);
    setToggle(m_logWarningEnabled, s.log_warning_enabled);
    setToggle(m_logErrorEnabled, s.log_error_enabled);

    refreshToggleStates();
}

// ============================================================================
// 动作
// ============================================================================

void SettingsView::onSave()
{
    if (!saveToSettings()) return;
    emit settingsSaved();
    ElaMessageBar::success(ElaMessageBarType::TopRight,
                           "设置已保存", "新的设置已立即生效。", 2200, this);
}

void SettingsView::onRevert()
{
    ++m_fetchGen;       // 丢弃进行中的模型列表请求
    loadFromSettings(); // 从已保存的配置回填
    ElaMessageBar::information(ElaMessageBarType::TopRight,
                               "已放弃修改", "表单已恢复为当前已保存的配置。", 2200, this);
}

void SettingsView::onResetAll()
{
    if (!confirmReset("全部")) return;
    ++m_fetchGen;
    const Settings previous = *m_settings;
    Settings defaults(Settings::defaultsDict());
    defaults.config_dir = previous.config_dir;
    m_settings->updateFromDict(defaults.toDict());
    if (!m_settings->save()) {
        *m_settings = previous;
        if (m_notifyManager) m_notifyManager->warn("恢复失败", "配置无法保存，请检查目录权限。", this);
        return;
    }
    loadFromSettings();
    emit settingsSaved();
    ElaMessageBar::success(ElaMessageBarType::TopRight,
                           "已恢复默认", "全部设置已恢复为出厂默认值。", 2600, this);
}

// ============================================================================
// 重置
// ============================================================================

bool SettingsView::confirmReset(const QString& scope) {
    NotifyManager fallback;
    NotifyManager* notify = m_notifyManager ? m_notifyManager : &fallback;
    return notify->confirm("恢复默认设置",
        QString("确定要将%1设置恢复为默认值吗？\n\n此操作不可撤销。").arg(scope), this);
}

// ================================================================ 表驱动重置
// 对齐 Python `_reset_map`：一张表 (tab → 重置动作) 驱动所有"恢复本页默认"。
// 新增设置项只需在 buildResetTable() 加一行，重置自动生效，无需再写 reset 方法。

void SettingsView::buildResetTable() {
    m_resetItems = {
        // ---- 截图 ----
        {"screenshot", [this](const Settings& d){ m_hotkeyEdit->setKeySequence(QKeySequence(d.hotkey)); }},
        {"screenshot", [this](const Settings& d){ m_toolbarOpacity->setValue(d.toolbar_opacity); }},
        {"screenshot", [this](const Settings& d){ m_crosshairColor->setColor(d.crosshair_color); }},
        {"screenshot", [this](const Settings& d){ m_crosshairAlpha->setValue(d.crosshair_alpha); }},
        {"screenshot", [this](const Settings& d){ m_crosshairThickness->setValue(d.crosshair_thickness); }},
        {"screenshot", [this](const Settings& d){ setToggle(m_crosshairInvert, d.crosshair_invert); }},
        {"screenshot", [this](const Settings& d){ setToggle(m_crosshairEnabled, d.crosshair_enabled); }},
        {"screenshot", [this](const Settings& d){ setToggle(m_cursorEnabled, d.cursor_enabled); }},
        {"screenshot", [this](const Settings& d){
            setToggle(m_cursorKeyboardEnabled, d.cursor_keyboard_enabled);
            m_cursorKeyboardMode->setEnabled(d.cursor_keyboard_enabled);
            m_cursorKeyboardMode->setCurrentIndex(d.cursor_keyboard_mode == "wasd" ? 1 : d.cursor_keyboard_mode == "both" ? 2 : 0);
        }},

        // ---- 放大镜 ----
        {"magnifier", [this](const Settings& d){ setToggle(m_magnifierEnabled, d.magnifier_enabled); }},
        {"magnifier", [this](const Settings& d){ m_magnifierZoom->setValue(static_cast<int>(d.magnifier_zoom * 10)); }},
        {"magnifier", [this](const Settings& d){ m_magnifierFixedZoom->setValue(static_cast<int>(d.magnifier_fixed_zoom * 10)); }},
        {"magnifier", [this](const Settings& d){ setToggle(m_magnifierWheelZoom, d.magnifier_wheel_zoom); }},
        {"magnifier", [this](const Settings& d){ m_magnifierSize->setValue(d.magnifier_size); }},
        {"magnifier", [this](const Settings& d){ m_magCrossColor->setColor(d.magnifier_cross_color); }},
        {"magnifier", [this](const Settings& d){ m_magCrossAlpha->setValue(d.magnifier_cross_alpha); }},
        {"magnifier", [this](const Settings& d){ m_magCrossThickness->setValue(d.magnifier_cross_thickness); }},
        {"magnifier", [this](const Settings& d){ setToggle(m_magCrossInvert, d.magnifier_cross_invert); }},
        {"magnifier", [this](const Settings& d){ setToggle(m_gridEnabled, d.grid_enabled); }},
        {"magnifier", [this](const Settings& d){ m_gridColor->setColor(d.grid_color); }},
        {"magnifier", [this](const Settings& d){ m_gridAlpha->setValue(d.grid_alpha); }},
        {"magnifier", [this](const Settings& d){ m_edgeModeCombo->setCurrentIndex(d.edge_mode == "crop" ? 0 : 1); }},
        {"magnifier", [this](const Settings& d){ m_edgeColor->setColor(d.edge_color); }},
        {"magnifier", [this](const Settings& d){ setToggle(m_zoomLabelEnabled, d.zoom_label_enabled); }},
        {"magnifier", [this](const Settings& d){ m_zoomLabelTextColor->setColor(d.zoom_label_text_color); }},
        {"magnifier", [this](const Settings& d){ m_zoomLabelTextAlpha->setValue(d.zoom_label_text_alpha); }},
        {"magnifier", [this](const Settings& d){ m_zoomLabelBgColor->setColor(d.zoom_label_bg_color); }},
        {"magnifier", [this](const Settings& d){ m_zoomLabelBgAlpha->setValue(d.zoom_label_bg_alpha); }},

        // ---- 坐标与颜色 ----
        {"labels", [this](const Settings& d){ setToggle(m_coordLabelEnabled, d.coord_label_enabled); }},
        {"labels", [this](const Settings& d){ m_coordTextColor->setColor(d.coord_label_text_color); }},
        {"labels", [this](const Settings& d){ m_coordTextAlpha->setValue(d.coord_label_text_alpha); }},
        {"labels", [this](const Settings& d){ m_coordBgColor->setColor(d.coord_label_bg_color); }},
        {"labels", [this](const Settings& d){ m_coordBgAlpha->setValue(d.coord_label_bg_alpha); }},
        {"labels", [this](const Settings& d){ setToggle(m_colorLabelEnabled, d.color_label_enabled); }},
        {"labels", [this](const Settings& d){ m_colorTextColor->setColor(d.color_label_text_color); }},
        {"labels", [this](const Settings& d){ m_colorTextAlpha->setValue(d.color_label_text_alpha); }},
        {"labels", [this](const Settings& d){ m_colorBgColor->setColor(d.color_label_bg_color); }},
        {"labels", [this](const Settings& d){ m_colorBgAlpha->setValue(d.color_label_bg_alpha); }},
        {"labels", [this](const Settings& d){ m_colorFormatCombo->setCurrentIndex(d.color_format == "rgb" ? 0 : 1); }},
        {"labels", [this](const Settings& d){ m_copyColorKeyEdit->setKeySequence(QKeySequence(d.copy_color_key)); }},
        {"labels", [this](const Settings& d){
            m_colorFormatKeyMode->setCurrentIndex(d.color_format_key == "Shift" ? 0 : 1);
            m_colorFormatKeyEdit->setKeySequence(d.color_format_key == "Shift" ? QKeySequence() : QKeySequence(d.color_format_key));
            setToggle(m_colorSwatchEnabled, d.color_swatch_enabled);
        }},
        {"labels", [this](const Settings& d){ setToggle(m_copyHexPrefix, d.copy_hex_prefix); }},
        {"labels", [this](const Settings& d){ setToggle(m_copyRgbPrefix, d.copy_rgb_prefix); }},

        // ---- 存储 ----
        {"storage", [this](const Settings&){ m_dirEdit->setText(QString()); }},          // save_dir 动态默认，清空由用户重选
        {"storage", [this](const Settings&){ m_formatCombo->setCurrentIndex(0); }},      // 默认 png
        {"storage", [this](const Settings&){ m_configEdit->clear(); m_configEdit->setPlaceholderText("（程序所在目录）"); }},

        // ---- OCR ----
        {"ocr", [this](const Settings& d){ m_ocrLangList->reload(d.ai_ocr_langs, d.ocr_tessdata_dir); m_ocrTessdataEdit->clear(); m_ocrTessdataEdit->setPlaceholderText("留空则自动检测"); }},

        // ---- AI 设置 ----
        // 先恢复默认服务商（触发 onProviderChanged 同步其默认 API 地址/Key），
        // 避免"服务商保持用户所选、地址却恢复为默认"的错配（对齐 Python _reset_map）
        {"ai_settings", [this](const Settings&){
            m_providerKeys.clear(); m_providerBases.clear(); m_providerModels.clear();
            m_currentProvider.clear();
        }},
        {"ai_settings", [this](const Settings& d){
            int idx = m_aiProviderCombo->findData(d.ai_provider);
            if (idx >= 0) m_aiProviderCombo->setCurrentIndex(idx);
        }},
        {"ai_settings", [this](const Settings&){ m_aiApiKeyEdit->clear(); }},
        {"ai_settings", [this](const Settings& d){ m_aiApiBaseEdit->setText(d.ai_api_base); }},
        {"ai_settings", [this](const Settings&){ m_aiModelCombo->clearEditText(); }},
        {"ai_settings", [this](const Settings&){ m_aiCustomModelsEdit->clear(); }},
        {"ai_settings", [this](const Settings& d){ m_aiTimeout->setValue(d.ai_timeout); m_aiResponseIdleTimeout->setValue(d.ai_response_idle_timeout); }},
        {"ai_settings", [this](const Settings& d){ m_aiTemperature->setValue(static_cast<int>(d.ai_temperature * 100)); }},
        {"ai_settings", [this](const Settings& d){ m_aiMaxTokens->setValue(d.ai_max_tokens); }},
        {"ai_settings", [this](const Settings& d){ m_aiTopP->setValue(static_cast<int>(d.ai_top_p * 100)); }},
        {"ai_settings", [this](const Settings& d){ m_aiFrequencyPenalty->setValue(static_cast<int>(d.ai_frequency_penalty * 100)); }},
        {"ai_settings", [this](const Settings& d){ m_aiPresencePenalty->setValue(static_cast<int>(d.ai_presence_penalty * 100)); }},
        {"ai_settings", [this](const Settings& d){ m_aiSeedEdit->setText(QString::number(d.ai_seed)); }},
        {"ai_settings", [this](const Settings& d){
            const int idx = m_aiDoSampleCombo->findData(d.ai_do_sample);
            m_aiDoSampleCombo->setCurrentIndex(idx >= 0 ? idx : 0);
        }},
        {"ai_settings", [this](const Settings& d){
            const int idx = m_aiThinkingCombo->findData(d.ai_thinking);
            m_aiThinkingCombo->setCurrentIndex(idx >= 0 ? idx : 0);
        }},
        {"ai_settings", [this](const Settings& d){ m_aiStopEdit->setPlainText(d.ai_stop); }},
        {"ai_settings", [this](const Settings& d){ setToggle(m_aiStreamUsage, d.ai_stream_usage); }},
        {"ai_settings", [this](const Settings&){ m_aiErrorRow->setVisible(false); }},
        {"ai_settings", [this](const Settings&){
            m_aiProfiles = {}; m_aiMetadata = {};
            applyAiProfile({});
            refreshAiPage();
        }},

        // ---- 翻译 ----
        {"translate", [this](const Settings& d){
            int idx = m_aiTargetLangCombo->findText(d.ai_target_lang);
            if (idx >= 0) m_aiTargetLangCombo->setCurrentIndex(idx);
        }},
        {"translate", [this](const Settings& d){ setToggle(m_aiConfirmBefore, d.ai_confirm_before_translate); }},
        {"translate", [this](const Settings& d){ setToggle(m_aiStreamThinking, d.ai_stream_thinking); }},
        {"translate", [this](const Settings& d){ m_aiPromptEdit->setPlainText(d.ai_translation_prompt); }},
        {"translate", [this](const Settings& d){ m_textPromptEdit->setPlainText(d.text_translation_prompt); }},

        // ---- 常规 ----
        {"general", [this](const Settings& d){
            int idx = m_themeCombo->findData(d.theme_mode);
            if (idx >= 0) m_themeCombo->setCurrentIndex(idx);
        }},
        {"general", [this](const Settings& d){ m_appModeCombo->setCurrentIndex(d.app_mode == "translate" ? 0 : 1); }},
        {"general", [this](const Settings&){ m_closeToTrayCombo->setCurrentIndex(0); }},  // 默认"询问"
        {"general", [this](const Settings& d){ setToggle(m_notifyCopy, d.notify_copy); }},
        {"general", [this](const Settings& d){ setToggle(m_notifySaveSuccess, d.notify_save_success); }},
        {"general", [this](const Settings& d){ setToggle(m_notifySaveFail, d.notify_save_fail); }},
        {"general", [this](const Settings& d){ setToggle(m_notifyCaptureFail, d.notify_capture_fail); }},
        {"general", [this](const Settings& d){ setToggle(m_notifyHotkeyFail, d.notify_hotkey_fail); }},
        {"general", [this](const Settings& d){ setToggle(m_notifyTransSuccess, d.notify_translate_success); }},
        {"general", [this](const Settings& d){ setToggle(m_notifyTransFail, d.notify_translate_fail); }},
        {"general", [this](const Settings& d){ setToggle(m_notifyOcrFail, d.notify_ocr_fail); }},
        {"general", [this](const Settings& d){ setToggle(m_notifyLangDownload, d.notify_lang_download); }},
        {"general", [this](const Settings& d){ setToggle(m_logDebugEnabled, d.log_debug_enabled); }},
        {"general", [this](const Settings& d){ setToggle(m_logInfoEnabled, d.log_info_enabled); }},
        {"general", [this](const Settings& d){ setToggle(m_logWarningEnabled, d.log_warning_enabled); }},
        {"general", [this](const Settings& d){ setToggle(m_logErrorEnabled, d.log_error_enabled); }},
    };
}

void SettingsView::resetTab(const QString& tab, const QString& scope) {
    if (!confirmReset(scope)) return;
    ++m_fetchGen;  // 使进行中的模型获取失效
    Settings d(Settings::defaultsDict());
    for (const auto& item : m_resetItems)
        if (item.tab == tab) item.apply(d);
    refreshToggleStates();
}

void SettingsView::refreshToggleStates() {
    refreshScreenshotPage();
    refreshMagnifierPage();
    refreshLabelsPage();
}
