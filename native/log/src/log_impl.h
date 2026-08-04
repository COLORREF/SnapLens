// log_impl.h - 日志模块内部实现头
//
// LogManager 是线程安全的单例。
// 不安装自定义 Qt 消息处理器，而是设置 qSetMessagePattern("%{message}")
// 直接将格式化好的 "[snap LEVEL file:line] msg" 交给 qDebug/qWarning 等输出，
// 由 Qt 默认机制处理所有平台的编码适配。
//
#ifndef SNAP_LOG_IMPL_H
#define SNAP_LOG_IMPL_H

#include <QtGlobal>
#include <QString>
#include <mutex>

class LogManager {
public:
    static LogManager& instance();

    void init();
    void shutdown();

    // 线程安全：write() 内部有 mutex
    void write(int level, const char* file, int line,
               const char* function, const QString& message);

private:
    LogManager() = default;
    ~LogManager() = default;
    LogManager(const LogManager&) = delete;
    LogManager& operator=(const LogManager&) = delete;

    std::mutex mutex_;
    bool initialized_ = false;
};

#endif  // SNAP_LOG_IMPL_H
