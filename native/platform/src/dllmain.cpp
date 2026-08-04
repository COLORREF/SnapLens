// dllmain.cpp - DLL 入口 + C ABI 函数实现
//
// 该文件实现 snaplens_platform.h 中声明的所有 C ABI 函数：
// - 生命周期（snap_init / snap_shutdown）
// - 热键（snap_hotkey_*）
// - 窗口枚举（snap_window_enum / snap_window_get_item）
// - 光标位置（snap_cursor_get_pos）—— 直接调用 GetCursorPos，无需单独类
// - Esc 拦截（snap_cancel_*）
// - 光标限制（snap_clip_cursor_*）—— 直接调用 ClipCursor，无需单独类
//
// cursor 和 clip_cursor 的逻辑极简，直接在此文件内联实现，避免额外文件。
//
#include "../include/snaplens_platform.h"
#include "common.h"
#include "hotkey.h"
#include "window_enum.h"
#include "cancel.h"

// ============================================================================
// DLL 入口
// ============================================================================
BOOL APIENTRY DllMain(HMODULE, DWORD reason, LPVOID) {
    // 不在 DllMain 中做任何初始化，避免 LoadLock 死锁
    // 所有资源按需在 snap_init 或各 Manager::install 中创建
    switch (reason) {
        case DLL_PROCESS_ATTACH:
            SNAP_LOG_DEBUG("DllMain: PROCESS_ATTACH");
            break;
        case DLL_PROCESS_DETACH:
            SNAP_LOG_DEBUG("DllMain: PROCESS_DETACH");
            break;
        case DLL_THREAD_ATTACH:
        case DLL_THREAD_DETACH:
            break;
    }
    return TRUE;
}

// ============================================================================
// C ABI 实现
// 外部使用 extern "C" 包裹，确保 C ABI 兼容（无 name mangling）
// ============================================================================
extern "C" {

// ---------- 生命周期 ----------

SNAP_API int snap_init(void) {
    SNAP_LOG_DEBUG("snap_init");
// 当前为空操作，所有 Manager 都是单例，按需启动
    // 保留接口用于未来扩展（如全局 COM 初始化）
    return 1;
}

SNAP_API void snap_shutdown(void) {
    SNAP_LOG_DEBUG("snap_shutdown");
    HotkeyManager::instance().stop();
    CancelManager::instance().uninstall();
    // 光标限制解除（如果之前处于限制状态）
    ClipCursor(nullptr);
}

// ---------- 热键 ----------

SNAP_API void snap_hotkey_set_callback(void (*callback)(int, void*),
                                          void* user_data) {
    SNAP_LOG_DEBUG("snap_hotkey_set_callback: callback=%p, user_data=%p",
                   reinterpret_cast<void*>(callback), user_data);
    HotkeyManager::instance().set_callback(callback, user_data);
}

SNAP_API int snap_hotkey_register(int hotkey_id, unsigned int mods,
                                     unsigned int vk) {
    auto& mgr = HotkeyManager::instance();
    // 后台线程按需启动（首次调用时）
    if (!mgr.start()) {
        SNAP_LOG_ERROR("snap_hotkey_register: start() failed for id=%d",
                       hotkey_id);
    return 0;
    }
    int ok = mgr.register_hotkey(hotkey_id, mods, vk) ? 1 : 0;
    SNAP_LOG_DEBUG("snap_hotkey_register: id=%d, mods=%u, vk=%u, ok=%d",
                   hotkey_id, mods, vk, ok);
    return ok;
}

SNAP_API void snap_hotkey_unregister(int hotkey_id) {
    SNAP_LOG_DEBUG("snap_hotkey_unregister: id=%d", hotkey_id);
    HotkeyManager::instance().unregister_hotkey(hotkey_id);
}

// ---------- 窗口枚举 ----------

SNAP_API int snap_window_enum(void) {
    int n = WindowEnumerator::instance().enum_windows();
    SNAP_LOG_DEBUG("snap_window_enum: count=%d", n);
    return n;
}

SNAP_API int snap_window_get_item(int idx, long long* hwnd,
                                     int* left, int* top,
                                     int* right, int* bottom,
                                     wchar_t* title, int title_size) {
    int ok = WindowEnumerator::instance().get_item(
        idx, hwnd, left, top, right, bottom, title, title_size
    ) ? 1 : 0;
    // 仅记录第一次（idx=0）和失败情况，避免刷屏
    if (ok) {
        if (idx == 0) {
}
    } else {
}
    return ok;
}

// ---------- 光标位置 ----------

SNAP_API void snap_cursor_get_pos(int* x, int* y) {
    POINT pt{};
    if (GetCursorPos(&pt)) {
        if (x) *x = pt.x;
        if (y) *y = pt.y;
    } else {
        if (x) *x = 0;
        if (y) *y = 0;
    }
}

// ---------- Esc 拦截 ----------

SNAP_API int snap_cancel_install(void (*callback)(void*), void* user_data) {
    SNAP_LOG_DEBUG("snap_cancel_install");
    int ok = CancelManager::instance().install(callback, user_data) ? 1 : 0;
    return ok;
}

SNAP_API void snap_cancel_uninstall(void) {
    SNAP_LOG_DEBUG("snap_cancel_uninstall");
    CancelManager::instance().uninstall();
}

// ---------- 光标限制 ----------

SNAP_API int snap_clip_cursor(int left, int top, int right, int bottom) {
    SNAP_LOG_DEBUG("snap_clip_cursor: rect=(%d,%d,%d,%d)",
                   left, top, right, bottom);
    RECT r{left, top, right, bottom};
    int ok = ClipCursor(&r) ? 1 : 0;
    return ok;
}

SNAP_API void snap_clip_cursor_release(void) {
    SNAP_LOG_DEBUG("snap_clip_cursor_release");
ClipCursor(nullptr);
}

}  // extern "C"
