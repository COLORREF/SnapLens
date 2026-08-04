// dllmain.cpp - DLL 入口 + C ABI 函数实现
//
// 将 LogManager 的 C++ 接口转换为 C ABI，
// 供 Python ctypes 调用，也供其他原生 DLL 直接链接。
//
#include <windows.h>
#include "../include/snaplens_log.h"
#include "log_impl.h"

#include <QString>
#include <cstdio>     // vsnprintf
#include <cstdarg>

// ============================================================================
// DLL 入口 — 不在 DllMain 中做日志输出（Loader Lock 风险）
// ============================================================================
BOOL APIENTRY DllMain(HMODULE, DWORD reason, LPVOID) {
    switch (reason) {
        case DLL_PROCESS_ATTACH:
            LogManager::instance().init();
            break;
        case DLL_PROCESS_DETACH:
            LogManager::instance().shutdown();
            break;
        case DLL_THREAD_ATTACH:
        case DLL_THREAD_DETACH:
            break;
    }
    return TRUE;
}

// ============================================================================
// C ABI 实现
// ============================================================================
extern "C" {

SNAP_LOG_API int snap_log_init(void) {
    LogManager::instance().init();
    SNAP_LOG_DEBUG("snap_log_init()");
    return 0;
}

SNAP_LOG_API void snap_log_shutdown(void) {
    SNAP_LOG_DEBUG("snap_log_shutdown()");
    LogManager::instance().shutdown();
}

SNAP_LOG_API void snap_log_write(int level, const char* file, int line,
                                    const char* function, const char* fmt, ...) {
    char buf[2048];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    buf[sizeof(buf) - 1] = '\0';

    QString msg = QString::fromUtf8(buf);
    LogManager::instance().write(level, file, line, function, msg);
}

SNAP_LOG_API void snap_log_write_msg(int level, const char* file, int line,
                                       const char* function, const char* message) {
    QString msg = QString::fromUtf8(message ? message : "");
    LogManager::instance().write(level,
                                  file ? file : "?",
                                  line,
                                  function ? function : "",
                                  msg);
}

}  // extern "C"
