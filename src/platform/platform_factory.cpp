#include "platform_factory.h"

#include "platform_base.h"

// 各平台实现（当前仅 Windows；后续接入 macOS/Linux 时新增对应实现头文件）
#ifdef _WIN32
#include "hotkey.h"            // Win32HotkeyProvider
#include "cursor.h"            // Win32CursorProvider / Win32ClipCursorProvider
#include "cancel_intercept.h"  // Win32CancelProvider
#include "window_enum.h"       // Win32WindowProvider
#endif

// ============================================================================
// 工厂实现 — 按平台宏分发，非 _WIN32 平台使用 Null 占位实现
// ============================================================================

IHotkeyProvider *createHotkeyProvider(QObject *parent)
{
#ifdef _WIN32
    return new Win32HotkeyProvider(parent);
#else
    return new NullHotkeyProvider(parent);
#endif
}

IWindowProvider *createWindowProvider(QObject *parent)
{
#ifdef _WIN32
    return new Win32WindowProvider(parent);
#else
    return new NullWindowProvider(parent);
#endif
}

ICursorProvider *createCursorProvider(QObject *parent)
{
#ifdef _WIN32
    return new Win32CursorProvider(parent);
#else
    return new NullCursorProvider(parent);
#endif
}

ICancelProvider *createCancelProvider(QObject *parent)
{
#ifdef _WIN32
    return new Win32CancelProvider(parent);
#else
    return new NullCancelProvider(parent);
#endif
}

IClipCursorProvider *createClipCursorProvider(QObject *parent)
{
#ifdef _WIN32
    return new Win32ClipCursorProvider(parent);
#else
    return new NullClipCursorProvider(parent);
#endif
}
