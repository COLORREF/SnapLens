#include "cancel_intercept.h"
#include "log/log_manager.h"

namespace {

const wchar_t *kClassName = L"SnapLensCancelWindow";

} // namespace

std::atomic<HHOOK> Win32CancelProvider::s_hook{nullptr};
std::atomic<Win32CancelProvider *> Win32CancelProvider::s_instance{nullptr};

Win32CancelProvider::Win32CancelProvider(QObject *parent)
    : ICancelProvider(parent)
{
}

Win32CancelProvider::~Win32CancelProvider()
{
    uninstall();
}

bool Win32CancelProvider::install()
{
    if (m_running.load())
        return s_hook.load(std::memory_order_acquire) != nullptr;

    SNAP_LOG_DEBUG(QStringLiteral("Win32CancelProvider::install: installing"));
    s_instance.store(this, std::memory_order_release);
    m_running = true;
    m_thread = std::thread(&Win32CancelProvider::thread_main, this);

    for (int i = 0; i < 2000 && m_hwnd.load() == nullptr && m_running.load(); ++i)
        Sleep(1);

    // m_hwnd 由钩子线程在 s_hook 写入之后以 release 语义发布（见 thread_main），
    // 此处 acquire 读到非空后，s_hook 必然可见 —— 消除"读到旧钩子句柄"的竞争。
    bool ok = s_hook.load(std::memory_order_acquire) != nullptr;
    if (ok)
        SNAP_LOG_INFO(QStringLiteral("Win32CancelProvider::install: hook installed"));
    else
        SNAP_LOG_ERROR(QStringLiteral("Win32CancelProvider::install: hook failed"));
    return ok;
}

void Win32CancelProvider::uninstall()
{
    if (!m_running.load())
        return;

    SNAP_LOG_DEBUG(QStringLiteral("Win32CancelProvider::uninstall"));

    HWND hwnd = m_hwnd.load();
    if (hwnd)
        PostMessageW(hwnd, WM_QUIT, 0, 0);

    bool same_thread = m_thread.joinable()
        && (m_thread.get_id() == std::this_thread::get_id());
    if (m_thread.joinable() && !same_thread)
        m_thread.join();
    else if (same_thread)
        m_thread.detach();

    m_running = false;
    m_hwnd = nullptr;
    s_instance.store(nullptr, std::memory_order_release);
}

void Win32CancelProvider::thread_main()
{
    HINSTANCE hinst = GetModuleHandleW(nullptr);

    WNDCLASSW wc = {};
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = hinst;
    wc.lpszClassName = kClassName;
    ATOM atom = RegisterClassW(&wc);

    HWND hwnd = CreateWindowExW(
        0, kClassName, L"", 0,
        0, 0, 0, 0,
        HWND_MESSAGE, nullptr, hinst, nullptr);

    // 先写钩子句柄（release），再发布窗口句柄（release）：
    // 主线程在 install() 中 acquire 看到 m_hwnd 非空后，s_hook 必然可见。
    s_hook.store(SetWindowsHookExW(WH_KEYBOARD_LL,
                                   &Win32CancelProvider::low_level_proc,
                                   hinst, 0),
                 std::memory_order_release);
    m_hwnd.store(hwnd, std::memory_order_release);

    if (s_hook.load(std::memory_order_relaxed))
        SNAP_LOG_DEBUG(QStringLiteral("Win32CancelProvider: hook set"));
    else
        SNAP_LOG_ERROR(QStringLiteral("Win32CancelProvider: hook failed"));

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0)
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (HHOOK hook = s_hook.load(std::memory_order_relaxed))
    {
        UnhookWindowsHookEx(hook);
        s_hook.store(nullptr, std::memory_order_relaxed);
    }
    if (hwnd)
        DestroyWindow(hwnd);
    if (atom)
        UnregisterClassW(kClassName, hinst);
    m_hwnd.store(nullptr, std::memory_order_relaxed);
}

LRESULT CALLBACK Win32CancelProvider::low_level_proc(int code, WPARAM wp, LPARAM lp)
{
    if (code == HC_ACTION && wp == WM_KEYDOWN)
    {
        auto *kb = reinterpret_cast<KBDLLHOOKSTRUCT *>(lp);
        if (kb->vkCode == VK_ESCAPE)
        {
            SNAP_LOG_DEBUG(QStringLiteral("Win32CancelProvider: Esc pressed"));
            // 原子读取实例指针，避免与主线程 install/uninstall 的写入构成数据竞争。
            if (auto *inst = s_instance.load(std::memory_order_acquire))
            {
                QMetaObject::invokeMethod(inst, "escPressed",
                                          Qt::QueuedConnection);
            }
        }
    }
    return CallNextHookEx(nullptr, code, wp, lp);
}