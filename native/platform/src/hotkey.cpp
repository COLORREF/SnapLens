// hotkey.cpp - 全局热键实现
//
// 流程：
// 1. start() 启动后台线程
// 2. 后台线程注册窗口类、创建仅消息窗口、运行消息循环
// 3. 主线程通过 SendMessage 把注册/注销请求转发到后台线程
//    （RegisterHotKey 必须在拥有消息循环的线程中调用）
// 4. WM_HOTKEY 触发时，WndProc 调用 callback_
// 5. stop() 通过 PostQuitMessage 让消息循环退出，join 线程
//
#include "hotkey.h"
#include "common.h"

namespace {

// 窗口类名（仅内部使用，避免与其他应用冲突）
const wchar_t* kClassName = L"SnapLensHotkeyWindow";

// 自定义消息：在后台线程中执行注册/注销/退出
// 注意：必须用 WM_USER 之后的值，避免与系统消息冲突
const UINT kMsgRegister   = WM_USER + 1;  // WPARAM=id, LPARAM=MAKELONG(mods, vk)
const UINT kMsgUnregister = WM_USER + 2;  // WPARAM=id
const UINT kMsgQuit        = WM_USER + 3;  // 请求消息循环退出

}  // namespace

    HotkeyManager& HotkeyManager::instance() {
    static HotkeyManager inst;
    return inst;
}

    HotkeyManager::~HotkeyManager() {
    stop();
}

bool HotkeyManager::start() {
    if (running_.load()) {
        return hwnd_.load() != nullptr;  // 已启动，返回窗口是否就绪
    }
    SNAP_LOG_DEBUG("HotkeyManager::start: starting thread");
    running_ = true;
    thread_ = std::thread(&HotkeyManager::thread_main, this);

    // 等待后台线程创建窗口完成（最多 2 秒）
    // 窗口未创建时不能注册热键
    for (int i = 0; i < 2000 && hwnd_.load() == nullptr && running_.load(); ++i) {
        Sleep(1);
    }
    bool ok = hwnd_.load() != nullptr;
    SNAP_LOG_INFO("HotkeyManager::start: thread started, hwnd=%p",
                  reinterpret_cast<void*>(hwnd_.load()));
    return ok;
}

void HotkeyManager::stop() {
    if (!running_.load()) return;
    SNAP_LOG_DEBUG("HotkeyManager::stop");
    HWND hwnd = hwnd_.load();
    if (hwnd) {
        // 向后台线程的消息循环发送退出请求
        PostMessageW(hwnd, kMsgQuit, 0, 0);
    }

    // 同线程检测：若从 WM_HOTKEY 回调中调用 stop() 会死锁
    bool is_same_thread = thread_.joinable()
        && (thread_.get_id() == std::this_thread::get_id());
    if (thread_.joinable() && !is_same_thread) {
        thread_.join();
    } else if (is_same_thread) {
thread_.detach();
    }

    running_ = false;
    hwnd_ = nullptr;
}

bool HotkeyManager::register_hotkey(int id, unsigned int mods, unsigned int vk) {
    HWND hwnd = hwnd_.load();
    if (!hwnd) return false;

    // SendMessage 跨线程同步：后台线程的 WndProc 收到此消息时
    // 立即调用 RegisterHotKey 并返回结果
    // LPARAM 低 16 位放 mods，高 16 位放 vk（实际值都很小，无截断风险）
    LRESULT ret = SendMessageW(hwnd, kMsgRegister, (WPARAM)id,
                                 (LPARAM)MAKELONG(mods, vk));
    return ret != 0;
}

void HotkeyManager::unregister_hotkey(int id) {
    SNAP_LOG_DEBUG("HotkeyManager::unregister_hotkey: id=%d", id);
    HWND hwnd = hwnd_.load();
    if (!hwnd) return;
    SendMessageW(hwnd, kMsgUnregister, (WPARAM)id, 0);
}

void HotkeyManager::thread_main() {
    HINSTANCE hinst = GetModuleHandleW(nullptr);
// 注册窗口类（仅消息窗口也需要类）
    WNDCLASSW wc = {};
    wc.lpfnWndProc = &HotkeyManager::wnd_proc;
    wc.hInstance = hinst;
    wc.lpszClassName = kClassName;
    ATOM atom = RegisterClassW(&wc);
    if (!atom) {
}

    // 创建仅消息窗口：不可见、不接收鼠标键盘事件
    // 作用仅为：拥有线程消息队列 + 接收 WM_HOTKEY
    HWND hwnd = CreateWindowExW(
        0, kClassName, L"SnapLens", 0,
        0, 0, 0, 0,
        HWND_MESSAGE, nullptr, hinst, nullptr
    );
    hwnd_ = hwnd;
    if (!hwnd) {
        SNAP_LOG_ERROR("HotkeyManager: window creation failed, class=%ls",
                       kClassName);
} else {
    SNAP_LOG_DEBUG("HotkeyManager: window created, class=%ls, hwnd=%p",
                   kClassName, reinterpret_cast<void*>(hwnd));
    }

    // 消息循环：处理 WM_HOTKEY 和自定义消息
    // GetMessage 在收到 WM_QUIT 时返回 0，循环退出
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    // 清理
    if (hwnd) DestroyWindow(hwnd);
    if (atom) UnregisterClassW(kClassName, hinst);
    hwnd_ = nullptr;
}

LRESULT CALLBACK HotkeyManager::wnd_proc(HWND hwnd, UINT msg,
                                            WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_HOTKEY: {
            // 热键触发：wp 是 hotkey_id
            int id = static_cast<int>(wp);
            SNAP_LOG_DEBUG("HotkeyManager: WM_HOTKEY id=%d", id);
            HotkeyManager& mgr = instance();
            if (mgr.callback_) {
                // 跨线程调用 Python 回调
                // ctypes CFUNCTYPE 回调会自动获取 GIL，可安全访问 Python 对象
                mgr.callback_(id, mgr.user_data_);
            } else {
            }
            return 0;
        }
        case kMsgRegister: {
            // 主线程请求注册：在后台线程中调用 RegisterHotKey
            int id = static_cast<int>(wp);
            unsigned int mods = LOWORD(lp);
            unsigned int vk = HIWORD(lp);
            BOOL ok = RegisterHotKey(hwnd, id, mods, vk);
            if (ok) {
                SNAP_LOG_INFO("HotkeyManager::register_hotkey: id=%d", id);
            } else {
                SNAP_LOG_DEBUG("HotkeyManager::register_hotkey: already registered id=%d", id);
            }
    return ok ? 1 : 0;
        }
        case kMsgUnregister: {
            int id = static_cast<int>(wp);
            UnregisterHotKey(hwnd, id);
            return 0;
        }
        case kMsgQuit: {
            // 主线程请求退出：发送 WM_QUIT 让 GetMessage 返回 0
            PostQuitMessage(0);
            return 0;
        }
        default:
            return DefWindowProcW(hwnd, msg, wp, lp);
    }
}
