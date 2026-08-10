#ifndef SNAPLENS_PLATFORM_FACTORY_H
#define SNAPLENS_PLATFORM_FACTORY_H

// ============================================================================
// 平台能力工厂（对齐 Python snaplens/platform/__init__.py）
//
// 上层代码只通过这里创建平台对象，按平台宏分发：
//   - _WIN32              → Win32 原生实现
//   - 其他平台（暂未实现） → NullXxx 占位实现（优雅降级，不崩溃）
// ============================================================================

class QObject;
class IHotkeyProvider;
class IWindowProvider;
class ICursorProvider;
class ICancelProvider;
class IClipCursorProvider;

IHotkeyProvider*     createHotkeyProvider(QObject* parent = nullptr);
IWindowProvider*     createWindowProvider(QObject* parent = nullptr);
ICursorProvider*     createCursorProvider(QObject* parent = nullptr);
ICancelProvider*     createCancelProvider(QObject* parent = nullptr);
IClipCursorProvider* createClipCursorProvider(QObject* parent = nullptr);

#endif // SNAPLENS_PLATFORM_FACTORY_H
