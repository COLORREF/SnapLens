#ifndef SNAPLENS_PLATFORM_WINDOW_ENUM_H
#define SNAPLENS_PLATFORM_WINDOW_ENUM_H

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <dwmapi.h>

#include "platform_base.h"

// Win32 顶层窗口枚举实现（EnumWindows + DWM 扩展边界，供"单击窗口截图"命中检测）
class Win32WindowProvider : public IWindowProvider
{
    Q_OBJECT

public:
    explicit Win32WindowProvider(QObject *parent = nullptr);

    int enumWindows() override;
    const QVector<WindowInfo> &items() const override { return m_items; }

private:
    static BOOL CALLBACK enum_proc(HWND hwnd, LPARAM lparam);

    QVector<WindowInfo> m_items;

#ifndef DWMWA_EXTENDED_FRAME_BOUNDS
    static constexpr int DWMWA_EXTENDED_FRAME_BOUNDS = 9;
#endif
#ifndef DWMWA_CLOAKED
    static constexpr int DWMWA_CLOAKED = 14;
#endif
};

#endif // SNAPLENS_PLATFORM_WINDOW_ENUM_H
