#ifndef SNAPLENS_UI_ZOOMABLE_VIEW_H
#define SNAPLENS_UI_ZOOMABLE_VIEW_H

#include <QGraphicsView>
#include <QPixmap>

class QGraphicsScene;
class QGraphicsPixmapItem;

class ZoomableView : public QGraphicsView
{
    Q_OBJECT

public:
    explicit ZoomableView(QWidget* parent = nullptr);
    void setPixmap(const QPixmap& pixmap);
    void fitToWindow();

protected:
    void wheelEvent(QWheelEvent* event) override;

private:
    QGraphicsScene*      m_scene = nullptr;
    QGraphicsPixmapItem* m_pixmapItem = nullptr;
    double               m_zoomLevel = 1.0;
};

#endif