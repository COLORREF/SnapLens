// common.h - 内部公共包含和工具定义
//
// 所有 .cpp 文件统一包含此头，避免重复的 windows.h 设置和 STL 头。
// 不对外导出（仅 snaplens_platform.h 是公共头）。
//
#ifndef SNAPLENS_INTERNAL_COMMON_H
#define SNAPLENS_INTERNAL_COMMON_H

// 减少 windows.h 拉入的无关头文件
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX  // 避免与 std::min/max 冲突

#include <windows.h>
#include <dwmapi.h>

// C++17 标准库
#include <atomic>
#include <thread>
#include <mutex>
#include <vector>
#include <string>
#include <unordered_map>

// ---- DWM 属性常量（旧版 SDK 可能未定义，此处兜底）----
#ifndef DWMWA_EXTENDED_FRAME_BOUNDS
    #define DWMWA_EXTENDED_FRAME_BOUNDS 9   // 窗口可见边框（物理像素）
#endif
#ifndef DWMWA_CLOAKED
    #define DWMWA_CLOAKED 14                 // UWP 等被遮蔽的隐形窗口
#endif

#endif  // SNAPLENS_INTERNAL_COMMON_H
