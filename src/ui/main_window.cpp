#include "main_window.h"
#include "../translate/translation_page.h"
#include "../settings.h"
#include "../notify/notify_manager.h"
#include "../log/log_manager.h"
#include "snapkit.h"
#include "settings_view.h"
#include "home_tool_card.h"

#include <QComboBox>
#include <QPushButton>
#include <QCheckBox>
#include <QPlainTextEdit>
#include <QDialog>
#include <QSplitter>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QVBoxLayout>
#include <QLabel>
#include <QCloseEvent>
#include <QResizeEvent>
#include <QMessageBox>
#include <QApplication>
#include <QShortcut>
#include <QTimer>
#include <QSystemTrayIcon>

using namespace SnapUi;

// ============================================================================
// CloseDialog
// ============================================================================

CloseDialog::CloseDialog(QWidget* parent)
    : ElaDialog(parent)
{
    setWindowTitle("SnapLens");
    setFixedSize(500, 280);
    setIsDefaultClosed(false);
    setWindowButtonFlags(ElaAppBarType::CloseButtonHint);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 48, 24, 20);
    layout->setSpacing(12);
    layout->addWidget(sectionHeader("关闭 SnapLens", this));
    layout->addWidget(detailText("后台运行会保留截图快捷键和托盘入口。退出程序将停止所有任务。", this));
    m_remember = new ElaCheckBox("记住我的选择，下次不再询问", this);
    layout->addWidget(m_remember);
    layout->addStretch();
    auto* actions = new QHBoxLayout();
    auto* cancel = secondaryButton("取消", this);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    actions->addWidget(cancel);
    m_btnExit = secondaryButton("退出程序", this);
    connect(m_btnExit, &QPushButton::clicked, this, [this] { done(false); });
    actions->addWidget(m_btnExit);
    m_btnTray = primaryButton("后台运行", this);
    connect(m_btnTray, &QPushButton::clicked, this, [this] { done(true); });
    m_btnTray->setEnabled(QSystemTrayIcon::isSystemTrayAvailable());
    actions->addWidget(m_btnTray);
    layout->addLayout(actions);
    connect(this, &ElaDialog::closeButtonClicked, this, &QDialog::reject);
}

void CloseDialog::done(bool toTray)
{
    m_decided = true;
    m_result = toTray;
    accept();
}

std::pair<QVariant, bool> CloseDialog::ask(QWidget* parent)
{
    CloseDialog dlg(parent);
    dlg.exec();
    if (!dlg.m_decided)
        return {QVariant(), false};
    return {QVariant(dlg.m_result), dlg.m_remember->isChecked()};
}

// ============================================================================
// MainWindow
// ============================================================================

MainWindow::MainWindow(Settings* settings, NotifyManager* notifyManager,
                       const QString& appMode, QWidget* parent)
    : ElaWindow(parent)
    , m_settings(settings)
    , m_notifyManager(notifyManager)
{
    setWindowTitle("SnapLens");
    resize(1220, 800);
    setMinimumSize(900, 600);
    setNavigationBarWidth(226);
    const QPixmap appIcon = makeAppIcon();
    setWindowIcon(QIcon(appIcon));
    setUserInfoCardPixmap(appIcon);
    setUserInfoCardTitle("SnapLens");
    setUserInfoCardSubTitle("你的日常工具箱");
    setIsDefaultClosed(false);
    connect(this, &ElaWindow::closeButtonClicked, this, [this] { close(); });
    setIsAllowPageOpenInNewWindow(false);
    buildUi();
    Q_UNUSED(appMode);
    polishPopup(this);
}

void MainWindow::buildUi()
{
    // 设置是窗口内的一个普通页面（挂在页脚节点上），不再是独立弹窗。
    m_settingsView = new SettingsView(m_settings, m_notifyManager, this);
    connect(m_settingsView, &SettingsView::settingsSaved, this, &MainWindow::settingsSaved);

    m_tools = {
        {"translate", "文本翻译", "按领域与目标语言翻译文本",
            "支持自定义 AI 服务、流式输出和专业场景。", ElaIconType::Language,
            [this] {
                m_translationPage = new TranslationPage(m_settings, m_notifyManager, this);
                connect(m_translationPage, &TranslationPage::settingsRequested, this, &MainWindow::showSettings);
                return m_translationPage;
            }},
        {"capture", "截图与识别", "截图、贴图、取色、OCR 与截图翻译",
            "框选后可复制、保存、钉图或交给 OCR 与 AI。", ElaIconType::CameraViewfinder,
            [this] { return buildCapturePage(); }},
    };
    QWidget* homePage = buildHomePage();
    addPageNode("首页", homePage, ElaIconType::House);
    for (const auto& tool : m_tools) {
        QWidget* page = tool.createPage();
        addPageNode(tool.title, page, tool.icon);
        setProperty(qPrintable("tool." + tool.id), page->property("ElaPageKey"));
    }
    addFooterNode("设置", m_settingsView, m_settingsKey, 0, ElaIconType::GearComplex);

    connect(this, &ElaWindow::userInfoCardClicked, this, [this, homePage] {
        navigation(homePage->property("ElaPageKey").toString());
    });
}

void MainWindow::showSettings(const QString& category)
{
    if (m_settingsKey.isEmpty()) return;
    if (!category.isEmpty()) m_settingsView->selectCategory(category);
    navigation(m_settingsKey);
}

QWidget* MainWindow::buildHomePage()
{
    auto* page = new SettingsPage("首页", "选择一个工具开始工作。需要的服务将在首次使用时提示配置。", this);
    page->bodyLayout()->setContentsMargins(Dim::PageHMargin, 24, Dim::PageHMargin, 24);
    page->addSection("所有工具", "点击卡片打开；悬浮或键盘聚焦可查看详细说明。");
    auto* gridHost = new QWidget(page);
    m_homeGrid = new QGridLayout(gridHost);
    m_homeGrid->setContentsMargins(0, 0, 0, 0);
    m_homeGrid->setHorizontalSpacing(14);
    m_homeGrid->setVerticalSpacing(14);
    for (const auto& tool : m_tools) {
        auto* card = new HomeToolCard(tool, gridHost);
        connect(card, &QPushButton::clicked, this, [this, id = tool.id] {
            navigation(property(qPrintable("tool." + id)).toString());
        });
        m_homeCards.append(card);
    }
    layoutHomeCards();
    page->addBlock(gridHost, 20);

    page->addSection("快捷操作");
    auto* quick = new Card(page);
    auto* quickLayout = new QVBoxLayout(quick);
    quickLayout->setContentsMargins(18, 16, 18, 16);
    quickLayout->setSpacing(12);
    auto* shortcutHint = detailText({}, quick);
    auto refreshShortcut = [this, shortcutHint] {
        shortcutHint->setText(QString("截图快捷键：%1   ·   可在设置中修改")
            .arg(hotkeyText(m_settings->hotkey)));
    };
    refreshShortcut();
    connect(this, &MainWindow::settingsSaved, shortcutHint, refreshShortcut);
    quickLayout->addWidget(shortcutHint);
    auto* actions = new QHBoxLayout();
    actions->setSpacing(10);
    auto* capture = primaryButton("开始截图", quick);
    capture->setFixedWidth(140);
    connect(capture, &QPushButton::clicked, this, &MainWindow::snipRequested);
    actions->addWidget(capture);
    auto* settings = secondaryButton("应用设置", quick);
    settings->setFixedWidth(120);
    connect(settings, &QPushButton::clicked, this, [this] { showSettings("general"); });
    actions->addWidget(settings);
    actions->addStretch();
    quickLayout->addLayout(actions);
    page->addBlock(quick);
    page->bodyLayout()->addStretch();
    return page;
}

void MainWindow::layoutHomeCards()
{
    if (!m_homeGrid) return;
    const int columns = width() >= 1500 ? 3 : width() >= 820 ? 2 : 1;
    while (auto* item = m_homeGrid->takeAt(0)) delete item;
    for (int i = 0; i < m_homeCards.size(); ++i)
        m_homeGrid->addWidget(m_homeCards[i], i / columns, i % columns);
    for (int col = 0; col < 3; ++col)
        m_homeGrid->setColumnStretch(col, col < columns ? 1 : 0);
}

QWidget* MainWindow::buildCapturePage()
{
    auto* page = new QWidget(this);
    page->setWindowTitle("截图");
    auto* root = new QVBoxLayout(page);
    root->setContentsMargins(Dim::PageHMargin, 18, Dim::PageHMargin, 22);
    root->setSpacing(14);

    root->addWidget(pageHeader(
        "截图", "框选屏幕内容后，可直接复制、保存、钉图、OCR 或交给 AI 翻译。", page));

    const QString hotkey = m_settings ? hotkeyText(m_settings->hotkey)
                                      : QStringLiteral("Ctrl+Shift+Z");

    auto* hero = new Card(page);
    auto* heroLayout = new QVBoxLayout(hero);
    heroLayout->setContentsMargins(24, 22, 24, 22);
    heroLayout->setSpacing(10);

    auto* glyph = iconGlyph(ElaIconType::CameraViewfinder, 30, hero);
    glyph->setFixedSize(48, 48);
    heroLayout->addWidget(glyph);
    heroLayout->addWidget(sectionHeader("捕捉屏幕上的任意区域", hero));
    heroLayout->addWidget(detailText(
        "按下快捷键即可开始，或点击下方按钮。", hero));

    auto* hotkeyRow = new QHBoxLayout();
    hotkeyRow->setSpacing(10);
    auto* captureButton = primaryButton("开始截图", hero);
    captureButton->setMinimumWidth(140);
    connect(captureButton, &QPushButton::clicked, this, &MainWindow::snipRequested);
    hotkeyRow->addWidget(captureButton);
    auto* shortcutHint = detailText(QStringLiteral("当前快捷键：%1").arg(hotkey), hero);
    hotkeyRow->addWidget(shortcutHint);
    connect(this, &MainWindow::settingsSaved, shortcutHint, [this, shortcutHint] {
        shortcutHint->setText(QStringLiteral("当前快捷键：%1").arg(hotkeyText(m_settings->hotkey)));
    });
    hotkeyRow->addStretch();
    heroLayout->addSpacing(4);
    heroLayout->addLayout(hotkeyRow);
    root->addWidget(hero);

    auto* tips = new Card(page);
    auto* tipsLayout = new QVBoxLayout(tips);
    tipsLayout->setContentsMargins(24, 20, 24, 20);
    tipsLayout->setSpacing(8);
    tipsLayout->addWidget(sectionHeader("选区完成后的操作", tips));
    for (const auto& line : QStringList{
             "保存  ·  存为 PNG / JPG / BMP，目录与格式可在设置中调整",
             "钉图  ·  生成置顶窗口，可拖动、滚轮缩放、右键另存",
             "OCR  ·  Tesseract 进程内识别，支持多语言语言包",
             "AI 翻译  ·  识别文字后交给 AI，附带译文与思考过程",
             "确认  ·  复制到剪贴板并退出截图",
         }) {
        tipsLayout->addWidget(detailText(line, tips));
    }
    root->addWidget(tips);

    auto* shortcuts = new Card(page);
    auto* shortcutsLayout = new QVBoxLayout(shortcuts);
    shortcutsLayout->setContentsMargins(24, 20, 24, 20);
    shortcutsLayout->setSpacing(8);
    shortcutsLayout->addWidget(sectionHeader("键鼠操作", shortcuts));
    for (const auto& line : QStringList{
             "拖动鼠标框选区域；单击选中整个窗口；空白处单击选中全屏",
             "双击或 Enter 直接复制并退出，Esc 取消截图",
             "截图状态下按 C 复制光标下的像素颜色",
             "滚轮实时调整放大镜倍率（需在设置中开启）",
         }) {
        shortcutsLayout->addWidget(detailText(line, shortcuts));
    }
    root->addWidget(shortcuts);
    root->addStretch();
    return page;
}

void MainWindow::prepareForQuit()
{
    if (m_translationPage) m_translationPage->prepareForQuit();
    hide();
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    event->ignore();
    if (m_closePending) return;
    m_closePending = true;
    // Leave the native close event before entering a modal event loop.
    QTimer::singleShot(0, this, [this] {
        QVariant toTray = m_settings ? m_settings->close_to_tray : QVariant();
        if (toTray.isNull()) {
            const auto [result, remember] = CloseDialog::ask(this);
            if (!result.isValid()) { m_closePending = false; return; }
            toTray = result;
            if (remember && m_settings) {
                m_settings->close_to_tray = toTray;
                m_settings->save();
            }
        }
        m_closePending = false;
        if (toTray.toBool()) {
            if (QSystemTrayIcon::isSystemTrayAvailable()) hide();
            else ElaMessageBar::warning(ElaMessageBarType::TopRight, "无法后台运行",
                 "系统托盘不可用，请保留窗口或选择退出。", 3000, this);
        } else {
            emit quitRequested();
        }
    });
}

bool MainWindow::isBusy() const
{
    return m_translationPage && m_translationPage->isBusy();
}

void MainWindow::resizeEvent(QResizeEvent* event)
{
    ElaWindow::resizeEvent(event);
    setNavigationBarDisplayMode(width() < 1100 ? ElaNavigationType::Compact : ElaNavigationType::Maximal);
    layoutHomeCards();
}
