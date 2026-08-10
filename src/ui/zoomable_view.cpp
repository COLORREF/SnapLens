#include "zoomable_view.h"

#include <QGraphicsScene>
#include <QGraphicsPixmapItem>
#include <QWheelEvent>
#include <QScrollBar>

static constexpr double ZOOM_MIN = 0.1;
static constexpr double ZOOM_MAX = 10.0;
static constexpr double ZOOM_STEP = 1.08;  // Match Python's finer granularity

ZoomableView::ZoomableView(QWidget* parent)
    : QGraphicsView(parent)
{
    m_scene = new QGraphicsScene(this);
    setScene(m_scene);

    setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setDragMode(QGraphicsView::ScrollHandDrag);
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    setResizeAnchor(QGraphicsView::AnchorUnderMouse);
}

void ZoomableView::setPixmap(const QPixmap& pixmap)
{
    m_scene->clear();
    m_pixmapItem = m_scene->addPixmap(pixmap);
    m_scene->setSceneRect(QRectF(pixmap.rect()));
    m_zoomLevel = 1.0;
    resetTransform();
    fitInView(m_scene->sceneRect(), Qt::KeepAspectRatio);
}

void ZoomableView::wheelEvent(QWheelEvent* event)
{
    if (!m_pixmapItem) {
        QGraphicsView::wheelEvent(event);
        return;
    }

    double delta = event->angleDelta().y();
    double factor;
    if (delta > 0 && m_zoomLevel < ZOOM_MAX) {
        factor = ZOOM_STEP;
        m_zoomLevel *= factor;
    } else if (delta < 0 && m_zoomLevel > ZOOM_MIN) {
        factor = 1.0 / ZOOM_STEP;
        m_zoomLevel /= ZOOM_STEP;
    } else {
        return;
    }
    scale(factor, factor);
}

void ZoomableView::fitToWindow()
{
    if (!m_pixmapItem) return;
    m_zoomLevel = 1.0;
    resetTransform();
    fitInView(m_scene->sceneRect(), Qt::KeepAspectRatio);
}