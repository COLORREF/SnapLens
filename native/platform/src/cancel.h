// cancel.h - Esc 键全局拦截（WH_KEYBOARD_LL 低级键盘钩子）
//
// 通过 WH_KEYBOARD_LL 低级键盘钩子拦截 Esc，无需依赖焦点/键盘抓取，
// 即使覆盖层未获得焦点也能取消截图。
//
// 低级钩子要求安装钩子的线程必须运行消息循环（否则钩子不触发），
// 因此本类内部启动一个 std::thread 专门运行消息循环。
//
// 注意：低级钩子有超时限制（默认 300ms），回调函数必须快速返回。
//       Python 侧的回调应仅 emit Qt 信号，不要执行耗时操作。
//
#ifndef SNAPLENS_CANCEL_H
#define SNAPLENS_CANCEL_H

#include "common.h"

// Esc 按下时的回调
using CancelCallback = void (*)(void*);

class CancelManager {
public:
    static CancelManager& instance();

    // 安装钩子（幂等，重复调用安全）
    // callback 在 Esc 按下时从后台线程被调用
    // 返回：成功 true，失败 false（已安装或钩子安装失败）
    bool install(CancelCallback callback, void* user_data);

    // 卸载钩子（幂等）
    void uninstall();

private:
    CancelManager() = default;
    ~CancelManager();
    CancelManager(const CancelManager&) = delete;
    CancelManager& operator=(const CancelManager&) = delete;

    void thread_main();
    static LRESULT CALLBACK low_level_proc(int code, WPARAM wp, LPARAM lp);

    std::thread thread_;
    std::atomic<HWND> hwnd_{nullptr};     // 仅消息窗口，用于退出通知
    std::atomic<bool> running_{false};

    // 钩子句柄（s_hook_ 是静态的，因为 LowLevelKeyboardProc 是静态回调）
    static HHOOK s_hook_;

    // 回调（install 时设置，uninstall 时清空）
    // 注意：先卸载钩子再清空 callback_，确保钩子回调中不会读到悬空指针
    CancelCallback callback_ = nullptr;
    void* user_data_ = nullptr;
};

#endif  // SNAPLENS_CANCEL_H
