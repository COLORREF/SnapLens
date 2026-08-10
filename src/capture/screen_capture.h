#ifndef SNAPLENS_CAPTURE_SCREEN_CAPTURE_H
#define SNAPLENS_CAPTURE_SCREEN_CAPTURE_H

#include <QScreen>
#include <QPixmap>
#include <QRect>
#include <QPointF>
#include <QVector>
#include <QGuiApplication>
#include <algorithm>
#include <vector>
#include <map>

struct ScreenShot {
    QScreen* screen = nullptr;
    QPixmap   pixmap;
    QRect     geometry;
    qreal     devicePixelRatio = 1.0;

    qreal dpr() const { return devicePixelRatio; }

    // Compute the physical-pixel origin of this screen within the virtual desktop.
    // This groups screens into columns/rows by spatial overlap and accumulates
    // physical pixel widths/heights of preceding groups. Correct for mixed-DPI
    // multi-monitor setups where logical coordinates do not match physical pixels.
    QPointF physicalOrigin() const {
        if (!screen) return QPointF();

        QRect my = screen->geometry();
        const auto allScreens = QGuiApplication::screens();

        // Build info for all other screens
        struct ScreenInfo {
            QRect geo;
            qreal dpr;
            double physW;
            double physH;
        };
        std::vector<ScreenInfo> others;
        for (auto* s : allScreens) {
            if (s == screen) continue;
            QRect g = s->geometry();
            qreal d = s->devicePixelRatio();
            others.push_back({g, d, g.width() * d, g.height() * d});
        }

        // Helper: group screens by overlapping axis range
        auto groupByAxis = [](const std::vector<ScreenInfo>& screens, bool axisX) {
            std::vector<std::vector<const ScreenInfo*>> groups;
            for (const auto& s : screens) {
                bool placed = false;
                for (auto& grp : groups) {
                    if (axisX) {
                        int grpLeft =  INT_MAX, grpRight = INT_MIN;
                        for (auto* m : grp) { grpLeft = std::min(grpLeft, m->geo.left()); grpRight = std::max(grpRight, m->geo.right()); }
                        if (s.geo.left() < grpRight && grpLeft < s.geo.right()) { grp.push_back(&s); placed = true; break; }
                    } else {
                        int grpTop = INT_MAX, grpBottom = INT_MIN;
                        for (auto* m : grp) { grpTop = std::min(grpTop, m->geo.top()); grpBottom = std::max(grpBottom, m->geo.bottom()); }
                        if (s.geo.top() < grpBottom && grpTop < s.geo.bottom()) { grp.push_back(&s); placed = true; break; }
                    }
                }
                if (!placed) groups.push_back({&s});
            }
            return groups;
        };

        // X offset: sum physical widths of screen columns to the left
        double x = 0;
        {
            std::vector<ScreenInfo> leftScreens;
            for (const auto& o : others) {
                if (o.geo.top() < my.bottom() && o.geo.bottom() > my.top()) {
                    if (o.geo.right() <= my.left()) leftScreens.push_back(o);
                }
            }
            for (auto& col : groupByAxis(leftScreens, true)) {
                double maxW = 0;
                for (auto* info : col) maxW = std::max(maxW, info->physW);
                x += maxW;
            }
        }

        // Y offset: sum physical heights of screen rows above
        double y = 0;
        {
            std::vector<ScreenInfo> topScreens;
            for (const auto& o : others) {
                if (o.geo.left() < my.right() && o.geo.right() > my.left()) {
                    if (o.geo.bottom() <= my.top()) topScreens.push_back(o);
                }
            }
            for (auto& row : groupByAxis(topScreens, false)) {
                double maxH = 0;
                for (auto* info : row) maxH = std::max(maxH, info->physH);
                y += maxH;
            }
        }

        return QPointF(x, y);
    }
};

class CapturedDesktop {
public:
    CapturedDesktop();

    QVector<ScreenShot> shots;

    static QPixmap crop(const ScreenShot& shot, const QRect& localRect);
};

#endif // SNAPLENS_CAPTURE_SCREEN_CAPTURE_H