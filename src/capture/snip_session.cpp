#include "snip_session.h"
#include "screen_capture.h"
#include "overlay.h"

#include "platform/platform_base.h"
#include "platform/platform_factory.h"
#include "log/log_manager.h"

SnipSession::SnipSession(PixmapCallback onSave,
                         PixmapCallback onPin,
                         PixmapCallback onCopy,
                         PixmapCallback onTranslate,
                         PixmapCallback onOcr,
                         const Settings *settings,
                         QObject *parent)
    : QObject(parent)
    , m_onSave(std::move(onSave))
    , m_onPin(std::move(onPin))
    , m_onCopy(std::move(onCopy))
    , m_onTranslate(std::move(onTranslate))
    , m_onOcr(std::move(onOcr))
    , m_settings(settings)
{
}

SnipSession::~SnipSession()
{
    close();
}

// ============================================================================
// start
// ============================================================================

bool SnipSession::start()
{
    m_desktop = new CapturedDesktop();
    if (m_desktop->shots.isEmpty()) {
        delete m_desktop;
        m_desktop = nullptr;
        emit finished();
        return false;
    }

    // 枚举窗口（通过工厂创建平台对象）
    m_windowProvider = createWindowProvider(this);
    m_windowProvider->enumWindows();
    const QVector<WindowInfo> &windows = m_windowProvider->items();

    // 光标坐标（供 overlay 放大镜/坐标标签使用）
    m_cursorProvider = createCursorProvider(this);

    // 每屏一个覆盖层
    for (const auto &shot : m_desktop->shots) {
        auto *overlay = new SnipOverlay(shot, windows, m_cursorProvider, m_settings);
        connect(overlay, &SnipOverlay::selectionStarted,
                this, &SnipSession::onSelectionStarted);
        connect(overlay, &SnipOverlay::regionSelected,
                this, &SnipSession::onRegionSelected);
        connect(overlay, &SnipOverlay::cancelled,
                this, &SnipSession::close);
        connect(overlay, &SnipOverlay::actionChosen,
                this, &SnipSession::onAction);
        m_overlays.append(overlay);
    }

    for (auto *overlay : m_overlays)
        overlay->show();

    // Esc 拦截
    m_cancelProvider = createCancelProvider(this);
    connect(m_cancelProvider, &ICancelProvider::escPressed, this, &SnipSession::close);
    m_cancelProvider->install();

    // 光标限制
    m_clipProvider = createClipCursorProvider(this);

    // 激活窗口并抓取键盘
    m_overlays.first()->raise();
    m_overlays.first()->activateWindow();
    m_overlays.first()->setFocus();
    m_overlays.first()->grabKeyboard();

    SNAP_LOG_DEBUG(QStringLiteral("SnipSession started with %1 overlay(s)").arg(m_overlays.size()));
    return true;
}

// ============================================================================
// 覆盖层信号
// ============================================================================

void SnipSession::onSelectionStarted(SnipOverlay *overlay)
{
    // 锁定光标在当前屏幕范围内（物理像素）
    if (m_clipProvider) {
        QPointF origin = overlay->m_shot.physicalOrigin();
        const QPixmap &pm = overlay->m_shot.pixmap;
        int left   = static_cast<int>(origin.x());
        int top    = static_cast<int>(origin.y());
        // ClipCursor 的 RECT 为半开区间（排他 right/bottom）：
        // 必须传 left+width / top+height，否则光标可活动范围右/下少 1 像素。
        int right  = left + pm.width();
        int bottom = top + pm.height();
        m_clipProvider->clipToScreen(left, top, right, bottom);
    }
    for (auto *other : m_overlays) {
        if (other != overlay)
            other->clearSelection();
    }
}

void SnipSession::onRegionSelected(SnipOverlay *overlay, QRect rect)
{
    for (auto *other : m_overlays) {
        if (other != overlay) {
            other->clearSelection();
            other->hide();
        }
    }
    m_activeOverlay = overlay;
    m_activeRect = rect;
}

void SnipSession::onAction(const QString &action)
{
    if (action == QStringLiteral("cancel") || !m_activeOverlay) {
        close();
        return;
    }

    // 先裁剪，再关闭覆盖层，最后执行动作
    QPixmap pixmap = m_desktop->crop(m_activeOverlay->m_shot, m_activeRect);
    close();

    if (action == QStringLiteral("save") && m_onSave) {
        m_onSave(pixmap);
    } else if (action == QStringLiteral("pin") && m_onPin) {
        m_onPin(pixmap);
    } else if (action == QStringLiteral("copy") && m_onCopy) {
        m_onCopy(pixmap);
    } else if (action == QStringLiteral("translate") && m_onTranslate) {
        m_onTranslate(pixmap);
    } else if (action == QStringLiteral("ocr") && m_onOcr) {
        m_onOcr(pixmap);
    }
}

// ============================================================================
// close
// ============================================================================

void SnipSession::close()
{
    if (m_closed)
        return;
    m_closed = true;

    // 先卸载 Esc 拦截
    if (m_cancelProvider) {
        m_cancelProvider->uninstall();
        m_cancelProvider = nullptr;
    }

    // 释放光标限制
    if (m_clipProvider) {
        m_clipProvider->release();
        m_clipProvider = nullptr;
    }

    for (auto *overlay : m_overlays) {
        overlay->releaseKeyboard();
        overlay->close();
    }
    m_overlays.clear();

    delete m_desktop;
    m_desktop = nullptr;

    emit finished();
}