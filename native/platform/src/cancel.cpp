// cancel.cpp - Esc 键拦截实现
//
// 流程：
// 1. install() 启动后台线程
// 2. 后台线程创建仅消息窗口，安装 WH_KEYBOARD_LL 钩子，运行消息循环
// 3. Esc 按下时，LowLevelKeyboardProc 调用 callback
// 4. uninstall() 通过 PostQuitMessage 让消息循环退出，join 线程
//
// 重要：uninstall 时先卸载钩子，再清空 callback_，
//       确保钩子回调不会调用已销毁的 Python 对象。
//
#include "cancel.h"
#include "common.h"

namespace {

const wchar_t* kClassName = L"SnapLensCancelWindow";

}  // namespace

// 静态成员初始化（LowLevelKeyboardProc 是静态回调）
HHOOK CancelManager::s_hook_ = nullptr;

    CancelManager& CancelManager::instance() {
    static CancelManager inst;
    return inst;
}

    CancelManager::~CancelManager() {
    uninstall();
}

bool CancelManager::install(CancelCallback callback, void* user_data) {
    if (running_.load()) {
        return s_hook_ != nullptr;  // 已安装
    }
    SNAP_LOG_DEBUG("CancelManager::install: installing");
    callback_ = callback;
    user_data_ = user_data;
    running_ = true;
    thread_ = std::thread(&CancelManager::thread_main, this);

    // 等待钩子安装完成（最多 2 秒）
    for (int i = 0; i < 2000 && hwnd_.load() == nullptr && running_.load(); ++i) {
        Sleep(1);
    }
    bool ok = s_hook_ != nullptr;
    if (ok) {
        SNAP_LOG_INFO("CancelManager::install: installed, hook=%p",
                      reinterpret_cast<void*>(s_hook_));
    } else {
        SNAP_LOG_ERROR("CancelManager::install: hook failed");
    }
    return ok;
}

void CancelManager::uninstall() {
    if (!running_.load()) return;
    SNAP_LOG_DEBUG("CancelManager::uninstall");
    HWND hwnd = hwnd_.load();
    if (hwnd) {
        // 通知消息循环退出
        PostMessageW(hwnd, WM_QUIT, 0, 0);
    }

    // 检测是否从钩子线程内部调用（例如 Esc 回调中调用 uninstall）
    // 若同线程调用 thread_.join() 会死锁（等待自身退出），
    // 改为 detach 让线程自行退出并清理
    bool is_same_thread = thread_.joinable()
        && (thread_.get_id() == std::this_thread::get_id());
    if (thread_.joinable() && !is_same_thread) {
        thread_.join();
    } else if (is_same_thread) {
thread_.detach();
    }

    running_ = false;
    hwnd_ = nullptr;

    // 先清空 callback_，防止钩子线程在退出前再次触发回调
    // （钩子线程会在处理完 WM_QUIT 后自行调用 UnhookWindowsHookEx）
    callback_ = nullptr;
    user_data_ = nullptr;
}

void CancelManager::thread_main() {
    HINSTANCE hinst = GetModuleHandleW(nullptr);
// 注册窗口类
    WNDCLASSW wc = {};
    wc.lpfnWndProc = DefWindowProcW;  // 仅消息窗口，用默认过程即可
    wc.hInstance = hinst;
    wc.lpszClassName = kClassName;
    ATOM atom = RegisterClassW(&wc);

    // 创建仅消息窗口（用于接收 WM_QUIT）
    HWND hwnd = CreateWindowExW(
        0, kClassName, L"", 0,
        0, 0, 0, 0,
        HWND_MESSAGE, nullptr, hinst, nullptr
    );
    hwnd_ = hwnd;

    // 安装低级键盘钩子
    // 第 4 个参数为 0 表示全局钩子（拦截所有线程的按键）
    s_hook_ = SetWindowsHookExW(WH_KEYBOARD_LL,
                                  &CancelManager::low_level_proc,
                                  hinst, 0);
    if (s_hook_) {
        SNAP_LOG_DEBUG("CancelManager: hook set");
    } else {
        SNAP_LOG_ERROR("CancelManager: hook failed");
    }
// 消息循环（低级钩子要求安装线程必须有消息循环）
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    // 清理钩子和窗口
    if (s_hook_) {
        UnhookWindowsHookEx(s_hook_);
        s_hook_ = nullptr;
    }
    if (hwnd) DestroyWindow(hwnd);
    if (atom) UnregisterClassW(kClassName, hinst);
    hwnd_ = nullptr;
}

LRESULT CALLBACK CancelManager::low_level_proc(int code, WPARAM wp, LPARAM lp) {
    // HC_ACTION 表示这是真实的键盘事件
    if (code == HC_ACTION && wp == WM_KEYDOWN) {
        auto* kb = reinterpret_cast<KBDLLHOOKSTRUCT*>(lp);
        if (kb->vkCode == VK_ESCAPE) {
            SNAP_LOG_DEBUG("CancelManager: Esc pressed");
            CancelManager& mgr = instance();
            // 读取 callback_（局部变量，避免在回调执行期间被 uninstall 清空）
            CancelCallback cb = mgr.callback_;
            if (cb) {
                cb(mgr.user_data_);
            } else {
                SNAP_LOG_WARNING("CancelManager: Esc pressed but no callback");
            }
        }
    }
    // 必须调用 CallNextHookEx 让其他钩子也能收到事件
    return CallNextHookEx(nullptr, code, wp, lp);
}
