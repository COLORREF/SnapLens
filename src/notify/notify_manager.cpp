#include "notify_manager.h"

#include "../settings.h"
#include "../log/log_manager.h"

#include <QSystemTrayIcon>
#include <QMessageBox>
#include <QDialog>
#include <QWidget>

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

void NotifyManager::sendToDialog(NotifyLevel level, const QString& title,
                                 const QString& message)
{
    QString dedupKey = QStringLiteral("notify:") + title;
    if (m_activeDedupKey == dedupKey)
        return;
    m_activeDedupKey = dedupKey;

    QMessageBox::Icon icon = QMessageBox::Information;
    switch (level) {
    case NotifyLevel::Info:    icon = QMessageBox::Information; break;
    case NotifyLevel::Warning: icon = QMessageBox::Warning;     break;
    case NotifyLevel::Error:   icon = QMessageBox::Critical;    break;
    }

    QMessageBox msgBox(icon, title, message, QMessageBox::Ok, m_defaultParent);
    msgBox.exec();

    if (m_activeDedupKey == dedupKey)
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

    // 多通道分发
    Channels channels = ndef->channels;

    if (channels.testFlag(Channel::Log)) {
        bool logEnabled = !m_settings
            || m_settings->log_debug_enabled
            || m_settings->log_info_enabled
            || m_settings->log_warning_enabled
            || m_settings->log_error_enabled;
        if (logEnabled)
            sendToLog(ndef->level, title, message);
    }

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
    QWidget* p = parent ? parent : m_defaultParent;
    QMessageBox::StandardButton reply = QMessageBox::question(
        p, title, message,
        QMessageBox::Yes | QMessageBox::No);
    return reply == QMessageBox::Yes;
}

void NotifyManager::warn(const QString& title, const QString& message,
                         QWidget* parent)
{
    QWidget* p = parent ? parent : m_defaultParent;
    QMessageBox::warning(p, title, message);
}

void NotifyManager::info(const QString& title, const QString& message,
                         QWidget* parent)
{
    QWidget* p = parent ? parent : m_defaultParent;
    QMessageBox::information(p, title, message);
}

int NotifyManager::askCustom(QDialog* dialog)
{
    return dialog->exec();
}