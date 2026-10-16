#ifndef SNAPLENS_LOG_LOG_MANAGER_H
#define SNAPLENS_LOG_LOG_MANAGER_H

#include <QString>
#include <QtLogging>
#include <atomic>
#include <cstddef>
#include <memory>
#include <mutex>

/** @brief 应用日志级别；Fatal 始终输出，不受用户开关影响。 */
enum class LogLevel : unsigned { Debug, Info, Warning, Error, Fatal };

/** @brief 同步日志配置；默认使用用户本地数据目录下的 SnapLens/logs/snaplens.log。 */
struct LogOptions {
    QString filePath;
    std::size_t maxFileBytes = 5 * 1024 * 1024;
    std::size_t maxFiles = 3; ///< 历史文件数，不包含当前文件。
    bool truncate = false; ///< 回归模式使用单个覆盖写入的文件。
    bool consoleOutput = true; ///< 输出到 stderr，支持终端、IDE 控制台和重定向。
    bool debugOutput = true; ///< 额外输出到 Windows 调试器。
};

/**
 * @brief Qt/QString 与 spdlog 之间的适配层，保留四级独立开关。
 * @details 写入和过滤支持并发调用；初始化及关闭应在主线程执行。
 * 后端快照拥有共享所有权，关闭时已开始的写入仍可安全完成。
 * 未初始化或关闭后的消息降级到 stderr，禁止通过 Qt 日志回报后端错误。
 */
class LogManager final {
public:
    static LogManager& instance();
    /** @brief 安装 Qt 桥接并打开输出；文件失败时保留诊断输出。
     * @return 文件输出是否可用。重复初始化不会替换当前后端。
     */
    bool init(const LogOptions& options = {});
    /** @brief 恢复原 Qt 处理器并释放后端；允许重复调用。 */
    void shutdown() noexcept;
    /** @brief 刷新当前后端，用于回归验证及退出前落盘。 */
    void flush() noexcept;
    bool isEnabled(LogLevel level) const noexcept;
    /** @brief 原子替换四级开关，保持“DEBUG 开、INFO 关”等组合语义。 */
    void applySettings(bool debug, bool info, bool warning, bool error) noexcept;
    /** @brief 写入 UTF-8 消息及调用位置，不向业务代码传播后端异常。 */
    void write(LogLevel level, const char* file, int line, const char* function,
               const QString& message) noexcept;

private:
    struct Backend;
    LogManager() = default;
    static void qtMessageHandler(QtMsgType type, const QMessageLogContext& context,
                                 const QString& message) noexcept;
    std::atomic<unsigned> m_enabled{0x0f};
    std::shared_ptr<Backend> m_backend;
    std::mutex m_lifecycleMutex;
    QtMessageHandler m_previousHandler = nullptr;
};

/** @brief 覆盖整个 QApplication 生命周期，保证提前返回路径也会关闭日志。 */
class LogSession final {
public:
    explicit LogSession(const LogOptions& options = {}) { LogManager::instance().init(options); }
    ~LogSession() { LogManager::instance().shutdown(); }
    LogSession(const LogSession&) = delete;
    LogSession& operator=(const LogSession&) = delete;
};

// 在求值消息表达式前过滤，关闭的 DEBUG 不再执行 QString::arg 等操作。
#define SNAP_LOG_WRITE(level, message) \
    do { \
        if (LogManager::instance().isEnabled(level)) \
            LogManager::instance().write(level, __FILE__, __LINE__, __FUNCTION__, (message)); \
    } while (false)
#define SNAP_LOG_DEBUG(message) SNAP_LOG_WRITE(LogLevel::Debug, message)
#define SNAP_LOG_INFO(message) SNAP_LOG_WRITE(LogLevel::Info, message)
#define SNAP_LOG_WARNING(message) SNAP_LOG_WRITE(LogLevel::Warning, message)
#define SNAP_LOG_ERROR(message) SNAP_LOG_WRITE(LogLevel::Error, message)

#endif // SNAPLENS_LOG_LOG_MANAGER_H
