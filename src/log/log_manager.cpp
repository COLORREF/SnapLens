#include "log_manager.h"

#include <QDir>
#include <QFileInfo>
#include <QLockFile>
#include <QStandardPaths>
#include <spdlog/logger.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_sinks.h>
#ifdef Q_OS_WIN
#include <spdlog/sinks/msvc_sink.h>
#endif
#include <cstdio>
#include <vector>

namespace {
/** @brief 后端故障的最终输出路径，不调用 Qt 或 spdlog，防止递归。 */
void emergencyWrite(const char* message) noexcept
{
    std::fprintf(stderr, "[SnapLens logging] %s\n", message);
    std::fflush(stderr);
}

spdlog::level::level_enum backendLevel(LogLevel level)
{
    switch (level) {
    case LogLevel::Debug: return spdlog::level::debug;
    case LogLevel::Info: return spdlog::level::info;
    case LogLevel::Warning: return spdlog::level::warn;
    case LogLevel::Error: return spdlog::level::err;
    case LogLevel::Fatal: return spdlog::level::critical;
    }
    return spdlog::level::err;
}
} // namespace

struct LogManager::Backend {
    // 先关闭 logger 的文件句柄，再释放跨进程锁，避免多个实例同时轮转。
    std::unique_ptr<QLockFile> fileLock;
    std::shared_ptr<spdlog::logger> logger;
    bool fileAvailable = false;
};

LogManager& LogManager::instance()
{
    // 桥接对象保持进程级寿命，避免分离的 OCR 线程在静态析构期间访问失效对象。
    // 文件及 spdlog 资源由 LogSession::shutdown 显式释放。
    static auto* manager = new LogManager;
    return *manager;
}

bool LogManager::init(const LogOptions& options)
{
    std::lock_guard<std::mutex> lock(m_lifecycleMutex);
    if (const auto current = std::atomic_load(&m_backend)) return current->fileAvailable;

    auto backend = std::make_shared<Backend>();
    std::vector<spdlog::sink_ptr> sinks;
    QString filePath = options.filePath;
    if (filePath.isEmpty()) {
        filePath = QDir(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation))
                       .filePath("SnapLens/logs/snaplens.log");
    }
    try {
        if (!QDir().mkpath(QFileInfo(filePath).absolutePath()))
            throw spdlog::spdlog_ex("Cannot create log directory");
        backend->fileLock = std::make_unique<QLockFile>(filePath + ".lock");
        backend->fileLock->setStaleLockTime(0);
        if (!backend->fileLock->tryLock())
            throw spdlog::spdlog_ex("Log file is locked by another process");
#ifdef Q_OS_WIN
        const auto filename = filePath.toStdWString();
#else
        const auto filename = filePath.toStdString();
#endif
        if (options.truncate) {
            sinks.push_back(std::make_shared<spdlog::sinks::basic_file_sink_mt>(filename, true));
        } else {
            sinks.push_back(std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
                filename, options.maxFileBytes, options.maxFiles));
        }
        backend->fileAvailable = true;
    } catch (const std::exception& error) {
        backend->fileLock.reset();
        emergencyWrite(error.what());
    }
#ifdef Q_OS_WIN
    if (options.debugOutput) {
        sinks.push_back(std::make_shared<spdlog::sinks::msvc_sink_mt>());
    }
#endif
    // 调试器输出与 stderr 是两个独立目标；IDE 的运行控制台通常捕获后者。
    if (options.consoleOutput || !backend->fileAvailable)
        sinks.push_back(std::make_shared<spdlog::sinks::stderr_sink_mt>());

    backend->logger = std::make_shared<spdlog::logger>("snap", sinks.begin(), sinks.end());
    backend->logger->set_level(spdlog::level::debug);
    backend->logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%n %l] [thread %t] [%s:%# %!] %v");
    backend->logger->set_error_handler([](const std::string& error) { emergencyWrite(error.c_str()); });
    backend->logger->flush_on(spdlog::level::warn);
    std::atomic_store(&m_backend, backend);
    m_previousHandler = qInstallMessageHandler(qtMessageHandler);
    return backend->fileAvailable;
}

void LogManager::shutdown() noexcept
{
    std::lock_guard<std::mutex> lock(m_lifecycleMutex);
    if (!std::atomic_load(&m_backend)) return;
    qInstallMessageHandler(m_previousHandler);
    m_previousHandler = nullptr;
    auto backend = std::atomic_exchange(&m_backend, std::shared_ptr<Backend>{});
    try { backend->logger->flush(); }
    catch (const std::exception& error) { emergencyWrite(error.what()); }
}

void LogManager::flush() noexcept
{
    if (const auto backend = std::atomic_load(&m_backend)) {
        try { backend->logger->flush(); }
        catch (const std::exception& error) { emergencyWrite(error.what()); }
    }
}

bool LogManager::isEnabled(LogLevel level) const noexcept
{
    return level == LogLevel::Fatal ||
           (m_enabled.load(std::memory_order_relaxed) & (1u << static_cast<unsigned>(level))) != 0;
}

void LogManager::applySettings(bool debug, bool info, bool warning, bool error) noexcept
{
    const unsigned mask = unsigned(debug) | (unsigned(info) << 1) |
                          (unsigned(warning) << 2) | (unsigned(error) << 3);
    m_enabled.store(mask, std::memory_order_relaxed);
}

void LogManager::write(LogLevel level, const char* file, int line, const char* function,
                       const QString& message) noexcept
{
    if (!isEnabled(level)) return;
    try {
        const auto utf8 = message.toUtf8();
        if (const auto backend = std::atomic_load(&m_backend)) {
            backend->logger->log(spdlog::source_loc{file ? file : "", line, function ? function : ""},
                                 backendLevel(level), "{}",
                                 spdlog::string_view_t(utf8.constData(), std::size_t(utf8.size())));
        } else {
            emergencyWrite(utf8.constData());
        }
    } catch (const std::exception& error) {
        emergencyWrite(error.what());
    }
}

void LogManager::qtMessageHandler(QtMsgType type, const QMessageLogContext& context,
                                  const QString& message) noexcept
{
    LogLevel level = LogLevel::Info;
    switch (type) {
    case QtDebugMsg: level = LogLevel::Debug; break;
    case QtInfoMsg: level = LogLevel::Info; break;
    case QtWarningMsg: level = LogLevel::Warning; break;
    case QtCriticalMsg: level = LogLevel::Error; break;
    case QtFatalMsg: level = LogLevel::Fatal; break;
    }
    // Fatal 同步写入并由 flush_on 刷新；返回后 Qt 执行其正常终止流程。
    instance().write(level, context.file, context.line, context.function, message);
}
