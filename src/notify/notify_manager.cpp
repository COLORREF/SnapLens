#include "notify_manager.h"

#include "../settings.h"
#include "../log/log_manager.h"
#include "../ui/snapkit.h"

#include <QSystemTrayIcon>
#include <QApplication>
#include <QHBoxLayout>
#include <QVBoxLayout>

#include "ElaWidgetTools.hpp"
#include <QDialog>
#include <QWidget>
#include <QScrollArea>
#include <QScreen>

// ============================================================================
// NotifyManager 实现
// ============================================================================

void NotifyManager::setSettings(Settings* settings)
{
    m_settings = settings;
}

void NotifyManager::setTray(QSystemTrayIcon* tray)
{
    m_tray = tray;
}

void NotifyManager::setDefaultParent(QWidget* parent)
{
    m_defaultParent = parent;
}

// ============================================================================
// 通道分发
// ============================================================================

void NotifyManager::sendToLog(NotifyLevel level, const QString& title,
                              const QString& message)
{
    QString fullMsg = QStringLiteral("[Notify] %1: %2").arg(title, message);
    switch (level) {
    case NotifyLevel::Info:
        SNAP_LOG_INFO(fullMsg);
        break;
    case NotifyLevel::Warning:
        SNAP_LOG_WARNING(fullMsg);
        break;
    case NotifyLevel::Error:
        SNAP_LOG_ERROR(fullMsg);
        break;
    }
}

void NotifyManager::sendToTray(NotifyLevel level, const QString& title,
                               const QString& message)
{
    if (!m_tray) {
        SNAP_LOG_INFO(QStringLiteral("[Notify][FALLBACK] tray 未绑定，降级为日志: %1: %2")
                      .arg(title, message));
        return;
    }

    QSystemTrayIcon::MessageIcon icon = QSystemTrayIcon::Information;
    switch (level) {
    case NotifyLevel::Info:    icon = QSystemTrayIcon::Information; break;
    case NotifyLevel::Warning: icon = QSystemTrayIcon::Warning;     break;
    case NotifyLevel::Error:   icon = QSystemTrayIcon::Critical;    break;
    }

    m_tray->showMessage(title, message, icon, 3000);
}

// ============================================================================
// 弹窗（全部使用 ElaWidgetTools，不使用 Qt 原生 QMessageBox）
//
// QMessageBox 在本应用里观感割裂：它跟随系统调色板而不是 ElaTheme，
// 在系统深色模式 + 应用浅色主题的组合下会出现「浅色底 + 浅色字」的
// 不可读情况（按钮文字直接看不见）。因此：
//   · 需要用户决策 → ElaContentDialog（WinUI3 ContentDialog 形态，模态 + 遮罩）
//   · 纯提示/警告   → ElaMessageBar（WinUI3 InfoBar 形态，非阻塞）
// ============================================================================

namespace {

/// 取一个可用于承载模态对话框的窗口：显式 parent → 默认 parent → 当前活动窗口。
QWidget* dialogHost(QWidget* explicitParent, QWidget* fallback)
{
    QWidget* host = explicitParent ? explicitParent : fallback;
    if (!host) host = QApplication::activeWindow();
    return host ? host->window() : nullptr;
}

/// 统一构造一个「标题 + 正文 + 取消/确定」的 ContentDialog。
/// 返回 true 表示用户点了右侧主按钮。
bool runContentDialog(QWidget* host, const QString& title, const QString& message,
                      const QString& acceptText, const QString& cancelText)
{
    if (!host) {
        SNAP_LOG_WARNING("NotifyManager: 没有可用的父窗口，无法弹出确认对话框");
        return false;
    }

    // ElaContentDialog::_doCloseAnimation() 会在 accept/reject 之后再次关闭
    // windowHandle 并 processEvents；栈上对话框 + 嵌套事件循环因此容易重入。
    // 这里保留 Ela 按钮与主题色，但用 Qt 的单次完成语义承载确认流程。
    QDialog dialog(host);
    dialog.setWindowTitle(title);
    dialog.setWindowModality(Qt::WindowModal);
    dialog.setMinimumWidth(440);
    dialog.setMaximumWidth(620);
    dialog.setAttribute(Qt::WA_StyledBackground, true);
    const auto theme = eTheme->getThemeMode();
    dialog.setStyleSheet(QStringLiteral("QDialog { background: %1; border: 1px solid %2; border-radius: 8px; }")
        .arg(ElaThemeColor(theme, DialogBase).name(),
             ElaThemeColor(theme, BasicBorder).name()));

    auto* layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(24, 22, 24, 18);
    layout->setSpacing(16);
    auto* titleText = new ElaText(title, &dialog);
    titleText->setTextPixelSize(19);
    layout->addWidget(titleText);

    auto* scroll = new QScrollArea(&dialog);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setStyleSheet("QScrollArea { background: transparent; }");
    auto* body = new QWidget(scroll);
    body->setAttribute(Qt::WA_TranslucentBackground);
    body->setStyleSheet(QStringLiteral("background: transparent;"));
    auto* bodyLayout = new QVBoxLayout(body);
    bodyLayout->setContentsMargins(0, 0, 4, 0);
    auto* bodyText = new ElaText(message, body);
    bodyText->setTextPixelSize(13);
    bodyText->setIsWrapAnywhere(true);
    bodyText->setTextInteractionFlags(Qt::TextSelectableByMouse);
    bodyText->setMinimumWidth(390);
    bodyText->setMaximumWidth(540);
    bodyLayout->addWidget(bodyText);
    scroll->setWidget(body);
    const int wantedHeight = qMax(50, bodyText->heightForWidth(520) + 20);
    const int availableHeight = host->screen() ? host->screen()->availableGeometry().height() : 800;
    scroll->setFixedHeight(qMin(wantedHeight, qMin(260, availableHeight / 2)));
    layout->addWidget(scroll);

    auto* buttons = new QHBoxLayout();
    buttons->addStretch();
    auto* cancel = SnapUi::secondaryButton(cancelText, &dialog);
    auto* accept = SnapUi::primaryButton(acceptText, &dialog);
    cancel->setMinimumWidth(100);
    accept->setMinimumWidth(100);
    buttons->addWidget(cancel);
    buttons->addWidget(accept);
    layout->addLayout(buttons);
    QObject::connect(cancel, &ElaPushButton::clicked, &dialog, &QDialog::reject);
    QObject::connect(accept, &ElaPushButton::clicked, &dialog, &QDialog::accept);
    return dialog.exec() == QDialog::Accepted;
}

/// 非阻塞提示条（WinUI3 InfoBar）。用于不需要用户决策的提示与警告。
void showInfoBar(QWidget* host, NotifyLevel level,
                 const QString& title, const QString& message, int durationMs)
{
    if (!host) return;
    switch (level) {
    case NotifyLevel::Info:
        ElaMessageBar::information(ElaMessageBarType::TopRight, title, message,
                                   durationMs, host);
        break;
    case NotifyLevel::Warning:
        ElaMessageBar::warning(ElaMessageBarType::TopRight, title, message,
                               durationMs, host);
        break;
    case NotifyLevel::Error:
        ElaMessageBar::error(ElaMessageBarType::TopRight, title, message,
                             durationMs, host);
        break;
    }
}

} // namespace

void NotifyManager::sendToDialog(NotifyLevel level, const QString& title,
                                 const QString& message)
{
    QString dedupKey = QStringLiteral("notify:") + title;
    if (m_activeDedupKey == dedupKey)
        return;
    m_activeDedupKey = dedupKey;

    showInfoBar(dialogHost(nullptr, m_defaultParent), level, title, message, 3200);

    m_activeDedupKey.clear();
}

// ============================================================================
// checkSettingEnabled
// ============================================================================

bool NotifyManager::checkSettingEnabled(const NotifyDef& ndef) const
{
    if (!m_settings)
        return true; // settings 未加载时允许所有通知通过

    if (!ndef.setting)
        return true; // 无关联设置项，始终发送

    // 成员指针直接解引用：NOTIFY_DEFS 中绑定的开关即为唯一来源，
    // 不存在字符串 key 与字段名双轨（对齐 Python `getattr(settings, ndef.setting)`）
    return m_settings->*(ndef.setting);
}

// ============================================================================
// notify
// ============================================================================

void NotifyManager::notify(const QString& notifyId, const QString& title,
                           const QString& message)
{
    // 查找 NotifyDef
    const NotifyDef* ndef = nullptr;
    for (const auto& d : NOTIFY_DEFS) {
        if (d.id == notifyId) {
            ndef = &d;
            break;
        }
    }

    if (!ndef) {
        sendToLog(NotifyLevel::Warning, title,
                  QStringLiteral("%1 [未注册的通知类型: %2]").arg(message, notifyId));
        return;
    }

    // 检查设置开关
    if (!checkSettingEnabled(*ndef))
        return;

    // 分发到各通道
    const Channels channels = ndef->channels;

    if (channels.testFlag(Channel::Log))
        sendToLog(ndef->level, title, message);

    if (channels.testFlag(Channel::Tray))
        sendToTray(ndef->level, title, message);

    if (channels.testFlag(Channel::Dialog))
        sendToDialog(ndef->level, title, message);
}

// ============================================================================
// 不受设置控制的弹窗
// ============================================================================

bool NotifyManager::confirm(const QString& title, const QString& message,
                            QWidget* parent)
{
    return runContentDialog(dialogHost(parent, m_defaultParent), title, message,
                            QStringLiteral("确定"), QStringLiteral("取消"));
}

void NotifyManager::warn(const QString& title, const QString& message,
                         QWidget* parent)
{
    sendToLog(NotifyLevel::Warning, title, message);
    showInfoBar(dialogHost(parent, m_defaultParent), NotifyLevel::Warning,
                title, message, 3600);
}

void NotifyManager::info(const QString& title, const QString& message,
                         QWidget* parent)
{
    showInfoBar(dialogHost(parent, m_defaultParent), NotifyLevel::Info,
                title, message, 2800);
}

int NotifyManager::askCustom(QDialog* dialog)
{
    return dialog->exec();
}
