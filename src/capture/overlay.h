#ifndef SNAPLENS_CAPTURE_OVERLAY_H
#define SNAPLENS_CAPTURE_OVERLAY_H

#include <QWidget>
#include <QPoint>
#include <QRect>
#include <QRectF>
#include <QPointF>
#include <QSizeF>
#include <QString>
#include <QParallelAnimationGroup>
#include <QVariantAnimation>

#include "screen_capture.h"
#include "platform/platform_base.h"

class SnipToolbar;
class Settings;
class ICursorProvider;

class SnipOverlay : public QWidget
{
    Q_OBJECT

public:
    enum State { Idle, Dragging, Selected };

    SnipOverlay(const ScreenShot &shot,
                const QVector<WindowInfo> &windows,
                ICursorProvider *cursorProvider,
                const Settings *settings,
                QWidget *parent = nullptr);

    void clearSelection();
    void syncCursorPosition();

    // 公开数据（SnipSession 需要访问）
    ScreenShot m_shot;

signals:
    void selectionStarted(SnipOverlay *overlay);
    void regionSelected(SnipOverlay *overlay, QRect rect);
    void cancelled();
    void actionChosen(const QString &action);
    void cursorNudged();

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void keyReleaseEvent(QKeyEvent *event) override;

private:
    // ---- 坐标换算 ----
    qreal dpr() const;
    void  localToPhys(qreal lx, qreal ly, double &px, double &py) const;
    void  cursorPixmapPos(int &px, int &py) const;
    QRectF physToLocal(int left, int top, int right, int bottom) const;

    // ---- 状态管理 ----
    void enterSelected(const QRect &rect);
    void removeToolbar();
    void showToolbar();
    void updateHover();

    // ---- 绘制层 ----
    void drawBackground(QPainter &painter);
    void drawSelectionHover(QPainter &painter);
    void drawIdleOverlays(QPainter &painter);
    void drawHint(QPainter &painter);
    void drawSizeLabel(QPainter &painter, const QRectF &sel);
    void drawCrosshairLines(QPainter &painter, qreal cx, qreal cy);
    bool moveCursorWithKey(QKeyEvent* event);

    // ---- 信息标签 ----
    void drawInfoLabel(QPainter &painter, qreal cx, qreal cy, int gap = 12);
    void drawInfoLabelRects(QPainter &painter,
                            const QString &coord, const QString &color,
                            QPointF coordPos, QPointF colorPos,
                            QSizeF coordSize, QSizeF colorSize,
                            QPointF hintPos, QSizeF hintSize);
    QString coordText() const;
    QString colorText() const;
    QString colorShortcutText() const;
    QSizeF colorLabelSize(const QFontMetrics& metrics, const QString& color) const;
    QColor pixelColor(int px, int py) const;
    QString formatColorText(int r, int g, int b) const;
    QString formatColorCopy(int r, int g, int b) const;
    bool matchCopyColorKey(QKeyEvent *event) const;

    // ---- 放大镜 (inline drawing like Python) ----
    struct MgFrame {
        double zoom = 4.0;
        int    half = 15;
        double dispW = 0, dispH = 0;     // display size in logical px
        double mgX = 0, mgY = 0;         // target position in logical px
        int    srcLeft = 0, srcTop = 0, srcW = 0, srcH = 0;
        int    physCx = 0, physCy = 0;
        bool   isPad = false;
        int    fullLeft = 0, fullTop = 0, fullW = 0, fullH = 0;
        bool   hasLabels = false;
        QString coordText, colorText;
        double zoomLabelH = 0;
    };
    void drawMagnifierInline(QPainter &painter, qreal cx, qreal cy);
    MgFrame prepareMgFrame(qreal cx, qreal cy);
    void drawMgPixels(QPainter &painter, const MgFrame &f);
    void drawMgOverlays(QPainter &painter, const MgFrame &f);
    void drawMgLabels(QPainter &painter, const MgFrame &f);
    void drawZoomLabel(QPainter &painter, double mgX, double mgY, double zoom);

    // ---- 放大镜动画 ----
    void animateLabels(const MgFrame &f);
    void computeLabelTargets(const MgFrame &f, QPainter &painter);
    void applyMgDisplayPos(MgFrame &f);
    void onLabelAnimDone();

    void updateMagnifierPosition(int cx, int cy);
    void positionMagnifier(int cx, int cy);

    // ---- 数据 ----
    QVector<WindowInfo> m_windows;
    ICursorProvider    *m_cursorProvider = nullptr;

    // ---- 状态 ----
    State  m_state = Idle;
    QPoint m_anchor;
    QPointF m_cursor;
    QRect  m_selection;
    QRectF m_hover;
    SnipToolbar     *m_toolbar = nullptr;

    // ---- 放大镜动画状态 ----
    QPointF m_mgPos{-1, -1};       // current animated mag position
    QPointF m_coordPos{-1, -1};    // current animated coord label pos
    QPointF m_colorPos{-1, -1};    // current animated color label pos
    QPointF m_hintPos{-1, -1};
    QPointF m_hintTarget;
    QSizeF m_hintSize;
    QPointF m_coordTarget;         // target coord label pos
    QPointF m_colorTarget;         // target color label pos
    QSizeF  m_coordSize;           // coord label size
    QSizeF  m_colorSize;           // color label size
    int     m_mgSideX = 0;         // previous mag side X (±1)
    int     m_mgSideY = 0;         // previous mag side Y (±1)
    bool    m_labelNeedsAnim = false; // zoom-change → animate labels
    bool    m_flipAnim = false;       // side-flip → animate mag+labels
    QParallelAnimationGroup *m_labelAnimGroup = nullptr;

    // ---- 设置（快照） ----
    double m_toolbarOpacity = 1.0;
    // 十字准星
    bool   m_crosshairEnabled = true;
    QString m_crosshairColorStr = QStringLiteral("#FFFFFF");
    int    m_crosshairAlpha = 47;
    int    m_crosshairThickness = 1;
    bool   m_crosshairInvert = true;
    bool   m_cursorEnabled = false;
    bool m_cursorKeyboardEnabled = true;
    QString m_cursorKeyboardMode = QStringLiteral("arrows");
    // 放大镜
    bool   m_magnifierEnabled = true;
    double m_magnifierZoom = 4.0;
    int    m_magnifierSize = 15;
    bool   m_magnifierWheelZoom = true;
    QString m_magnifierCrossColorStr = QStringLiteral("#DC1414");
    int    m_magnifierCrossAlpha = 100;
    int    m_magnifierCrossThickness = 1;
    bool   m_magnifierCrossInvert = false;
    bool   m_gridEnabled = true;
    QString m_gridColorStr = QStringLiteral("#FFFFFF");
    int    m_gridAlpha = 8;
    QString m_edgeMode = QStringLiteral("pad");
    QString m_edgeColorStr = QStringLiteral("#000000");
    bool   m_zoomLabelEnabled = true;
    QString m_zoomLabelTextColorStr = QStringLiteral("#FFFFFF");
    int    m_zoomLabelTextAlpha = 100;
    QString m_zoomLabelBgColorStr = QStringLiteral("#000000");
    int    m_zoomLabelBgAlpha = 67;
    // 坐标 / 颜色
    bool   m_coordLabelEnabled = true;
    QString m_coordLabelTextColorStr = QStringLiteral("#FFFFFF");
    int    m_coordLabelTextAlpha = 100;
    QString m_coordLabelBgColorStr = QStringLiteral("#000000");
    int    m_coordLabelBgAlpha = 67;
    bool   m_colorLabelEnabled = true;
    QString m_colorLabelTextColorStr = QStringLiteral("#FFFFFF");
    int    m_colorLabelTextAlpha = 100;
    QString m_colorLabelBgColorStr = QStringLiteral("#000000");
    int    m_colorLabelBgAlpha = 67;
    bool m_colorSwatchEnabled = true;
    QString m_colorFormatKey = QStringLiteral("Shift");
    QString m_colorFormat = QStringLiteral("rgb");
    QString m_copyColorKey = QStringLiteral("C");
    bool m_colorShiftPending = false;
    bool   m_copyHexPrefix = true;
    bool   m_copyRgbPrefix = true;

    static const int MIN_DRAG = 4;
};

#endif // SNAPLENS_CAPTURE_OVERLAY_H
