#ifndef SNAPLENS_PLATFORM_CANCEL_INTERCEPT_H
#define SNAPLENS_PLATFORM_CANCEL_INTERCEPT_H

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <thread>
#include <atomic>
#include <Windows.h>

#include "platform_base.h"

// Win32 Esc 键拦截实现（WH_KEYBOARD_LL 低级键盘钩子，不依赖焦点）
class Win32CancelProvider : public ICancelProvider
{
    Q_OBJECT

public:
    explicit Win32CancelProvider(QObject *parent = nullptr);
    ~Win32CancelProvider() override;

    bool install() override;
    void uninstall() override;

private:
    void thread_main();
    static LRESULT CALLBACK low_level_proc(int code, WPARAM wp, LPARAM lp);

    std::thread m_thread;
    std::atomic<HWND> m_hwnd{nullptr};
    std::atomic<bool> m_running{false};

    // 跨线程共享（主线程 ↔ 钩子线程）必须用原子类型：
    // 非原子读写同一变量构成数据竞争（C++ 未定义行为）。
    static std::atomic<HHOOK> s_hook;
    static std::atomic<Win32CancelProvider *> s_instance;
};

#endif // SNAPLENS_PLATFORM_CANCEL_INTERCEPT_H
