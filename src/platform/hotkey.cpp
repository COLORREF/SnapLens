#include "hotkey.h"

#include <QKeySequence>
#include <QSemaphore>
#include <QDebug>

#include "../log/log_manager.h"

// ============================================================================
// HotkeyThread — 内部类：在独立线程中创建隐藏窗口，运行消息循环，注册全局热键
// ============================================================================

class Win32HotkeyProvider::HotkeyThread : public QThread
{
public:
    HotkeyThread(Win32HotkeyProvider* provider)
        : m_provider(provider)
    {
    }

    ~HotkeyThread() override
    {
        // 正常路径下 stop() 已确保线程退出（wait 成功）或已分离
        // （分离后本对象在线程结束后才被 deleteLater 删除，析构时线程必然已结束），
        // 因此这里只需置停止标志，不做 wait/terminate。
        m_stopped = true;
    }

    bool register_hotkey(UINT modifiers, UINT vk)
    {
        m_modifiers = modifiers;
        m_vk = vk;
        start();
        // 限时等待线程完成注册：正常路径 run() 在数毫秒内 release 信号量；
        // 3s 防御 QThread::start 失败/调度异常等极端情况，超时视为注册失败，
        // 避免 acquire() 无限期阻塞主线程。
        if (!m_started_sem.tryAcquire(1, 3000)) {
            qWarning() << "HotkeyThread::register_hotkey: timeout waiting for registration";
            // 线程可能仍存活：置停止标志让其退出消息循环并短等待，
            // 确保返回后调用方不会对运行中的 QThread 对象执行 delete（UB）。
            request_stop();
            m_register_result = false;
        }
        return m_register_result;
    }

    void request_stop()
    {
        m_stopped = true;
        // 消息循环为 PeekMessageW 轮询（见 run()）：m_stopped 置位后最多 ~10ms
        // 线程自行退出，无需 PostMessage 唤醒。
        // 不使用 terminate()：QThread::terminate 会在任意指令位置强杀线程（Qt 反模式）。
        if (isRunning()) {
            wait(1000);   // 正常 ~10ms 退出；1s 为系统调用异常卡住的防御上限
        }
        if (isRunning()) {
            SNAP_LOG_WARNING("HotkeyThread::request_stop: 线程未在预期时间内退出");
        }
    }

protected:
    void run() override
    {
        const wchar_t* className = L"SnapLens_HotkeyWindow";

        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof(WNDCLASSEXW);
        wc.lpfnWndProc = &HotkeyThread::wnd_proc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = className;
        RegisterClassExW(&wc);

        m_hwnd = CreateWindowExW(0, className, L"", 0,
                                  0, 0, 0, 0,
                                  HWND_MESSAGE, nullptr,
                                  GetModuleHandleW(nullptr), this);
        if (!m_hwnd) {
            m_register_result = false;
            m_started_sem.release();
            return;
        }

        constexpr int kHotkeyId = 1;
        if (!RegisterHotKey(m_hwnd, kHotkeyId, m_modifiers, m_vk)) {
            m_register_result = false;
            m_started_sem.release();
            return;
        }

        m_register_result = true;
        m_started_sem.release();

        MSG msg = {};
        // 非阻塞轮询消息循环：m_stopped 置位后最多 ~10ms 内自行退出，
        // 无需 PostMessage 唤醒，也不依赖 terminate。
        // WM_HOTKEY 等消息由 PeekMessageW + DispatchMessageW 正常分发。
        while (!m_stopped) {
            while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
            Sleep(10);
        }

        UnregisterHotKey(m_hwnd, kHotkeyId);
        DestroyWindow(m_hwnd);
        UnregisterClassW(className, GetModuleHandleW(nullptr));
        m_hwnd = nullptr;
    }

private:
    static LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        if (msg == WM_NCCREATE) {
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                              reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
            return DefWindowProcW(hwnd, msg, wParam, lParam);
        }

        if (msg == WM_HOTKEY) {
            auto* self = reinterpret_cast<HotkeyThread*>(
                GetWindowLongPtrW(hwnd, GWLP_USERDATA));
            if (self && self->m_provider) {
                // Only emit Qt signal — queued connection ensures main-thread delivery
                emit self->m_provider->triggered();
            }
            return 0;
        }

        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }

    Win32HotkeyProvider* m_provider;
    HWND m_hwnd = nullptr;
    UINT m_modifiers = 0;
    UINT m_vk = 0;
    bool m_register_result = false;
    QSemaphore m_started_sem;
    std::atomic<bool> m_stopped{false};
};

// ============================================================================
// Win32HotkeyProvider
// ============================================================================

Win32HotkeyProvider::Win32HotkeyProvider(QObject* parent)
    : IHotkeyProvider(parent)
{
}

Win32HotkeyProvider::~Win32HotkeyProvider()
{
    stop();
}

bool Win32HotkeyProvider::start(const QString& sequence_text)
{
    stop();

    UINT modifiers = 0;
    UINT vk = 0;
    if (!parse_sequence(sequence_text, modifiers, vk)) {
        qWarning() << "Win32HotkeyProvider: failed to parse sequence:" << sequence_text;
        return false;
    }

    m_thread = new HotkeyThread(this);
    bool ok = m_thread->register_hotkey(modifiers, vk);
    if (!ok) {
        qWarning() << "Win32HotkeyProvider: RegisterHotKey failed for:" << sequence_text;
        // 安全清理：register_hotkey 失败时线程可能仍未完全退出（极端异常），
        // 对运行中的 QThread 对象 delete 是未定义行为（"Destroyed while running" 崩溃）。
        // 先请求停止；仍未退出则分离线程对象，由 finished → deleteLater 自清理。
        m_thread->request_stop();
        if (m_thread->isRunning()) {
            SNAP_LOG_WARNING("Win32HotkeyProvider::start: 注册失败但线程未退出，转为后台自清理");
            m_thread->setParent(nullptr);
            QObject::connect(m_thread, &QThread::finished,
                             m_thread, &QObject::deleteLater);
        } else {
            delete m_thread;
        }
        m_thread = nullptr;
        return false;
    }

    m_registered = true;
    return true;
}

void Win32HotkeyProvider::stop()
{
    if (m_thread) {
        m_thread->request_stop();
        if (m_thread->isRunning()) {
            // 极端异常（系统调用卡死）：不能 delete 运行中的 QThread 对象
            // （会触发 "QThread: Destroyed while thread is still running" 崩溃）。
            // 分离线程对象，由 finished → deleteLater 在线程结束后自清理。
            SNAP_LOG_WARNING("Win32HotkeyProvider::stop: 线程未退出，转为后台自清理");
            m_thread->setParent(nullptr);
            QObject::connect(m_thread, &QThread::finished,
                             m_thread, &QObject::deleteLater);
        } else {
            delete m_thread;
        }
        m_thread = nullptr;
    }
    m_registered = false;
}

// ---------------------------------------------------------------------------
// 解析 QKeySequence 文本
// ---------------------------------------------------------------------------

bool Win32HotkeyProvider::parse_sequence(const QString& sequence_text,
                                     UINT& out_modifiers, UINT& out_vk)
{
    QKeySequence ks(sequence_text);
    if (ks.isEmpty()) {
        return false;
    }

    QKeyCombination combo = ks[0];
    Qt::KeyboardModifiers mods = combo.keyboardModifiers();
    Qt::Key key = combo.key();

    out_modifiers = qt_modifiers_to_win32(mods) | MOD_NOREPEAT;
    out_vk = qt_key_to_vk(key);

    return out_vk != 0;
}

UINT Win32HotkeyProvider::qt_modifiers_to_win32(Qt::KeyboardModifiers modifiers)
{
    UINT result = 0;
    if (modifiers & Qt::AltModifier)     result |= MOD_ALT;
    if (modifiers & Qt::ControlModifier) result |= MOD_CONTROL;
    if (modifiers & Qt::ShiftModifier)   result |= MOD_SHIFT;
    if (modifiers & Qt::MetaModifier)    result |= MOD_WIN;
    return result;
}

UINT Win32HotkeyProvider::qt_key_to_vk(Qt::Key key)
{
    // A-Z 直接映射
    if (key >= Qt::Key_A && key <= Qt::Key_Z) {
        return static_cast<UINT>(key);
    }

    // 0-9 直接映射
    if (key >= Qt::Key_0 && key <= Qt::Key_9) {
        return static_cast<UINT>(key);
    }

    // F1-F24
    if (key >= Qt::Key_F1 && key <= Qt::Key_F24) {
        int offset = static_cast<int>(key) - static_cast<int>(Qt::Key_F1);
        return 0x70 + static_cast<UINT>(offset);
    }

    // 特殊键映射表
    static const std::pair<Qt::Key, UINT> kSpecialKeys[] = {
        {Qt::Key_Space,     0x20},  // VK_SPACE
        {Qt::Key_Backspace, 0x08},  // VK_BACK
        {Qt::Key_Tab,       0x09},  // VK_TAB
        {Qt::Key_Return,    0x0D},  // VK_RETURN（主键盘回车）
        {Qt::Key_Enter,     0x0D},  // VK_RETURN（小键盘回车，对齐 Python _SPECIAL_KEYS）
        {Qt::Key_Escape,    0x1B},  // VK_ESCAPE
        {Qt::Key_Print,     0x2C},  // VK_SNAPSHOT
        {Qt::Key_Insert,    0x2D},  // VK_INSERT
        {Qt::Key_Delete,    0x2E},  // VK_DELETE
        {Qt::Key_Home,      0x24},  // VK_HOME
        {Qt::Key_End,       0x23},  // VK_END
        {Qt::Key_PageUp,    0x21},  // VK_PRIOR
        {Qt::Key_PageDown,  0x22},  // VK_NEXT
        {Qt::Key_Left,      0x25},  // VK_LEFT
        {Qt::Key_Up,        0x26},  // VK_UP
        {Qt::Key_Right,     0x27},  // VK_RIGHT
        {Qt::Key_Down,      0x28},  // VK_DOWN
    };

    for (const auto& [qt_key, vk] : kSpecialKeys) {
        if (qt_key == key) {
            return vk;
        }
    }

    return 0;
}