// window_enum.h - 顶层窗口枚举（EnumWindows + DWM 扩展边框）
//
// 枚举所有可见顶层窗口，用于"单击窗口截图"的命中检测。
// 返回结果缓存在内部列表中，Python 侧通过 snap_window_get_item 逐项获取。
//
#ifndef SNAPLENS_WINDOW_ENUM_H
#define SNAPLENS_WINDOW_ENUM_H

#include "common.h"

class WindowEnumerator {
public:
    // 单个窗口的信息
    struct Item {
        long long hwnd;       // 窗口句柄（避免 64 位截断）
        int left, top;        // 物理像素坐标（左上角）
        int right, bottom;    // 物理像素坐标（右下角）
        std::wstring title;   // 窗口标题
    };

    static WindowEnumerator& instance();

    // 枚举顶层窗口，结果缓存在内部列表
    // 返回：窗口数量
    int enum_windows();

    // 获取第 idx 个窗口信息（idx 从 0 到 enum_windows()-1）
    // 返回：成功 true，越界 false
    bool get_item(int idx, long long* hwnd,
                  int* left, int* top, int* right, int* bottom,
                  wchar_t* title, int title_size) const;

private:
    WindowEnumerator() = default;
    ~WindowEnumerator() = default;
    WindowEnumerator(const WindowEnumerator&) = delete;
    WindowEnumerator& operator=(const WindowEnumerator&) = delete;

    std::vector<Item> items_;

    // EnumWindows 的回调：参数 lparam 携带 this 指针
    static BOOL CALLBACK enum_proc(HWND hwnd, LPARAM lparam);
};

#endif  // SNAPLENS_WINDOW_ENUM_H
