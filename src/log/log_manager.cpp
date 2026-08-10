#include "log_manager.h"

#include <QDebug>
#include <QFileInfo>

// ── Singleton ────────────────────────────────────────────────────────

LogManager &LogManager::instance()
{
    static LogManager inst;
    return inst;
}

// ── init / shutdown ──────────────────────────────────────────────────

void LogManager::init()
{
    QMutexLocker lock(&m_mutex);

    qSetMessagePattern("%{message}");
    m_initialized = true;
}

void LogManager::shutdown()
{
    QMutexLocker lock(&m_mutex);

    qSetMessagePattern("%{if-category}%{category}: %{endif}%{message}");
    m_initialized = false;
}

// ── write ────────────────────────────────────────────────────────────

void LogManager::write(int level, const char *file, int line,
                       const char *function, const QString &message)
{
    QMutexLocker lock(&m_mutex);

    // Level-based filtering
    switch (level) {
    case SNAP_LOG_LEVEL_DEBUG:   if (!m_debugEnabled)   return; break;
    case SNAP_LOG_LEVEL_INFO:    if (!m_infoEnabled)    return; break;
    case SNAP_LOG_LEVEL_WARNING: if (!m_warningEnabled) return; break;
    case SNAP_LOG_LEVEL_ERROR:   if (!m_errorEnabled)   return; break;
    default: break;
    }

    const char *levelStr = nullptr;
    switch (level) {
    case SNAP_LOG_LEVEL_DEBUG:   levelStr = "DEBUG";   break;
    case SNAP_LOG_LEVEL_INFO:    levelStr = "INFO";    break;
    case SNAP_LOG_LEVEL_WARNING: levelStr = "WARNING"; break;
    case SNAP_LOG_LEVEL_ERROR:   levelStr = "ERROR";   break;
    default:                     levelStr = "UNKNOWN";  break;
    }

    QString fn = QString::fromUtf8(function);
    // Truncate long C++ function signatures to the short name
    int parenIdx = fn.indexOf('(');
    if (parenIdx > 0) {
        int spaceIdx = fn.lastIndexOf(' ', parenIdx);
        if (spaceIdx >= 0) fn = fn.mid(spaceIdx + 1, parenIdx - spaceIdx - 1);
        else fn = fn.left(parenIdx);
    }

    const QString formatted = QString("[snap %1 %2:%3 %4] %5")
                                  .arg(QLatin1String(levelStr),
                                       QLatin1String(shortFileName(file)))
                                  .arg(line)
                                  .arg(fn)
                                  .arg(message);

    switch (level) {
    case SNAP_LOG_LEVEL_DEBUG:   qDebug()    << formatted; break;
    case SNAP_LOG_LEVEL_INFO:    qInfo()     << formatted; break;
    case SNAP_LOG_LEVEL_WARNING: qWarning()  << formatted; break;
    case SNAP_LOG_LEVEL_ERROR:   qCritical() << formatted; break;
    default:                     qDebug()    << formatted; break;
    }
}

// ── level control ───────────────────────────────────────────────────

void LogManager::setEnabledLevels(bool debug, bool info, bool warning, bool error)
{
    QMutexLocker lock(&m_mutex);
    m_debugEnabled = debug;
    m_infoEnabled = info;
    m_warningEnabled = warning;
    m_errorEnabled = error;
}

void LogManager::applySettings(bool debug, bool info, bool warning, bool error)
{
    setEnabledLevels(debug, info, warning, error);
}

// ── helper ───────────────────────────────────────────────────────────

const char *LogManager::shortFileName(const char *filePath)
{
    const char *p = filePath;
    while (*p) ++p;

    while (p > filePath) {
        --p;
        if (*p == '/' || *p == '\\')
            return p + 1;
    }
    return filePath;
}