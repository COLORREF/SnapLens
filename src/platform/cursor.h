#ifndef SNAPLENS_PLATFORM_CURSOR_H
#define SNAPLENS_PLATFORM_CURSOR_H

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Windows.h>

#include "platform_base.h"

// Win32 光标坐标实现（GetCursorPos，物理像素）
class Win32CursorProvider : public ICursorProvider
{
    Q_OBJECT

public:
    explicit Win32CursorProvider(QObject *parent = nullptr);

    void getPos(int &x, int &y) override;
    bool setPos(int x, int y) override;
};

// Win32 光标范围限制实现（ClipCursor）
class Win32ClipCursorProvider : public IClipCursorProvider
{
    Q_OBJECT

public:
    explicit Win32ClipCursorProvider(QObject *parent = nullptr);

    bool clipToScreen(int left, int top, int right, int bottom) override;
    void release() override;
};

#endif // SNAPLENS_PLATFORM_CURSOR_H
