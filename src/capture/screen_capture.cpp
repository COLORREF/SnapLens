#include "screen_capture.h"
#include <QGuiApplication>
#include <QPoint>
#include <algorithm>

CapturedDesktop::CapturedDesktop()
{
    const auto screens = QGuiApplication::screens();
    for (QScreen* screen : screens) {
        QPixmap pixmap = screen->grabWindow(0);
        if (pixmap.isNull())
            continue;
        ScreenShot shot;
        shot.screen = screen;
        shot.pixmap = pixmap;
        shot.geometry = screen->geometry();
        shot.devicePixelRatio = screen->devicePixelRatio();
        shots.append(shot);
    }
}

QPixmap CapturedDesktop::crop(const ScreenShot& shot, const QRect& localRect)
{
    qreal dpr = shot.dpr();
    QRect phys = QRectF(
        localRect.x() * dpr, localRect.y() * dpr,
        localRect.width() * dpr, localRect.height() * dpr
    ).toAlignedRect().intersected(QRect(QPoint(0, 0), shot.pixmap.size()));
    if (phys.isEmpty()) return {}; // Qt copy(empty) otherwise copies the whole screen.
    QPixmap out = shot.pixmap.copy(phys);
    out.setDevicePixelRatio(dpr);
    return out;
}