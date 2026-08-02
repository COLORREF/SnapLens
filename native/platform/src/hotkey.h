// hotkey.h - 全局热键管理（隐藏窗口 + 后台消息循环）
//
// RegisterHotKey 必须在有消息循环的线程调用，WM_HOTKEY 也会发到该线程。
// 因此本类在内部启动一个 std::thread，创建仅消息窗口（HWND_MESSAGE），
// 所有注册/注销操作通过 PostMessage 调度到后台线程执行。
//
// WM_HOTKEY 触发时，调用 Python 侧设置的回调函数（跨线程，ctypes 自动获取 GIL）。
//
#ifndef SNAPLENS_HOTKEY_H
#define SNAPLENS_HOTKEY_H

#include "common.h"

// 热键触发回调：参数为 hotkey_id 和用户上下文指针
using HotkeyCallback = void (*)(int, void*);

class HotkeyManager {
public:
    static HotkeyManager& instance();

    // 启动后台线程（幂等，重复调用安全）
    // 返回：成功启动返回 true
    bool start();

    // 停止后台线程并清理窗口（幂等）
    void stop();

    // 设置回调（在 WM_HOTKEY 触发时调用）
    void set_callback(HotkeyCallback cb, void* user_data) {
        callback_ = cb;
        user_data_ = user_data;
    }

    // 注册热键：在后台线程中调用 RegisterHotKey
    // 返回：成功 true，失败 false（键位冲突）
    bool register_hotkey(int id, unsigned int mods, unsigned int vk);

    // 注销热键
    void unregister_hotkey(int id);

private:
    HotkeyManager() = default;
    ~HotkeyManager();
    HotkeyManager(const HotkeyManager&) = delete;
    HotkeyManager& operator=(const HotkeyManager&) = delete;

    void thread_main();
    static LRESULT CALLBACK wnd_proc(HWND, UINT, WPARAM, LPARAM);

    std::thread thread_;
    std::atomic<HWND> hwnd_{nullptr};
    std::atomic<bool> running_{false};

    HotkeyCallback callback_ = nullptr;
    void* user_data_ = nullptr;
};

#endif  // SNAPLENS_HOTKEY_H
