#ifndef SNAPLENS_PLATFORM_BASE_H
#define SNAPLENS_PLATFORM_BASE_H

// ============================================================================
// 平台能力抽象接口层（对齐 Python snaplens/platform/base.py）
//
// 所有平台实现（Windows/macOS/Linux）遵守同一契约，上层代码只依赖本文件
// 定义的接口 + platform_factory.h 的工厂函数，不感知具体平台。
//
// 接入新平台：
//   1. 新建实现类继承对应接口；
//   2. 在 platform_factory.cpp 的工厂函数中按平台宏登记（当前仅 _WIN32）。
//
// 平台能力缺失时由 NullXxx 占位实现优雅降级（对齐 Python NullXxx 系列）。
// ============================================================================

#include <QObject>
#include <QString>
#include <QRect>
#include <QVector>
#include <QCursor>
#include <QScreen>
#include <QGuiApplication>

// ----------------------------------------------------------------------------
// 窗口信息（跨平台结构：句柄用 qintptr，标题用 QString）
// ----------------------------------------------------------------------------
struct WindowInfo {
    qintptr handle = 0;   // 平台窗口句柄（Win32: HWND；macOS: NSWindow*；X11: Window）
    QRect   rect;         // 物理像素坐标
    QString title;
};

// ============================================================================
// IHotkeyProvider — 全局热键（对应 Python HotkeyProvider）
// ============================================================================
class IHotkeyProvider : public QObject {
    Q_OBJECT
public:
    explicit IHotkeyProvider(QObject* parent = nullptr) : QObject(parent) {}
    ~IHotkeyProvider() override = default;

    // sequenceText 为 QKeySequence PortableText（如 "Ctrl+Alt+A"），
    // 各平台后端自行解析。成功返回 true；失败多为键位冲突或键值不支持。
    virtual bool start(const QString& sequenceText) = 0;
    virtual void stop() = 0;

signals:
    void triggered();
};

class NullHotkeyProvider : public IHotkeyProvider {
    Q_OBJECT
public:
    using IHotkeyProvider::IHotkeyProvider;
    bool start(const QString&) override { return false; }
    void stop() override {}
};

// ============================================================================
// IWindowProvider — 顶层窗口枚举（对应 Python WindowProvider）
// ============================================================================
class IWindowProvider : public QObject {
    Q_OBJECT
public:
    explicit IWindowProvider(QObject* parent = nullptr) : QObject(parent) {}
    ~IWindowProvider() override = default;

    // 枚举顶层窗口，返回窗口数量。items() 为 Z 序自上而下。
    virtual int enumWindows() = 0;
    virtual const QVector<WindowInfo>& items() const = 0;
};

class NullWindowProvider : public IWindowProvider {
    Q_OBJECT
public:
    using IWindowProvider::IWindowProvider;
    int enumWindows() override { return 0; }
    const QVector<WindowInfo>& items() const override {
        static const QVector<WindowInfo> s_empty;
        return s_empty;
    }
};

// ============================================================================
// ICursorProvider — 鼠标物理像素坐标（对应 Python CursorProvider）
// ============================================================================
class ICursorProvider : public QObject {
    Q_OBJECT
public:
    explicit ICursorProvider(QObject* parent = nullptr) : QObject(parent) {}
    ~ICursorProvider() override = default;

    // 返回虚拟桌面物理像素坐标（与截图像素坐标系一致）。
    virtual void getPos(int& x, int& y) = 0;
};

class NullCursorProvider : public ICursorProvider {
    Q_OBJECT
public:
    using ICursorProvider::ICursorProvider;

    // 用 Qt 逻辑坐标 × DPR 近似（对齐 Python NullCursorProvider），
    // 在非整数 DPR 下会跳过部分物理像素。
    void getPos(int& x, int& y) override {
        QPoint gpos = QCursor::pos();
        QScreen* screen = QGuiApplication::screenAt(gpos);
        if (!screen) { x = 0; y = 0; return; }
        qreal dpr = screen->devicePixelRatio();
        QPoint local = gpos - screen->geometry().topLeft();
        x = qRound(local.x() * dpr);
        y = qRound(local.y() * dpr);
    }
};

// ============================================================================
// ICancelProvider — 截图取消拦截（对应 Python CancelProvider）
// ============================================================================
class ICancelProvider : public QObject {
    Q_OBJECT
public:
    explicit ICancelProvider(QObject* parent = nullptr) : QObject(parent) {}
    ~ICancelProvider() override = default;

    // 安装原生 Esc 键拦截，成功返回 true。触发后由上层解除（uninstall）。
    virtual bool install() = 0;
    virtual void uninstall() = 0;

signals:
    void escPressed();
};

class NullCancelProvider : public ICancelProvider {
    Q_OBJECT
public:
    using ICancelProvider::ICancelProvider;
    bool install() override { return false; }
    void uninstall() override {}
};

// ============================================================================
// IClipCursorProvider — 光标移动范围限制（对应 Python ClipCursorProvider）
// ============================================================================
class IClipCursorProvider : public QObject {
    Q_OBJECT
public:
    explicit IClipCursorProvider(QObject* parent = nullptr) : QObject(parent) {}
    ~IClipCursorProvider() override = default;

    // 将光标移动限制在指定物理像素矩形内，成功返回 true。
    virtual bool clipToScreen(int left, int top, int right, int bottom) = 0;
    virtual void release() = 0;
};

class NullClipCursorProvider : public IClipCursorProvider {
    Q_OBJECT
public:
    using IClipCursorProvider::IClipCursorProvider;
    bool clipToScreen(int, int, int, int) override { return false; }
    void release() override {}
};

#endif // SNAPLENS_PLATFORM_BASE_H
