// window_enum.cpp - 顶层窗口枚举实现
//
// 过滤规则：
// 1. 排除不可见窗口（IsWindowVisible == FALSE）
// 2. 排除最小化窗口（IsIconic）
// 3. 排除本进程窗口（避免抓到自己的覆盖层/钉图窗口）
// 4. 排除被 DWM 遮蔽的隐形窗口（UWP 挂起等场景）
// 5. 排除过小窗口（< 8x8 像素）
//
// 边框策略：优先取 DWM 扩展边框（去除 Win10/11 隐形阴影），
//          失败时回退到 GetWindowRect。
//
#include "window_enum.h"
#include "common.h"

WindowEnumerator& WindowEnumerator::instance() {
    static WindowEnumerator inst;
    return inst;
}

BOOL CALLBACK WindowEnumerator::enum_proc(HWND hwnd, LPARAM lparam) {
    auto* self = reinterpret_cast<WindowEnumerator*>(lparam);

    // 1) 排除不可见、最小化的窗口
    if (!IsWindowVisible(hwnd) || IsIconic(hwnd)) {
        return TRUE;  // 继续枚举
    }

    // 2) 排除本进程窗口
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == GetCurrentProcessId()) {
        return TRUE;
    }

    // 3) 排除被 DWM 遮蔽的隐形窗口（如挂起的 UWP 应用）
    BOOL cloaked = FALSE;
    if (DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED,
                                &cloaked, sizeof(cloaked)) == S_OK) {
        if (cloaked) return TRUE;
    }

    // 4) 取窗口可见边框（物理像素）
    //    优先 DWM 扩展边框（去除 Win10/11 隐形阴影边距）
    RECT rect{};
    if (DwmGetWindowAttribute(hwnd, DWMWA_EXTENDED_FRAME_BOUNDS,
                                &rect, sizeof(rect)) != S_OK) {
        // 回退到 GetWindowRect（包含阴影边距）
        if (!GetWindowRect(hwnd, &rect)) return TRUE;
    }

    // 5) 忽略过小窗口（< 8x8）
    if (rect.right - rect.left < 8 || rect.bottom - rect.top < 8) {
        return TRUE;
    }

    // 6) 取窗口标题
    wchar_t title[512] = {};
    GetWindowTextW(hwnd, title, 512);

    self->items_.push_back({
        reinterpret_cast<long long>(hwnd),
        rect.left, rect.top, rect.right, rect.bottom,
        title
    });
    return TRUE;
}

int WindowEnumerator::enum_windows() {
    SNAP_LOG_DEBUG("WindowEnumerator::enum_windows: starting");
    items_.clear();
    EnumWindows(&WindowEnumerator::enum_proc,
                reinterpret_cast<LPARAM>(this));
    int count = static_cast<int>(items_.size());
    SNAP_LOG_INFO("WindowEnumerator::enum_windows: count=%d", count);
    return count;
}

bool WindowEnumerator::get_item(int idx, long long* hwnd,
                                   int* left, int* top,
                                   int* right, int* bottom,
                                   wchar_t* title, int title_size) const {
    if (idx < 0 || idx >= static_cast<int>(items_.size())) {
        return false;
    }

    const Item& it = items_[idx];
    if (hwnd)  *hwnd = it.hwnd;
    if (left)  *left = it.left;
    if (top)   *top = it.top;
    if (right) *right = it.right;
    if (bottom) *bottom = it.bottom;

    // 安全拷贝标题（自动截断）
    if (title && title_size > 0) {
        wcsncpy_s(title, title_size, it.title.c_str(), _TRUNCATE);
    }
    return true;
}
