// snaplens_log.h - SnapLens 统一日志模块 C ABI 公共接口
//
// 该头文件定义了 snaplens_log.dll 导出的所有 C 函数，
// 供 Python ctypes 调用，也供 snaplens_platform / snaplens_ai / snaplens_ocr 直接链接。
//
// 日志等级定义与 Python logging / Qt QMessageLogger 对齐。
// 内部使用 qDebug + 自定义 Qt 消息处理器实现格式化输出到 stderr。
//
// 线程安全：snap_log_write 内部有 mutex 保护，可跨线程调用。
//
#ifndef SNAPLENS_LOG_H
#define SNAPLENS_LOG_H

#ifdef SNAP_LOG_EXPORTS
    #define SNAP_LOG_API __declspec(dllexport)
#else
    #define SNAP_LOG_API __declspec(dllimport)
#endif

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// 日志等级
// ============================================================================
#define SNAP_LOG_LEVEL_DEBUG   0
#define SNAP_LOG_LEVEL_INFO    1
#define SNAP_LOG_LEVEL_WARNING 2
#define SNAP_LOG_LEVEL_ERROR   3

// ============================================================================
// 生命周期
// ============================================================================

// 初始化日志系统（设置 qSetMessagePattern，安装 Qt 消息处理器）
// 返回：0 成功
SNAP_LOG_API int snap_log_init(void);

// 关闭日志系统（恢复 Qt 默认消息格式）
SNAP_LOG_API void snap_log_shutdown(void);

// ============================================================================
// 写日志
// ============================================================================

// 写入一条日志（变参版本，C++ 侧通过宏使用）。
SNAP_LOG_API void snap_log_write(int level, const char* file, int line,
                                   const char* function, const char* fmt, ...);

// 写入一条日志（非变参版本，专供 Python ctypes 等不支持变参调用的语言使用）。
SNAP_LOG_API void snap_log_write_msg(int level, const char* file, int line,
                                       const char* function, const char* message);

#ifdef __cplusplus
}  // extern "C"
#endif

// ============================================================================
// C++ 便捷宏（仅供 C++ 调用方使用）
// ============================================================================
#ifdef __cplusplus
#define SNAP_LOG_DEBUG(fmt, ...) \
    snap_log_write(SNAP_LOG_LEVEL_DEBUG, __FILE__, __LINE__, __FUNCTION__, fmt, ##__VA_ARGS__)

#define SNAP_LOG_INFO(fmt, ...) \
    snap_log_write(SNAP_LOG_LEVEL_INFO, __FILE__, __LINE__, __FUNCTION__, fmt, ##__VA_ARGS__)

#define SNAP_LOG_WARNING(fmt, ...) \
    snap_log_write(SNAP_LOG_LEVEL_WARNING, __FILE__, __LINE__, __FUNCTION__, fmt, ##__VA_ARGS__)

#define SNAP_LOG_ERROR(fmt, ...) \
    snap_log_write(SNAP_LOG_LEVEL_ERROR, __FILE__, __LINE__, __FUNCTION__, fmt, ##__VA_ARGS__)
#endif

#endif  // SNAPLENS_LOG_H
