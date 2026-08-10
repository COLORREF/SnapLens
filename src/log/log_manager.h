#ifndef SNAPLENS_LOG_LOG_MANAGER_H
#define SNAPLENS_LOG_LOG_MANAGER_H

#include <QString>
#include <QMutex>
#include <QMutexLocker>

// ── Log Level Constants ──────────────────────────────────────────────

#define SNAP_LOG_LEVEL_DEBUG   0
#define SNAP_LOG_LEVEL_INFO    1
#define SNAP_LOG_LEVEL_WARNING 2
#define SNAP_LOG_LEVEL_ERROR   3

// ── LogManager Singleton ─────────────────────────────────────────────

class LogManager
{
public:
    static LogManager &instance();

    void init();
    void shutdown();
    void write(int level, const char *file, int line, const char *function,
               const QString &message);

    // Runtime level control (matching Python DLL's set_enabled_levels)
    void setEnabledLevels(bool debug, bool info, bool warning, bool error);
    void applySettings(bool debug, bool info, bool warning, bool error);

private:
    LogManager() = default;
    ~LogManager() = default;
    LogManager(const LogManager &) = delete;
    LogManager &operator=(const LogManager &) = delete;

    static const char *shortFileName(const char *filePath);

    QMutex m_mutex;
    bool   m_initialized = false;
    bool   m_debugEnabled   = true;
    bool   m_infoEnabled    = true;
    bool   m_warningEnabled = true;
    bool   m_errorEnabled   = true;
};

// ── Convenience Macros ───────────────────────────────────────────────

#define SNAP_LOG_DEBUG(msg) \
    LogManager::instance().write(SNAP_LOG_LEVEL_DEBUG, \
                                 __FILE__, __LINE__, __FUNCTION__, msg)

#define SNAP_LOG_INFO(msg) \
    LogManager::instance().write(SNAP_LOG_LEVEL_INFO, \
                                 __FILE__, __LINE__, __FUNCTION__, msg)

#define SNAP_LOG_WARNING(msg) \
    LogManager::instance().write(SNAP_LOG_LEVEL_WARNING, \
                                 __FILE__, __LINE__, __FUNCTION__, msg)

#define SNAP_LOG_ERROR(msg) \
    LogManager::instance().write(SNAP_LOG_LEVEL_ERROR, \
                                 __FILE__, __LINE__, __FUNCTION__, msg)

#endif // SNAPLENS_LOG_LOG_MANAGER_H