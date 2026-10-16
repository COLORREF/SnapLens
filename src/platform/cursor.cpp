#include "cursor.h"
#include "log/log_manager.h"

// ============================================================================
// Win32CursorProvider
// ============================================================================

Win32CursorProvider::Win32CursorProvider(QObject *parent)
    : ICursorProvider(parent)
{
}

void Win32CursorProvider::getPos(int &x, int &y)
{
    POINT pt;
    if (GetCursorPos(&pt))
    {
        x = pt.x;
        y = pt.y;
    }
    else
    {
        x = 0;
        y = 0;
        SNAP_LOG_WARNING(QStringLiteral("Win32CursorProvider::getPos: GetCursorPos failed"));
    }
}

// ============================================================================
// Win32ClipCursorProvider
// ============================================================================

bool Win32CursorProvider::setPos(int x, int y)
{
    return SetCursorPos(x, y) != FALSE;
}

Win32ClipCursorProvider::Win32ClipCursorProvider(QObject *parent)
    : IClipCursorProvider(parent)
{
}

bool Win32ClipCursorProvider::clipToScreen(int left, int top, int right, int bottom)
{
    RECT rect = {left, top, right, bottom};
    if (ClipCursor(&rect))
    {
        SNAP_LOG_DEBUG(QStringLiteral("Win32ClipCursorProvider::clipToScreen: (%1,%2)-(%3,%4)")
                           .arg(left).arg(top).arg(right).arg(bottom));
        return true;
    }
    SNAP_LOG_WARNING(QStringLiteral("Win32ClipCursorProvider::clipToScreen: ClipCursor failed"));
    return false;
}

void Win32ClipCursorProvider::release()
{
    SNAP_LOG_DEBUG(QStringLiteral("Win32ClipCursorProvider::release"));
    ClipCursor(nullptr);
}
