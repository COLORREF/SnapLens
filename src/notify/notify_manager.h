#ifndef SNAPLENS_NOTIFY_NOTIFY_MANAGER_H
#define SNAPLENS_NOTIFY_NOTIFY_MANAGER_H

#include <QFlags>
#include <QString>
#include <QVector>

#include "../settings.h"  // 完整定义（NotifyDef::setting 为成员指针，需完整类型）

// 前向声明
class QSystemTrayIcon;
class QWidget;
class QDialog;

// ============================================================================
// Channel — 通知分发通道（位标志，支持组合）
// ============================================================================

enum class Channel {
    Tray   = 1 << 0,  // 系统托盘通知（QSystemTrayIcon::showMessage）
    Dialog = 1 << 1,  // Ela 提示界面
    Log    = 1 << 2,  // spdlog 日志（SNAP_LOG_* 宏）
};

Q_DECLARE_FLAGS(Channels, Channel)
Q_DECLARE_OPERATORS_FOR_FLAGS(Channels)

// ============================================================================
// NotifyLevel — 通知级别
// ============================================================================

enum class NotifyLevel {
    Info,     // 一般信息
    Warning,  // 警告
    Error,    // 错误
};

// ============================================================================
// NotifyDef — 单个通知类型定义
// ============================================================================

struct NotifyDef {
    QString       id;       // 通知类型唯一标识（如 "save_success"）
    bool Settings::*setting = nullptr; // 关联的 Settings 开关成员（nullptr = 无开关控制，始终发送）
    Channels      channels; // 默认分发通道
    NotifyLevel   level;    // 通知级别
};

// ============================================================================
// NOTIFY_DEFS — 所有通知类型注册表（唯一来源）
// ============================================================================
// 新增通知类型只需在此处添加一行；设置开关直接绑定 Settings 成员指针，
// 不存在字符串 key 与字段名双轨问题（对齐 Python `getattr(settings, ndef.setting)`）。

inline const QVector<NotifyDef> NOTIFY_DEFS = {
    // ---- 截图结果 ----
    {"save_success", &Settings::notify_save_success,
     Channel::Tray | Channel::Log, NotifyLevel::Info},
    {"save_fail", &Settings::notify_save_fail,
     Channel::Tray | Channel::Log, NotifyLevel::Warning},
    {"copy", &Settings::notify_copy,
     Channel::Tray | Channel::Log, NotifyLevel::Info},

    // ---- 截图异常 ----
    {"capture_fail", &Settings::notify_capture_fail,
     Channel::Tray | Channel::Log, NotifyLevel::Warning},
    {"hotkey_fail", &Settings::notify_hotkey_fail,
     Channel::Tray | Channel::Log, NotifyLevel::Warning},

    // ---- AI 翻译 ----
    {"translate_success", &Settings::notify_translate_success,
     Channel::Tray | Channel::Log, NotifyLevel::Info},
    {"translate_fail", &Settings::notify_translate_fail,
     Channel::Tray | Channel::Log, NotifyLevel::Error},

    // ---- OCR 识别 ----
    {"ocr_fail", &Settings::notify_ocr_fail,
     Channel::Tray | Channel::Log, NotifyLevel::Warning},

    // ---- OCR 语言包下载 ----
    {"lang_download_success", &Settings::notify_lang_download,
     Channel::Tray, NotifyLevel::Info},
    {"lang_download_fail", &Settings::notify_lang_download,
     Channel::Tray | Channel::Log, NotifyLevel::Warning},

    // ---- 配置生效提示（无开关，始终提示）----
    {"settings_deferred", nullptr,
     Channel::Tray | Channel::Log, NotifyLevel::Info},
};

// ============================================================================
// NotifyManager — 通知系统统一入口
// ============================================================================

/** @brief 按通知注册表分发日志、托盘和界面提示，仅在 GUI 线程调用。 */
class NotifyManager {
public:
    NotifyManager() = default;

    // ---- 绑定外部依赖 ----
    void setSettings(Settings* settings);
    void setTray(QSystemTrayIcon* tray);
    void setDefaultParent(QWidget* parent);

    // ---- 设置控制的通知 ----
    void notify(const QString& notifyId, const QString& title,
                const QString& message);

    // ---- 不受设置控制的弹窗 ----
    bool confirm(const QString& title, const QString& message,
                 QWidget* parent = nullptr);
    void warn(const QString& title, const QString& message,
              QWidget* parent = nullptr);
    void info(const QString& title, const QString& message,
              QWidget* parent = nullptr);

    // ---- 自定义弹窗 ----
    int askCustom(QDialog* dialog);

private:
    // 根据 NotifyDef 中的 setting 字段查询 Settings 中的对应 bool 值
    bool checkSettingEnabled(const NotifyDef& ndef) const;

    // 通道分发实现
    void sendToLog(NotifyLevel level, const QString& title, const QString& message);
    void sendToTray(NotifyLevel level, const QString& title, const QString& message);
    void sendToDialog(NotifyLevel level, const QString& title, const QString& message);

    Settings*         m_settings       = nullptr;
    QSystemTrayIcon*  m_tray           = nullptr;
    QWidget*          m_defaultParent  = nullptr;
    QString           m_activeDedupKey; // 当前活跃弹窗的去重 key
};

#endif // SNAPLENS_NOTIFY_NOTIFY_MANAGER_H
