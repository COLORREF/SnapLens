#ifndef SNAPLENS_UI_SNAPKIT_H
#define SNAPLENS_UI_SNAPKIT_H

// ============================================================================
// SnapUi — SnapLens 的界面设计系统
//
// 目标风格：Windows 11 / WinUI 3 / 微软商店（Fluent）。
// 这里集中定义间距、字号、语义颜色，以及两个核心容器：
//
//   SettingsCard  —— 单条设置行（标题+说明在左，控件在右），对应 WinUI3
//                    的 SettingsCard，用于设置对话框与向导。
//   PanelCard     —— 带标题的内容卡片，用于编辑器、列表等整块内容。
//
// 所有颜色都走 ElaTheme 语义色，并通过 bindTheme 在主题切换时自动重刷，
// 避免各窗口散落 #RRGGBB 硬编码。
// ============================================================================

#include "ElaWidgetTools.hpp"

#include <QFrame>
#include <QPixmap>
#include <QString>
#include <functional>

class QHBoxLayout;
class QEvent;
class QVBoxLayout;
class QLayout;
class QPaintEvent;
class QSlider;
class QSpinBox;
class QComboBox;
class QLineEdit;
class QPushButton;
class QScrollArea;

namespace SnapUi {

// ---------------------------------------------------------------------------
// 尺寸令牌
// ---------------------------------------------------------------------------
namespace Dim {
constexpr int PageHMargin   = 32;   // 页面左右留白
constexpr int PageVMargin   = 20;   // 页面上下留白
constexpr int SectionGap    = 24;   // 分区之间的垂直间距
constexpr int RowGap        = 4;    // 同类设置行之间的间距
constexpr int ContentGap    = 12;   // 通用控件间距
constexpr int RowPadH       = 16;   // 设置行内左右内边距
constexpr int RowPadV       = 10;   // 设置行内上下内边距
constexpr int RowMinHeight  = 56;   // 设置行最小高度（WinUI3 SettingsCard 标准）
constexpr int RowRadius     = 4;    // 设置行圆角（WinUI3 卡片标准 4px）
constexpr int RowIconSize   = 28;   // 须大于 iconGlyph(15) 的 27px 固定尺寸，避免裁切
constexpr int CardRadius    = 6;    // 内容卡片圆角
constexpr int CardPad       = 16;   // 内容卡片内边距
constexpr int LabelWidth    = 320;  // 设置行左侧文字列参考宽度
} // namespace Dim

// ---------------------------------------------------------------------------
// 语义颜色（跟随主题）
// ---------------------------------------------------------------------------
QColor colorPrimary();
QColor colorPrimaryHover();
QColor colorText();
QColor colorTextSecondary();
QColor colorTextTertiary();
QColor colorBorder();
QColor colorSurface();
QColor colorAccentText();
/// 侧栏底色（比页面底色深/浅一层的"表面"），用于设置分类栏与向导步骤栏。
QColor colorRail();

/// 在 widget 存活期间同步主题色：立刻调用一次 apply，并在主题切换时再次调用。
void bindTheme(QWidget* widget, const std::function<void()>& apply);

/// 把 ElaText 设为次要文本色（灰色说明文字），并随主题自动更新。
void makeSecondaryText(ElaText* text);
/// 把 ElaText 设为三级文本色（更弱的提示文字）。
void makeTertiaryText(ElaText* text);

// ---------------------------------------------------------------------------
// 文本构件（统一的字号层级）
// ---------------------------------------------------------------------------
ElaText* pageTitle(const QString& text, QWidget* parent = nullptr);      // 页面大标题
ElaText* pageSubtitle(const QString& text, QWidget* parent = nullptr);   // 页面副标题
ElaText* sectionHeader(const QString& text, QWidget* parent = nullptr);  // 分区标题
ElaText* cardTitle(const QString& text, QWidget* parent = nullptr);      // 卡片标题
ElaText* detailText(const QString& text, QWidget* parent = nullptr);     // 卡片说明文字
ElaText* caption(const QString& text, QWidget* parent = nullptr);        // 脚注

/// 字号可调的图标字形（ElaAwesome 字体），用于空状态、卡片图示等。
ElaText* iconGlyph(ElaIconType::IconName icon, int pixelSize,
                   QWidget* parent = nullptr);

// ---------------------------------------------------------------------------
// 页面头部
// ---------------------------------------------------------------------------
/// 大标题 + 说明文字的页头块。
QWidget* pageHeader(const QString& title, const QString& subtitle,
                    QWidget* parent = nullptr);

// ---------------------------------------------------------------------------
// 容器
// ---------------------------------------------------------------------------

/// 通用圆角容器（跟随主题的描边 + 半透明底色），用于工作区、分组框。
class Card : public QFrame
{
    Q_OBJECT
public:
    explicit Card(QWidget* parent = nullptr);
    void setRadius(int radius);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    int m_radius = Dim::CardRadius;
};

/// 单条设置行：左侧标题/说明，右侧控件。对应 WinUI3 SettingsCard。
class SettingsCard : public ElaScrollPageArea
{
    Q_OBJECT
public:
    explicit SettingsCard(const QString& title, const QString& description = {},
                          QWidget* parent = nullptr);

    /// 行首图标（WinUI3 SettingsCard 的 leading glyph 槽位）。
    void setIcon(ElaIconType::IconName icon);

    /// 在行右侧追加一个控件。
    void addControl(QWidget* control);
    /// 在行右侧追加一组控件（依次排布）。
    void addControls(const QList<QWidget*>& controls);
    /// 行右侧改为放置一个布局（例如「输入框 + 浏览按钮」）。
    void addControlLayout(QLayout* layout);
    /// 让右侧控件占据剩余宽度（用于需要拉伸的输入框、下拉框）。
    void setControlStretch(int stretch);
    /// 允许说明文字换行，行高随之自适应。
    void setTall(bool tall);

    QHBoxLayout* rowLayout() const { return m_row; }
    QSize minimumSizeHint() const override;

protected:
    void changeEvent(QEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    /// 自绘卡片底与描边（不沿用 ElaScrollPageArea 的半透明叠色）。
    void paintEvent(QPaintEvent* event) override;

private:
    /// 主开关关闭（整行禁用）时同步弱化标题与说明文字。
    void refreshEnabledLook();

    QHBoxLayout* m_row = nullptr;
    QWidget*     m_iconHost = nullptr;
    ElaText*     m_icon = nullptr;
    QWidget*     m_labelHost = nullptr;
    QWidget*     m_headerHost = nullptr;
    bool m_stacked = false;
    int m_controlStretch = 0;
    QWidget*     m_controlHost = nullptr;
    QHBoxLayout* m_controlLayout = nullptr;
    QSpacerItem* m_spacer = nullptr;
    ElaText*     m_titleText = nullptr;
    ElaText*     m_descText = nullptr;
};

/// 带标题的内容卡片：标题行（可放右侧按钮）+ 内容区。
class PanelCard : public Card
{
    Q_OBJECT
public:
    explicit PanelCard(const QString& title, const QString& description = {},
                       QWidget* parent = nullptr);

    /// 设置主内容（通常是编辑器、列表、图像视图）。
    void setContent(QWidget* content, int stretch = 1);
    /// 在标题行右侧追加控件（工具按钮等）。
    void addHeaderWidget(QWidget* widget);
    /// 内容区内追加自定义控件（在 setContent 之前调用）。
    void addContentWidget(QWidget* widget, int stretch = 0);

    QVBoxLayout* bodyLayout() const { return m_body; }

private:
    QVBoxLayout* m_body = nullptr;
    QHBoxLayout* m_headerRow = nullptr;
};

// ---------------------------------------------------------------------------
// 控件工厂
// ---------------------------------------------------------------------------
ElaPushButton* primaryButton(const QString& text, QWidget* parent = nullptr);
ElaPushButton* secondaryButton(const QString& text, QWidget* parent = nullptr);
ElaPushButton* subtleButton(const QString& text, QWidget* parent = nullptr);

/// 图标按钮（无文字，带 tooltip）。用于面板标题、工具条等位置。
ElaToolButton* iconButton(ElaIconType::IconName icon, const QString& tooltip,
                          QWidget* parent = nullptr);

/// 应用内统一的开关控件（已按初始值静默设置，不会误触发 toggled）。
ElaToggleSwitch* toggleSwitch(bool checked, QWidget* parent = nullptr);

/// 竖/横分隔线。
QFrame* separator(Qt::Orientation orientation, QWidget* parent = nullptr);

// ---------------------------------------------------------------------------
// 设置行工厂
// ---------------------------------------------------------------------------
SettingsCard* row(const QString& title, const QString& description,
                  QWidget* control, QWidget* parent = nullptr,
                  ElaIconType::IconName icon = ElaIconType::None);

SettingsCard* toggleRow(const QString& title, const QString& description,
                        bool checked, ElaToggleSwitch** toggleOut = nullptr,
                        QWidget* parent = nullptr,
                        ElaIconType::IconName icon = ElaIconType::None);

SettingsCard* sliderRow(const QString& title, const QString& description,
                        int value, int lo, int hi, const QString& suffix,
                        QSlider** sliderOut = nullptr, QWidget* parent = nullptr,
                        ElaIconType::IconName icon = ElaIconType::None);

SettingsCard* decimalSliderRow(const QString& title, const QString& description,
                               double value, double lo, double hi, int decimals,
                               const QString& suffix = QString(),
                               QSlider** sliderOut = nullptr, QWidget* parent = nullptr,
                               ElaIconType::IconName icon = ElaIconType::None);

SettingsCard* comboRow(const QString& title, const QString& description,
                       const QStringList& items, int currentIndex,
                       QComboBox** comboOut = nullptr, QWidget* parent = nullptr,
                       ElaIconType::IconName icon = ElaIconType::None);

/// 整数输入行（QSpinBox）。用于值域过大、滑块不合适的参数（如输出 token 上限）。
SettingsCard* spinRow(const QString& title, const QString& description,
                      int value, int lo, int hi, int step,
                      const QString& suffix = QString(),
                      QSpinBox** spinOut = nullptr, QWidget* parent = nullptr,
                      ElaIconType::IconName icon = ElaIconType::None);

/// 选项带 data 的下拉行（如「跟随系统 / 浅色 / 深色」）。
/// 与 QStringList 版分开命名，避免花括号初始化列表在两者间产生二义性。
SettingsCard* comboRowData(const QString& title, const QString& description,
                           const QList<QPair<QString, QVariant>>& items, int currentIndex,
                           QComboBox** comboOut = nullptr, QWidget* parent = nullptr,
                           ElaIconType::IconName icon = ElaIconType::None);

/// 「输入框 + 浏览…」这一类带辅助按钮的行。
SettingsCard* pathRow(const QString& title, const QString& description,
                      const QString& value, const QString& placeholder,
                      const QStringList& actionTexts, QWidget* parent = nullptr,
                      ElaIconType::IconName icon = ElaIconType::None);
/// 取出 pathRow 创建的输入框与第 actionIndex 个按钮。
QLineEdit* pathRowEdit(QWidget* card);
QPushButton* pathRowButton(QWidget* card, int actionIndex);

/// 把原生输入控件包进一个 Fluent 风格的输入框外壳（用于 QKeySequenceEdit 等
/// 无法直接主题化的控件）。
QWidget* inputShell(QWidget* inner, QWidget* parent = nullptr);

// ---------------------------------------------------------------------------
// 左侧分类导航
// ---------------------------------------------------------------------------

/// 导航中的单项：图标 + 文字，支持选中态（左侧强调条 + 主题色）。
class NavItem : public QWidget
{
    Q_OBJECT
public:
    NavItem(ElaIconType::IconName icon, const QString& text, QWidget* parent = nullptr);

    void setSelected(bool selected);
    bool isSelected() const { return m_selected; }
    void setBadge(const QString& badge);

signals:
    void activated();

protected:
    void paintEvent(QPaintEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    void restyle();

    QString m_text;
    QString m_badge;
    bool    m_selected = false;
    bool    m_hover = false;
    ElaText* m_glyph = nullptr;
    ElaText* m_label = nullptr;
    ElaText* m_badgeText = nullptr;
};

/// 垂直分类导航（单选）。
class NavList : public QWidget
{
    Q_OBJECT
public:
    explicit NavList(QWidget* parent = nullptr);

    NavItem* addItem(ElaIconType::IconName icon, const QString& text);
    int currentIndex() const { return m_current; }
    void setCurrentIndex(int index);
    int pageCount() const { return m_items.size(); }

signals:
    void currentChanged(int index);

private:
    void applySelection(int index);

    QList<NavItem*> m_items;
    QVBoxLayout*    m_layout = nullptr;
    int             m_current = -1;
};

// ---------------------------------------------------------------------------
// 设置页容器
// ---------------------------------------------------------------------------

/// 高级设置折叠区：默认收起，把 temperature/top_p/惩罚项等细粒度参数
/// 收进一行标题之后，让普通用户不必面对这些选项。展开状态不持久化（每次进入
/// 页面都收起），符合「默认不打扰」的预期。
class AdvancedSection : public QWidget
{
    Q_OBJECT
public:
    explicit AdvancedSection(const QString& title, const QString& description = {},
                             QWidget* parent = nullptr);

    /// 向折叠区内部追加一行设置（收起时不显示）。
    void addRow(QWidget* row);
    bool isExpanded() const { return m_expanded; }
    /// 展开/收起；用于自动化测试或从别处联动。
    void setExpanded(bool expanded);

signals:
    void expandedChanged(bool expanded);

protected:
    /// 整行可点：QWidget 没有 clicked 信号，这里接管标题行的鼠标事件。
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void rebuildBodyVisibility();

    QWidget*     m_header = nullptr;
    QWidget*     m_body = nullptr;
    QVBoxLayout* m_bodyLayout = nullptr;
    ElaText*     m_chevron = nullptr;
    bool         m_expanded = false;
};

/// 可滚动的设置页：内部按「分区标题 + 设置行」组织，统一页边距与行距。
class SettingsPage : public QWidget
{
    Q_OBJECT
public:
    SettingsPage(const QString& title, const QString& subtitle,
                 QWidget* parent = nullptr);

    /// @brief 添加分区标题，返回容器以便与从属设置一起显隐。
    QWidget* addSection(const QString& title, const QString& description = {});
    /// 追加内容块；默认由布局提供行距，避免卡片隐藏后留下固定空隙。
    void addBlock(QWidget* widget, int bottomSpacing = 0);
    /// 追加一块说明文字。
    void addNote(const QString& text);

    QVBoxLayout* bodyLayout() const { return m_body; }
    ElaScrollArea* scrollArea() const { return m_scroll; }

private:
    QVBoxLayout* m_body = nullptr;
    ElaScrollArea* m_scroll = nullptr;
};

// ---------------------------------------------------------------------------
// 其它
// ---------------------------------------------------------------------------

/// 把 QKeySequence 转成人可读的本地化文本（用于热键展示）。
QString hotkeyText(const QString& keySequence);

/// 生成应用图标（圆角渐变 + 取景框 + 「文」字）。
QPixmap makeAppIcon(int size = 96);

// ---------------------------------------------------------------------------
// 主题（外观）
// ---------------------------------------------------------------------------

/// 应用外观：跟随系统 / 强制浅色 / 强制深色。
enum class ThemeMode { System, Light, Dark };

ThemeMode themeModeFromString(const QString& value);
QString   themeModeToString(ThemeMode mode);

/// 系统当前是否偏好深色（读 Qt 的 colorScheme，Unknown 视为浅色）。
bool systemPrefersDark();

/// 应用外观设置。System 会跟随系统深浅色，并在系统切换时自动更新；
/// 同时刷新 ElaTheme（影响所有 Ela 组件）与 Qt 原生控件的全局调色板。
void applyThemeMode(ThemeMode mode);

ThemeMode currentThemeMode();

/// 把 Qt 原生控件的全局调色板对齐到 ElaTheme 主题色。
///
/// 纯 Qt 控件（QKeySequenceEdit / QFileDialog / QMessageBox 等）会跟随系统
/// 调色板 —— Windows 深色模式下即为深色底，与应用主题割裂。
/// `applyThemeMode()` 会自动调用本函数，通常无需单独调用。
void applyAppPalette();

/// 统一浮层（ToolTip / Splitter 手柄）样式。
void polishPopup(QWidget* popup);

/// 主题变化广播器。
///
/// ElaAppBar 自带的标题栏主题按钮会直接调用 `ElaTheme::setThemeMode()`，
/// 绕过 `applyThemeMode()`。订阅本信号即可在那种情况下同步自己的状态
/// （例如把「跟随系统」改判为显式浅色/深色），或用 QObject 连接自动解绑。
class ThemeNotifier : public QObject
{
    Q_OBJECT
public:
    static ThemeNotifier* instance();

signals:
    /// 当前生效的外观模式发生变化（含标题栏按钮触发的显式切换）。
    void themeModeChanged(SnapUi::ThemeMode mode);

private:
    using QObject::QObject;
    friend void applyThemeMode(ThemeMode);
};

} // namespace SnapUi

#endif // SNAPLENS_UI_SNAPKIT_H
