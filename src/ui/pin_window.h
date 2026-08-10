#ifndef SNAPLENS_UI_PIN_WINDOW_H
#define SNAPLENS_UI_PIN_WINDOW_H

#include <QWidget>
#include <QPixmap>
#include <QPoint>
#include <QSizeF>

class QToolButton;
class QTimer;

class PinWindow : public QWidget
{
    Q_OBJECT

public:
    explicit PinWindow(const QPixmap& pixmap, const QString& saveDir, QWidget* parent = nullptr);

signals:
    void closed();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void closeEvent(QCloseEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    void updateCloseButtonPos();
    void saveAs();
    void updateCloseButtonVisibility();

    QPixmap    m_pixmap;
    QString    m_saveDir;
    QSizeF     m_baseSize;
    double     m_scale = 1.0;
    QPoint     m_dragOffset;
    QToolButton* m_closeBtn = nullptr;
    QTimer*      m_closeBtnTimer = nullptr;  // 延迟隐藏定时器（防止鼠标移到按钮上时闪烁）
};

#endif