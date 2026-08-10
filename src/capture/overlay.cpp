#include "overlay.h"
#include "toolbar.h"

#include <QPainter>
#include <QPen>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QWheelEvent>
#include <QGuiApplication>
#include <QClipboard>
#include <QKeySequence>
#include <QColor>

#include "settings.h"
#include "platform/platform_base.h"
#include "log/log_manager.h"

// ============================================================================
// 常量
// ============================================================================

static const QColor SELECTION_COLOR(0, 120, 215);
static const QColor MASK_COLOR(0, 0, 0, 110);

// ============================================================================
// 构造 / 析构
// ============================================================================

SnipOverlay::SnipOverlay(const ScreenShot &shot,
                         const QVector<WindowInfo> &windows,
                         ICursorProvider *cursorProvider,
                         const Settings *settings,
                         QWidget *parent)
    : QWidget(parent,
              Qt::FramelessWindowHint
              | Qt::WindowStaysOnTopHint
              | Qt::Tool)
    , m_shot(shot)
    , m_windows(windows)
    , m_cursorProvider(cursorProvider)
{
    // ---- 快照所有设置 ----
    if (settings) {
        const auto &s = *settings;
        m_toolbarOpacity          = s.toolbar_opacity / 100.0;
        m_magnifierEnabled        = s.magnifier_enabled;
        m_magnifierZoom           = s.magnifier_wheel_zoom ? s.magnifier_zoom : s.magnifier_fixed_zoom;
        m_magnifierSize           = s.magnifier_size;
        m_magnifierCrossColorStr  = s.magnifier_cross_color;
        m_magnifierCrossAlpha     = s.magnifier_cross_alpha;
        m_magnifierCrossThickness = s.magnifier_cross_thickness;
        m_magnifierCrossInvert    = s.magnifier_cross_invert;
        m_crosshairColorStr       = s.crosshair_color;
        m_crosshairAlpha          = s.crosshair_alpha;
        m_crosshairThickness      = s.crosshair_thickness;
        m_crosshairInvert         = s.crosshair_invert;
        m_crosshairEnabled        = s.crosshair_enabled;
        m_cursorEnabled           = s.cursor_enabled;
        m_gridEnabled             = s.grid_enabled;
        m_gridColorStr            = s.grid_color;
        m_gridAlpha               = s.grid_alpha;
        m_magnifierWheelZoom      = s.magnifier_wheel_zoom;
        m_edgeMode                = s.edge_mode;
        m_edgeColorStr            = s.edge_color;
        m_zoomLabelEnabled        = s.zoom_label_enabled;
        m_zoomLabelTextColorStr   = s.zoom_label_text_color;
        m_zoomLabelTextAlpha      = s.zoom_label_text_alpha;
        m_zoomLabelBgColorStr     = s.zoom_label_bg_color;
        m_zoomLabelBgAlpha        = s.zoom_label_bg_alpha;
        m_coordLabelEnabled       = s.coord_label_enabled;
        m_coordLabelTextColorStr  = s.coord_label_text_color;
        m_coordLabelTextAlpha     = s.coord_label_text_alpha;
        m_coordLabelBgColorStr    = s.coord_label_bg_color;
        m_coordLabelBgAlpha       = s.coord_label_bg_alpha;
        m_colorLabelEnabled       = s.color_label_enabled;
        m_colorLabelTextColorStr  = s.color_label_text_color;
        m_colorLabelTextAlpha     = s.color_label_text_alpha;
        m_colorLabelBgColorStr    = s.color_label_bg_color;
        m_colorLabelBgAlpha       = s.color_label_bg_alpha;
        m_colorFormat             = s.color_format;
        m_copyColorKey            = s.copy_color_key;
        m_copyHexPrefix           = s.copy_hex_prefix;
        m_copyRgbPrefix           = s.copy_rgb_prefix;
    }

    setAttribute(Qt::WA_DeleteOnClose);
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);

    if (m_cursorEnabled)
        setCursor(Qt::CrossCursor);
    else
        setCursor(Qt::BlankCursor);

    setGeometry(shot.geometry);
}

// ============================================================================
// 坐标换算
// ============================================================================

qreal SnipOverlay::dpr() const
{
    return m_shot.dpr();
}

void SnipOverlay::localToPhys(int lx, int ly, double &px, double &py) const
{
    qreal d = dpr();
    QPointF origin = m_shot.physicalOrigin();
    px = origin.x() + lx * d;
    py = origin.y() + ly * d;
}

void SnipOverlay::cursorPixmapPos(int &px, int &py) const
{
    qreal d = dpr();
    int cx = m_cursor.x();
    int cy = m_cursor.y();
    const QPixmap &pm = m_shot.pixmap;
    px = qBound(0, static_cast<int>(cx * d), pm.width() - 1);
    py = qBound(0, static_cast<int>(cy * d), pm.height() - 1);
}

QRectF SnipOverlay::physToLocal(int left, int top, int right, int bottom) const
{
    qreal d = dpr();
    QPointF origin = m_shot.physicalOrigin();
    return QRectF((left - origin.x()) / d, (top - origin.y()) / d,
                  (right - left) / d, (bottom - top) / d);
}

// ============================================================================
// 状态切换
// ============================================================================

void SnipOverlay::clearSelection()
{
    m_state = Idle;
    m_selection = QRect();
    removeToolbar();
    setCursor(m_cursorEnabled ? Qt::CrossCursor : Qt::BlankCursor);
    update();
}

void SnipOverlay::enterSelected(const QRect &rect)
{
    m_selection = rect;
    m_state = Selected;
    showToolbar();
    setCursor(Qt::ArrowCursor);
    emit regionSelected(this, m_selection);
    update();
}

void SnipOverlay::removeToolbar()
{
    if (m_toolbar) {
        m_toolbar->deleteLater();
        m_toolbar = nullptr;
    }
}

void SnipOverlay::showToolbar()
{
    removeToolbar();
    auto *tb = new SnipToolbar(m_toolbarOpacity, this);
    connect(tb, &SnipToolbar::actionChosen, this, &SnipOverlay::actionChosen);
    tb->adjustSize();
    QSize size = tb->size();
    const QRect &sel = m_selection;

    int x = qMin(sel.right() - size.width() + 1, width() - size.width() - 4);
    x = qMax(4, x);
    int y = sel.bottom() + 6;
    if (y + size.height() > height() - 4)
        y = sel.top() - size.height() - 6;
    if (y < 4)
        y = qMax(4, qMin(sel.bottom() - size.height() - 4, height() - size.height() - 4));

    tb->move(x, y);
    tb->show();
    tb->raise();
    m_toolbar = tb;
}

// ============================================================================
// 鼠标事件
// ============================================================================

void SnipOverlay::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton)
        return;
    emit selectionStarted(this);
    removeToolbar();
    m_state = Dragging;
    m_anchor = event->pos();
    m_cursor = m_anchor;
    m_selection = QRect();
    update();
}

void SnipOverlay::mouseMoveEvent(QMouseEvent *event)
{
    m_cursor = event->pos();
    if (m_state == Dragging) {
        m_selection = QRect(m_anchor, m_cursor).normalized().intersected(rect());
    } else if (m_state == Idle) {
        updateHover();
    }
    update();
}

void SnipOverlay::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton || m_state != Dragging)
        return;

    QPoint pt = event->pos();
    if ((pt - m_anchor).manhattanLength() < MIN_DRAG) {
        // 单击：优先取悬停窗口，否则整屏
        if (!m_hover.isNull() && !m_hover.isEmpty()) {
            enterSelected(m_hover.toAlignedRect());
        } else {
            enterSelected(rect());
        }
    } else {
        if (m_selection.isEmpty()) {
            m_state = Idle;
            update();
            return;
        }
        enterSelected(m_selection);
    }
}

void SnipOverlay::mouseDoubleClickEvent(QMouseEvent * /*event*/)
{
    if (m_state == Selected)
        emit actionChosen(QStringLiteral("copy"));
}

void SnipOverlay::wheelEvent(QWheelEvent *event)
{
    if (!m_magnifierWheelZoom || !m_magnifierEnabled || m_state == Selected)
        return;
    int delta = event->angleDelta().y();
    if (delta > 0)
        m_magnifierZoom = qMin(20.0, m_magnifierZoom + 0.5);
    else if (delta < 0)
        m_magnifierZoom = qMax(4.0, m_magnifierZoom - 0.5);
    if (delta != 0) {
        m_labelNeedsAnim = true;
        update();
    }
}

void SnipOverlay::updateHover()
{
    double px, py;
    localToPhys(m_cursor.x(), m_cursor.y(), px, py);
    m_hover = QRectF();

    for (const auto &win : m_windows) {
        const QRect &wr = win.rect;
        // QRect 为闭区间语义：命中检测需包含 right()/bottom()（最右/最下一像素）
        if (wr.left() <= px && px <= wr.right()
            && wr.top() <= py && py <= wr.bottom()) {
            // physToLocal 期望 RECT 排他边界（right/bottom 为"最后一个像素+1"），
            // QRect 的 right()/bottom() 已闭区间化（-1），需 +1 恢复排他值，
            // 否则高亮框宽度/高度少 1 物理像素。
            QRectF r = physToLocal(wr.left(), wr.top(), wr.right() + 1, wr.bottom() + 1)
                           .intersected(QRectF(rect()));
            if (!r.isEmpty())
                m_hover = r;
            break;
        }
    }
}

// ============================================================================
// 键盘事件
// ============================================================================

bool SnipOverlay::matchCopyColorKey(QKeyEvent *event) const
{
    QKeySequence seq(m_copyColorKey);
    if (seq.count() != 1)
        return false;
    QKeyCombination combo = seq[0];
    return (event->key() == combo.key()
            && event->modifiers() == combo.keyboardModifiers());
}

void SnipOverlay::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) {
        emit cancelled();
    } else if (m_state != Selected
               && m_colorLabelEnabled
               && matchCopyColorKey(event)) {
        // 复制像素颜色到剪贴板
        int px, py;
        cursorPixmapPos(px, py);
        QColor c = pixelColor(px, py);
        QGuiApplication::clipboard()->setText(formatColorCopy(c.red(), c.green(), c.blue()));
    } else if (m_state == Selected) {
        if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
            emit actionChosen(QStringLiteral("copy"));
        } else if (event->matches(QKeySequence::Save)) {
            emit actionChosen(QStringLiteral("save"));
        }
    }
}

// ============================================================================
// paintEvent 主入口
// ============================================================================

void SnipOverlay::paintEvent(QPaintEvent * /*event*/)
{
    QPainter painter(this);
    drawBackground(painter);
    drawSelectionHover(painter);
    drawIdleOverlays(painter);
    drawHint(painter);
}

// ============================================================================
// 层 1：背景
// ============================================================================

void SnipOverlay::drawBackground(QPainter &painter)
{
    painter.drawPixmap(rect(), m_shot.pixmap);
    painter.fillRect(rect(), MASK_COLOR);
}

// ============================================================================
// 层 2：选区 / 悬停窗口
// ============================================================================

void SnipOverlay::drawSelectionHover(QPainter &painter)
{
    const QPixmap &pixmap = m_shot.pixmap;
    qreal d = dpr();

    if ((m_state == Dragging || m_state == Selected) && !m_selection.isEmpty()) {
        QRectF sel = m_selection;
        QRectF src(sel.x() * d, sel.y() * d,
                   sel.width() * d, sel.height() * d);
        painter.drawPixmap(sel, pixmap, src);
        painter.setPen(QPen(SELECTION_COLOR, 1.5));
        painter.drawRect(sel);
        drawSizeLabel(painter, sel);
    } else if (m_state == Idle && !m_hover.isNull()) {
        QRectF r = m_hover;
        QRectF src(r.x() * d, r.y() * d,
                   r.width() * d, r.height() * d);
        painter.drawPixmap(r, pixmap, src);
        painter.setPen(QPen(SELECTION_COLOR, 1.5));
        painter.drawRect(r);
    }
}

// ============================================================================
// 层 3：准星 + 放大镜 / 标签
// ============================================================================

void SnipOverlay::drawIdleOverlays(QPainter &painter)
{
    if (m_state == Selected || !underMouse())
        return;

    int cx = m_cursor.x();
    int cy = m_cursor.y();
    drawCrosshairLines(painter, cx, cy);

    if (m_magnifierEnabled)
        drawMagnifierInline(painter, cx, cy);
    else
        drawInfoLabel(painter, cx, cy);
}

// ============================================================================
// 放大镜 — inline drawing (matching Python's approach to avoid widget lag)
// ============================================================================

SnipOverlay::MgFrame SnipOverlay::prepareMgFrame(int cx, int cy)
{
    MgFrame f;
    f.zoom = m_magnifierZoom;
    f.half = m_magnifierSize;

    int physCx, physCy;
    if (m_cursorProvider) m_cursorProvider->getPos(physCx, physCy); else { physCx = 0; physCy = 0; }
    physCx -= static_cast<int>(m_shot.physicalOrigin().x());
    physCy -= static_cast<int>(m_shot.physicalOrigin().y());

    const QPixmap &pm = m_shot.pixmap;
    int pmW = pm.width(), pmH = pm.height();
    f.physCx = qBound(0, physCx, pmW - 1);
    f.physCy = qBound(0, physCy, pmH - 1);

    f.isPad = (m_edgeMode == QStringLiteral("pad"));
    double zoom = f.zoom;
    int half = f.half;

    if (f.isPad) {
        f.fullLeft = f.physCx - half;
        f.fullTop  = f.physCy - half;
        f.fullW = 2 * half;
        f.fullH = 2 * half;
        f.srcLeft = qMax(0, f.fullLeft);
        f.srcTop  = qMax(0, f.fullTop);
        int srcRight  = qMin(pmW, f.physCx + half);
        int srcBottom = qMin(pmH, f.physCy + half);
        f.srcW = srcRight - f.srcLeft;
        f.srcH = srcBottom - f.srcTop;
        f.dispW = f.fullW * zoom;
        f.dispH = f.fullH * zoom;
    } else {
        f.srcLeft = qMax(0, f.physCx - half);
        f.srcTop  = qMax(0, f.physCy - half);
        int srcRight  = qMin(pmW, f.physCx + half);
        int srcBottom = qMin(pmH, f.physCy + half);
        f.srcW = srcRight - f.srcLeft;
        f.srcH = srcBottom - f.srcTop;
        if (f.srcW <= 0 || f.srcH <= 0)
            return f; // invalid frame
        f.dispW = f.srcW * zoom;
        f.dispH = f.srcH * zoom;
    }

    // Position: default to top-right of cursor, flip if out of bounds
    int gap = 40;
    double mgX = cx + gap;
    double mgY = cy - f.dispH - gap;
    if (mgX + f.dispW > width())
        mgX = cx - f.dispW - gap;
    if (mgY < 0)
        mgY = cy + gap;
    f.mgX = qBound(0.0, mgX, width() - f.dispW);
    f.mgY = qBound(0.0, mgY, height() - f.dispH);

    // Flip detection: magnifier crossed from one side of cursor to the other
    int newSideX = (f.mgX >= cx) ? 1 : -1;
    int newSideY = (f.mgY >= cy) ? 1 : -1;
    if (m_mgSideX != 0 && (m_mgSideX != newSideX || m_mgSideY != newSideY)) {
        m_flipAnim = true;
    }
    m_mgSideX = newSideX;
    m_mgSideY = newSideY;

    return f;
}

void SnipOverlay::drawMagnifierInline(QPainter &painter, int cx, int cy)
{
    MgFrame f = prepareMgFrame(cx, cy);
    if (f.dispW <= 0 || f.dispH <= 0)
        return;

    // Prepare label info
    QString coord = coordText();
    QString color = colorText();
    f.coordText = coord;
    f.colorText = color;
    f.hasLabels = !coord.isEmpty() || !color.isEmpty();

    QFontMetrics metrics = painter.fontMetrics();
    f.zoomLabelH = m_zoomLabelEnabled ? (metrics.height() + 6) : 0;

    // Phase: compute label targets → animate → apply display pos
    computeLabelTargets(f, painter);
    animateLabels(f);
    applyMgDisplayPos(f);

    // Render magnifier layers
    drawMgPixels(painter, f);
    drawMgOverlays(painter, f);
    drawMgLabels(painter, f);
}

void SnipOverlay::drawMgPixels(QPainter &painter, const MgFrame &f)
{
    const QPixmap &pixmap = m_shot.pixmap;
    double zoom = f.zoom;

    // Dark background
    painter.fillRect(QRectF(f.mgX, f.mgY, f.dispW, f.dispH), QColor(0, 0, 0, 220));

    if (f.isPad) {
        if (f.srcW > 0 && f.srcH > 0) {
            double offsetX = (f.srcLeft - f.fullLeft) * zoom;
            double offsetY = (f.srcTop - f.fullTop) * zoom;
            QColor edgeC(m_edgeColorStr);

            if (f.srcLeft > f.fullLeft)
                painter.fillRect(QRectF(f.mgX, f.mgY, offsetX, f.dispH), edgeC);
            if (f.srcLeft + f.srcW < f.fullLeft + f.fullW) {
                double rightGap = (f.fullLeft + f.fullW - f.srcLeft - f.srcW) * zoom;
                painter.fillRect(QRectF(f.mgX + f.dispW - rightGap, f.mgY, rightGap, f.dispH), edgeC);
            }
            if (f.srcTop > f.fullTop)
                painter.fillRect(QRectF(f.mgX, f.mgY, f.dispW, offsetY), edgeC);
            if (f.srcTop + f.srcH < f.fullTop + f.fullH) {
                double bottomGap = (f.fullTop + f.fullH - f.srcTop - f.srcH) * zoom;
                painter.fillRect(QRectF(f.mgX, f.mgY + f.dispH - bottomGap, f.dispW, bottomGap), edgeC);
            }

            QRectF srcRect(f.srcLeft, f.srcTop, f.srcW, f.srcH);
            QRectF dstRect(f.mgX + offsetX, f.mgY + offsetY, f.srcW * zoom, f.srcH * zoom);
            painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
            painter.drawPixmap(dstRect, pixmap, srcRect);
            painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
        } else {
            painter.fillRect(QRectF(f.mgX, f.mgY, f.dispW, f.dispH), QColor(m_edgeColorStr));
        }
    } else {
        QRectF srcRect(f.srcLeft, f.srcTop, f.srcW, f.srcH);
        QRectF dstRect(f.mgX, f.mgY, f.dispW, f.dispH);
        painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
        painter.drawPixmap(dstRect, pixmap, srcRect);
        painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    }
}

void SnipOverlay::drawMgOverlays(QPainter &painter, const MgFrame &f)
{
    double zoom = f.zoom;
    qreal d = dpr();

    // Pixel grid
    if (m_gridEnabled) {
        painter.setRenderHint(QPainter::Antialiasing, false);
        double gridWidth = 1.0 / d;
        QColor gridC(m_gridColorStr);
        gridC.setAlphaF(m_gridAlpha / 100.0);

        int totalCols = static_cast<int>(f.dispW / zoom);
        int totalRows = static_cast<int>(f.dispH / zoom);
        for (int col = 1; col < totalCols; ++col) {
            double x = f.mgX + col * zoom;
            painter.fillRect(QRectF(x, f.mgY, gridWidth, f.dispH), gridC);
        }
        for (int row = 1; row < totalRows; ++row) {
            double y = f.mgY + row * zoom;
            painter.fillRect(QRectF(f.mgX, y, f.dispW, gridWidth), gridC);
        }
    }

    // Center crosshair
    painter.setRenderHint(QPainter::Antialiasing, false);
    double centerX, centerY;
    if (f.isPad) {
        centerX = f.mgX + f.half * zoom + zoom / 2.0 + 0.5;
        centerY = f.mgY + f.half * zoom + zoom / 2.0 + 0.5;
    } else {
        centerX = f.mgX + (f.physCx - f.srcLeft) * zoom + zoom / 2.0 + 0.5;
        centerY = f.mgY + (f.physCy - f.srcTop) * zoom + zoom / 2.0 + 0.5;
    }

    if (m_magnifierCrossInvert) {
        painter.save();
        painter.setCompositionMode(QPainter::CompositionMode_Difference);
        QColor penC(255, 255, 255, static_cast<int>(255 * m_magnifierCrossAlpha / 100.0));
        painter.setPen(QPen(penC, m_magnifierCrossThickness / d));
        painter.drawLine(QPointF(centerX, f.mgY), QPointF(centerX, f.mgY + f.dispH));
        painter.drawLine(QPointF(f.mgX, centerY), QPointF(f.mgX + f.dispW, centerY));
        painter.restore();
    } else {
        QColor crossC(m_magnifierCrossColorStr);
        crossC.setAlphaF(m_magnifierCrossAlpha / 100.0);
        painter.setPen(QPen(crossC, m_magnifierCrossThickness / d));
        painter.drawLine(QPointF(centerX, f.mgY), QPointF(centerX, f.mgY + f.dispH));
        painter.drawLine(QPointF(f.mgX, centerY), QPointF(f.mgX + f.dispW, centerY));
    }
    painter.setRenderHint(QPainter::Antialiasing, true);

    // Border
    painter.setPen(QPen(SELECTION_COLOR, 1));
    painter.drawRect(QRectF(f.mgX, f.mgY, f.dispW, f.dispH));
}

// ============================================================================
// 放大镜动画系统 (matching Python's _animate_labels / _compute_label_targets)
// ============================================================================

void SnipOverlay::computeLabelTargets(const MgFrame &f, QPainter &painter)
{
    Q_UNUSED(painter)
    if (!f.hasLabels) return;

    QFontMetrics metrics = painter.fontMetrics();
    double textH = metrics.height() + 8;
    double coordW = f.coordText.isEmpty() ? 0 : metrics.horizontalAdvance(f.coordText) + 12;
    double colorW = f.colorText.isEmpty() ? 0 : metrics.horizontalAdvance(f.colorText) + 12;

    if (f.zoom >= 10) {
        double labelW = qMax(coordW, colorW);
        m_coordTarget = QPointF(f.mgX + 3, f.mgY + 3 + f.zoomLabelH + 2);
        m_coordSize = QSizeF(labelW, textH);
        m_colorTarget = QPointF(f.mgX + 3,
            f.mgY + 3 + f.zoomLabelH + 2 + (f.coordText.isEmpty() ? 0 : textH));
        m_colorSize = QSizeF(labelW, textH);
    } else if (f.zoom >= 7) {
        double totalW = coordW + colorW;
        double baseX = f.mgX + (f.dispW - totalW) / 2;
        double baseY = f.mgY + f.dispH + 4;
        m_coordTarget = QPointF(baseX, baseY);
        m_coordSize = QSizeF(coordW, textH);
        m_colorTarget = QPointF(baseX + coordW, baseY);
        m_colorSize = QSizeF(colorW, textH);
    } else {
        double labelW = qMax(coordW, colorW);
        double baseX = f.mgX + (f.dispW - labelW) / 2;
        double baseY = f.mgY + f.dispH + 4;
        m_coordTarget = QPointF(baseX, baseY);
        m_coordSize = QSizeF(labelW, textH);
        m_colorTarget = QPointF(baseX, baseY + (f.coordText.isEmpty() ? 0 : textH));
        m_colorSize = QSizeF(labelW, textH);
    }
}

void SnipOverlay::animateLabels(const MgFrame &f)
{
    // Zoom-change: magnifier snaps immediately, labels animate
    if (m_labelNeedsAnim) {
        m_mgPos = QPointF(f.mgX, f.mgY);
    }

    // If animation is running: on zoom-change restart label anim; on flip keep running
    if (m_labelAnimGroup && m_labelAnimGroup->state() == QParallelAnimationGroup::Running) {
        if (m_labelNeedsAnim && f.hasLabels) {
            m_labelAnimGroup->stop();
            m_labelAnimGroup->deleteLater();
            m_labelAnimGroup = nullptr;
        } else {
            return;
        }
    }

    bool needsLabelsZoom = m_labelNeedsAnim && f.hasLabels;
    bool needsMgFlip = m_flipAnim && !m_labelNeedsAnim;
    bool needsLabelsFlip = m_flipAnim && f.hasLabels;
    m_flipAnim = false;
    m_labelNeedsAnim = false;

    if (!needsMgFlip && !needsLabelsZoom && !needsLabelsFlip) {
        // No animation needed: snap all to targets
        m_mgPos = QPointF(f.mgX, f.mgY);
        m_coordPos = QPointF(m_coordTarget);
        m_colorPos = QPointF(m_colorTarget);
        return;
    }

    // First draw: initialize positions directly
    if (m_mgPos.x() < 0) m_mgPos = QPointF(f.mgX, f.mgY);
    if (m_coordPos.x() < 0) m_coordPos = QPointF(m_coordTarget);
    if (m_colorPos.x() < 0) m_colorPos = QPointF(m_colorTarget);

    if (m_labelAnimGroup) {
        m_labelAnimGroup->stop();
        m_labelAnimGroup->deleteLater();
    }

    auto makeAnim = [this](QPointF start, QPointF end) -> QVariantAnimation* {
        auto* a = new QVariantAnimation(this);
        a->setDuration(120);
        a->setStartValue(start);
        a->setEndValue(end);
        a->setEasingCurve(QEasingCurve::Linear);
        return a;
    };

    auto* group = new QParallelAnimationGroup(this);

    if (needsMgFlip) {
        auto* mgAnim = makeAnim(m_mgPos, QPointF(f.mgX, f.mgY));
        connect(mgAnim, &QVariantAnimation::valueChanged, this, [this](const QVariant &v) {
            m_mgPos = v.toPointF();
            update();
        });
        group->addAnimation(mgAnim);
    } else {
        m_mgPos = QPointF(f.mgX, f.mgY);
    }

    if (needsLabelsZoom || needsLabelsFlip) {
        auto* coordAnim = makeAnim(m_coordPos, m_coordTarget);
        connect(coordAnim, &QVariantAnimation::valueChanged, this, [this](const QVariant &v) {
            m_coordPos = v.toPointF();
            update();
        });
        group->addAnimation(coordAnim);

        auto* colorAnim = makeAnim(m_colorPos, m_colorTarget);
        connect(colorAnim, &QVariantAnimation::valueChanged, this, [this](const QVariant &v) {
            m_colorPos = v.toPointF();
            update();
        });
        group->addAnimation(colorAnim);
    }

    connect(group, &QParallelAnimationGroup::finished, this, &SnipOverlay::onLabelAnimDone);
    group->start();
    m_labelAnimGroup = group;
}

void SnipOverlay::applyMgDisplayPos(MgFrame &f)
{
    if (m_mgPos.x() >= 0) {
        f.mgX = m_mgPos.x();
        f.mgY = m_mgPos.y();
    }
}

void SnipOverlay::onLabelAnimDone()
{
    // 动画自然结束：释放动画组对象（deleteLater 排队删除），
    // 避免每次放大镜翻转/变倍动画泄漏一个 QParallelAnimationGroup。
    // 与 animateLabels 中 stop+deleteLater 的清理方式保持一致（双重 deleteLater 幂等）。
    if (m_labelAnimGroup) {
        m_labelAnimGroup->deleteLater();
        m_labelAnimGroup = nullptr;
    }
}

// ============================================================================
// 放大镜标签 (使用动画后的位置)
// ============================================================================

void SnipOverlay::drawMgLabels(QPainter &painter, const MgFrame &f)
{
    // Zoom label (top-left inside magnifier)
    if (m_zoomLabelEnabled)
        drawZoomLabel(painter, f.mgX, f.mgY, f.zoom);

    // Coord/color labels — use animated positions
    if (!f.hasLabels)
        return;

    QPointF coordPos = (m_coordPos.x() >= 0) ? m_coordPos : m_coordTarget;
    QPointF colorPos = (m_colorPos.x() >= 0) ? m_colorPos : m_colorTarget;

    // Clamp labels to stay within visible area
    double viewW = width(), viewH = height();
    coordPos.setX(qBound(0.0, coordPos.x(), viewW - m_coordSize.width()));
    coordPos.setY(qBound(0.0, coordPos.y(), viewH - m_coordSize.height()));
    colorPos.setX(qBound(0.0, colorPos.x(), viewW - m_colorSize.width()));
    colorPos.setY(qBound(0.0, colorPos.y(), viewH - m_colorSize.height()));

    drawInfoLabelRects(painter, f.coordText, f.colorText,
                       coordPos, colorPos, m_coordSize, m_colorSize);
}

void SnipOverlay::drawZoomLabel(QPainter &painter, double mgX, double mgY, double zoom)
{
    QString text = QStringLiteral("%1×").arg(zoom, 0, 'f', 1);
    QFontMetrics metrics = painter.fontMetrics();
    double textW = metrics.horizontalAdvance(text) + 8;
    double textH = metrics.height() + 6;
    double pad = 3;

    QColor textC(m_zoomLabelTextColorStr);
    textC.setAlphaF(m_zoomLabelTextAlpha / 100.0);

    QColor bgC(m_zoomLabelBgColorStr);
    bgC.setAlphaF(m_zoomLabelBgAlpha / 100.0);
    painter.fillRect(QRectF(mgX + pad, mgY + pad, textW, textH), bgC);
    painter.setPen(textC);
    painter.drawText(QRectF(mgX + pad, mgY + pad, textW, textH), Qt::AlignCenter, text);
}

void SnipOverlay::positionMagnifier(int /*cx*/, int /*cy*/)
{
    // Obsolete — magnifier is drawn inline in drawMagnifierInline
}

void SnipOverlay::updateMagnifierPosition(int /*cx*/, int /*cy*/)
{
    // Obsolete — magnifier is drawn inline in drawMagnifierInline
}

// ============================================================================
// 准星线
// ============================================================================

void SnipOverlay::drawCrosshairLines(QPainter &painter, int cx, int cy)
{
    if (!m_crosshairEnabled)
        return;

    qreal d = dpr();
    double lineW = m_crosshairThickness / d;
    double gap = lineW;

    if (m_crosshairInvert) {
        painter.save();
        painter.setCompositionMode(QPainter::CompositionMode_Difference);
        painter.setPen(QPen(QColor(255, 255, 255), lineW));
        painter.drawLine(QPointF(0, cy + 0.5), QPointF(width(), cy + 0.5));
        painter.drawLine(QPointF(cx + 0.5, 0), QPointF(cx + 0.5, cy + 0.5 - gap));
        painter.drawLine(QPointF(cx + 0.5, cy + 0.5 + gap), QPointF(cx + 0.5, height()));
        painter.restore();
    } else {
        QColor c(m_crosshairColorStr);
        c.setAlphaF(m_crosshairAlpha / 100.0);
        painter.setPen(QPen(c, lineW));
        painter.drawLine(QPointF(0, cy + 0.5), QPointF(width(), cy + 0.5));
        painter.drawLine(QPointF(cx + 0.5, 0), QPointF(cx + 0.5, cy + 0.5 - gap));
        painter.drawLine(QPointF(cx + 0.5, cy + 0.5 + gap), QPointF(cx + 0.5, height()));
    }
}

// ============================================================================
// 层 4：底部提示
// ============================================================================

void SnipOverlay::drawHint(QPainter &painter)
{
    if (m_state != Idle)
        return;

    QString text = QStringLiteral("拖动选择区域 · 单击窗口截图 · 双击/回车复制 · Esc 取消");
    QFontMetrics metrics = painter.fontMetrics();
    double w = metrics.horizontalAdvance(text) + 24;
    double h = metrics.height() + 12;
    double x = (width() - w) / 2.0;

    painter.fillRect(QRectF(x, 24, w, h), QColor(0, 0, 0, 150));
    painter.setPen(QColor(255, 255, 255));
    painter.drawText(QRectF(x, 24, w, h), Qt::AlignCenter, text);
}

// ============================================================================
// 选区尺寸标签
// ============================================================================

void SnipOverlay::drawSizeLabel(QPainter &painter, const QRectF &sel)
{
    QString text = QStringLiteral("%1 × %2").arg(static_cast<int>(sel.width()))
                                            .arg(static_cast<int>(sel.height()));
    QFontMetrics metrics = painter.fontMetrics();
    double w = metrics.horizontalAdvance(text) + 12;
    double h = metrics.height() + 8;
    double x = sel.left();
    double y = sel.top() - h - 4;
    if (y < 0)
        y = sel.top() + 4;

    painter.fillRect(QRectF(x, y, w, h), QColor(0, 0, 0, 170));
    painter.setPen(QColor(255, 255, 255));
    painter.drawText(QRectF(x, y, w, h), Qt::AlignCenter, text);
}

// ============================================================================
// 信息标签（坐标 + 颜色）
// ============================================================================

QString SnipOverlay::coordText() const
{
    if (!m_coordLabelEnabled)
        return {};
    int px, py;
    if (m_cursorProvider) m_cursorProvider->getPos(px, py); else { px = 0; py = 0; }
    return QStringLiteral("坐标：%1, %2").arg(px).arg(py);
}

QString SnipOverlay::colorText() const
{
    if (!m_colorLabelEnabled)
        return {};
    int px, py;
    cursorPixmapPos(px, py);
    QColor c = pixelColor(px, py);
    return QStringLiteral("色值：%1").arg(formatColorText(c.red(), c.green(), c.blue()));
}

QColor SnipOverlay::pixelColor(int px, int py) const
{
    QImage img = m_shot.pixmap.toImage();
    return img.pixelColor(px, py);
}

QString SnipOverlay::formatColorText(int r, int g, int b) const
{
    if (m_colorFormat == QStringLiteral("rgb"))
        return QStringLiteral("%1, %2, %3").arg(r).arg(g).arg(b);
    else
        return QStringLiteral("#%1%2%3")
            .arg(r, 2, 16, QLatin1Char('0'))
            .arg(g, 2, 16, QLatin1Char('0'))
            .arg(b, 2, 16, QLatin1Char('0'));
}

QString SnipOverlay::formatColorCopy(int r, int g, int b) const
{
    if (m_colorFormat == QStringLiteral("rgb")) {
        QString vals = QStringLiteral("%1, %2, %3").arg(r).arg(g).arg(b);
        return m_copyRgbPrefix ? QStringLiteral("rgb(%1)").arg(vals) : vals;
    } else {
        QString h = QStringLiteral("%1%2%3")
            .arg(r, 2, 16, QLatin1Char('0'))
            .arg(g, 2, 16, QLatin1Char('0'))
            .arg(b, 2, 16, QLatin1Char('0'));
        return m_copyHexPrefix ? QStringLiteral("#%1").arg(h) : h;
    }
}

void SnipOverlay::drawInfoLabel(QPainter &painter, int cx, int cy, int gap)
{
    QString coord = coordText();
    QString color = colorText();
    if (coord.isEmpty() && color.isEmpty())
        return;

    QFontMetrics metrics = painter.fontMetrics();
    double textH = metrics.height() + 8;
    double coordW = coord.isEmpty() ? 0 : metrics.horizontalAdvance(coord) + 12;
    double colorW = color.isEmpty() ? 0 : metrics.horizontalAdvance(color) + 12;
    double labelW = qMax(coordW, colorW);
    double totalH = (coord.isEmpty() ? 0 : textH) + (color.isEmpty() ? 0 : textH);

    double x = cx + gap;
    double y = cy + gap;
    if (x + labelW > width())
        x = cx - labelW - gap;
    if (y + totalH > height())
        y = cy - totalH - gap;
    x = qBound(0.0, x, width() - labelW);
    y = qBound(0.0, y, height() - totalH);

    QPointF coordPos(x, y);
    QPointF colorPos(x, y + (coord.isEmpty() ? 0 : textH));
    drawInfoLabelRects(painter, coord, color,
                       coordPos, colorPos,
                       QSizeF(labelW, textH), QSizeF(labelW, textH));
}

void SnipOverlay::drawInfoLabelRects(QPainter &painter,
                                     const QString &coord, const QString &color,
                                     QPointF coordPos, QPointF colorPos,
                                     QSizeF coordSize, QSizeF colorSize)
{
    if (!coord.isEmpty()) {
        QColor bg(m_coordLabelBgColorStr);
        bg.setAlphaF(m_coordLabelBgAlpha / 100.0);
        painter.fillRect(QRectF(coordPos, coordSize), bg);
        QColor textC(m_coordLabelTextColorStr);
        textC.setAlphaF(m_coordLabelTextAlpha / 100.0);
        painter.setPen(textC);
        painter.drawText(QRectF(coordPos, coordSize),
                         Qt::AlignLeft | Qt::AlignVCenter, coord);
    }
    if (!color.isEmpty()) {
        QColor bg(m_colorLabelBgColorStr);
        bg.setAlphaF(m_colorLabelBgAlpha / 100.0);
        painter.fillRect(QRectF(colorPos, colorSize), bg);
        QColor textC(m_colorLabelTextColorStr);
        textC.setAlphaF(m_colorLabelTextAlpha / 100.0);
        painter.setPen(textC);
        painter.drawText(QRectF(colorPos, colorSize),
                         Qt::AlignLeft | Qt::AlignVCenter, color);
    }
}