#ifndef SNAPLENS_PLATFORM_HOTKEY_H
#define SNAPLENS_PLATFORM_HOTKEY_H

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <QObject>
#include <QThread>
#include <Windows.h>

#include "platform_base.h"

// Win32 全局热键实现（RegisterHotKey + 独立线程消息窗）
class Win32HotkeyProvider : public IHotkeyProvider
{
    Q_OBJECT

public:
    explicit Win32HotkeyProvider(QObject* parent = nullptr);
    ~Win32HotkeyProvider() override;

    bool start(const QString& sequence_text) override;
    void stop() override;

private:
    bool parse_sequence(const QString& sequence_text, UINT& out_modifiers, UINT& out_vk);
    static UINT qt_modifiers_to_win32(Qt::KeyboardModifiers modifiers);
    static UINT qt_key_to_vk(Qt::Key key);

    class HotkeyThread;
    HotkeyThread* m_thread = nullptr;
    bool m_registered = false;
};

#endif // SNAPLENS_PLATFORM_HOTKEY_H