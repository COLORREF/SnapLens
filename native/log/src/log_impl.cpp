// log_impl.cpp - 日志模块实现
//
// 通过 qSetMessagePattern("%{message}") 去掉 Qt 默认前缀，
// 在 write() 中将 "[snap LEVEL file:line] msg" 拼接好，
// 直接交给 qDebug/qInfo/qWarning/qCritical 输出。
// Qt 默认机制负责所有平台的编码适配。
//
#include "log_impl.h"

#include <QDebug>
#include <cstring>

namespace {

const char* levelName(int level) {
    switch (level) {
        case 0: return "DEBUG";
        case 1: return "INFO";
        case 2: return "WARN";
        case 3: return "ERROR";
        default: return "????";
    }
}

const char* shortFileName(const char* path) {
    if (!path) return "?";
    if (const char* p = strrchr(path, '\\')) return p + 1;
    if (const char* p = strrchr(path, '/'))  return p + 1;
    return path;
}

}  // namespace

LogManager& LogManager::instance() {
    static LogManager mgr;
    return mgr;
}

void LogManager::init() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (initialized_) return;
    qSetMessagePattern("%{message}");
    initialized_ = true;
}

void LogManager::shutdown() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!initialized_) return;
    qSetMessagePattern("");
    initialized_ = false;
}

void LogManager::write(int level, const char* file, int line,
                        const char* function, const QString& message) {
    (void)function;

    QString formatted = QStringLiteral("[snap %1 %2:%3] %4")
                            .arg(QLatin1StringView(levelName(level)),
                                 QLatin1StringView(shortFileName(file)))
                            .arg(line)
                            .arg(message);

    switch (level) {
        case 0:  // SNAP_LOG_LEVEL_DEBUG
            qDebug() << formatted;
            break;
        case 1:  // SNAP_LOG_LEVEL_INFO
            qInfo() << formatted;
            break;
        case 2:  // SNAP_LOG_LEVEL_WARNING
            qWarning() << formatted;
            break;
        case 3:  // SNAP_LOG_LEVEL_ERROR
            qCritical() << formatted;
            break;
        default:
            qDebug() << formatted;
            break;
    }
}
