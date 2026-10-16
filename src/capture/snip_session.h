#ifndef SNAPLENS_CAPTURE_SNIP_SESSION_H
#define SNAPLENS_CAPTURE_SNIP_SESSION_H

#include <QObject>
#include <QRect>
#include <QPixmap>
#include <QVector>
#include <functional>

class CapturedDesktop;
class SnipOverlay;
class ICancelProvider;
class IClipCursorProvider;
class ICursorProvider;
class IWindowProvider;
class Settings;

/** @brief 一次截图会话，管理多屏覆盖层、Esc 拦截和光标约束。
 * @details 会话只能启动一次；finished 后调用方应通过 deleteLater 释放对象。
 */
class SnipSession : public QObject
{
    Q_OBJECT

public:
    /// 回调签名：void(QPixmap)
    using PixmapCallback = std::function<void(QPixmap)>;

    SnipSession(PixmapCallback onSave,
                PixmapCallback onPin,
                PixmapCallback onCopy,
                PixmapCallback onTranslate = nullptr,
                PixmapCallback onOcr = nullptr,
                const Settings *settings = nullptr,
                QObject *parent = nullptr);

    ~SnipSession() override;

    /** @brief 抓取桌面并显示覆盖层；失败时结束会话并返回 false。 */
    bool start();
    /** @brief 幂等释放覆盖层和系统输入约束，并发送 finished。 */
    void close();

signals:
    void finished();

private slots:
    void onSelectionStarted(SnipOverlay *overlay);
    void onRegionSelected(SnipOverlay *overlay, QRect rect);
    void onAction(const QString &action);

private:
    PixmapCallback m_onSave;
    PixmapCallback m_onPin;
    PixmapCallback m_onCopy;
    PixmapCallback m_onTranslate;
    PixmapCallback m_onOcr;
    const Settings *m_settings = nullptr;

    CapturedDesktop         *m_desktop = nullptr;
    QVector<SnipOverlay *>   m_overlays;
    SnipOverlay             *m_activeOverlay = nullptr;
    QRect                    m_activeRect;
    bool                     m_closed = false;

    ICancelProvider         *m_cancelProvider = nullptr;
    IClipCursorProvider     *m_clipProvider = nullptr;
    ICursorProvider         *m_cursorProvider = nullptr;
    IWindowProvider         *m_windowProvider = nullptr;
};

#endif // SNAPLENS_CAPTURE_SNIP_SESSION_H
