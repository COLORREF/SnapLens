#include "window_enum.h"
#include "log/log_manager.h"

Win32WindowProvider::Win32WindowProvider(QObject *parent)
    : IWindowProvider(parent)
{
}

BOOL CALLBACK Win32WindowProvider::enum_proc(HWND hwnd, LPARAM lparam)
{
    auto *self = reinterpret_cast<Win32WindowProvider *>(lparam);

    if (!IsWindowVisible(hwnd) || IsIconic(hwnd))
        return TRUE;

    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == GetCurrentProcessId())
        return TRUE;

    BOOL cloaked = FALSE;
    if (DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked)) == S_OK)
    {
        if (cloaked)
            return TRUE;
    }

    RECT rect{};
    if (DwmGetWindowAttribute(hwnd, DWMWA_EXTENDED_FRAME_BOUNDS,
                              &rect, sizeof(rect)) != S_OK)
    {
        if (!GetWindowRect(hwnd, &rect))
            return TRUE;
    }

    if (rect.right - rect.left < 8 || rect.bottom - rect.top < 8)
        return TRUE;

    wchar_t title[512] = {};
    GetWindowTextW(hwnd, title, 512);

    WindowInfo info;
    info.handle = reinterpret_cast<qintptr>(hwnd);
    info.rect = QRect(rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top);
    info.title = QString::fromWCharArray(title);
    self->m_items.push_back(info);
    return TRUE;
}

int Win32WindowProvider::enumWindows()
{
    SNAP_LOG_DEBUG(QStringLiteral("Win32WindowProvider::enumWindows: starting"));
    m_items.clear();
    EnumWindows(&Win32WindowProvider::enum_proc, reinterpret_cast<LPARAM>(this));
    int count = static_cast<int>(m_items.size());
    SNAP_LOG_INFO(QStringLiteral("Win32WindowProvider::enumWindows: count=%1").arg(count));
    return count;
}
