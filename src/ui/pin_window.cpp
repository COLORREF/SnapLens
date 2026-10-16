#include "pin_window.h"
#include "../log/log_manager.h"

#include <QPainter>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QResizeEvent>
#include <QCloseEvent>
#include <QContextMenuEvent>
#include <QKeyEvent>
#include <QMenu>
#include <QAction>
#include <QFileDialog>
#include <QClipboard>
#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QToolButton>
#include <QGuiApplication>
#include <QScreen>
#include <QTimer>
#include <QCursor>
#include <QEnterEvent>
#include "ElaWidgetTools.hpp"

PinWindow::PinWindow(const QPixmap& pixmap, const QString& saveDir, QWidget* parent)
    : QWidget(parent, Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Window)
    , m_pixmap(pixmap)
    , m_saveDir(saveDir)
{
    setAttribute(Qt::WA_DeleteOnClose);

    qreal dpr = pixmap.devicePixelRatio();
    m_baseSize = QSizeF(pixmap.width() / dpr, pixmap.height() / dpr);
    resize(m_baseSize.toSize());

    m_closeBtn = new QToolButton(this);
    m_closeBtn->setText(QString::fromUtf8("\u00d7"));
    m_closeBtn->setFixedSize(24, 24);
    m_closeBtn->setCursor(Qt::ArrowCursor);
    // 默认：半透明深色底 + 白色 ×，不遮挡图片内容；
    // hover：Windows 语义的关闭红 + 白色 ×，明确可点击反馈。
    m_closeBtn->setStyleSheet(
        "QToolButton {"
        "  background: rgba(32, 32, 32, 150);"
        "  color: #F2F2F2;"
        "  border: 1px solid rgba(255, 255, 255, 40);"
        "  border-radius: 6px;"
        "  font-size: 15px;"
        "  font-weight: bold;"
        "  padding-bottom: 2px;"
        "}"
        "QToolButton:hover {"
        "  background: rgba(196, 43, 28, 235);"
        "  border-color: rgba(255, 255, 255, 70);"
        "  color: white;"
        "}"
        "QToolButton:pressed {"
        "  background: rgba(160, 32, 20, 245);"
        "}"
    );
    connect(m_closeBtn, &QToolButton::clicked, this, &QWidget::close);

    // 关闭按钮默认隐藏，鼠标移入钉图区域时显示，避免常驻遮挡下层内容。
    m_closeBtn->hide();
    m_closeBtnTimer = new QTimer(this);
    m_closeBtnTimer->setSingleShot(true);
    m_closeBtnTimer->setInterval(200);
    connect(m_closeBtnTimer, &QTimer::timeout,
            this, &PinWindow::updateCloseButtonVisibility);

    updateCloseButtonPos();

    QScreen* screen = QGuiApplication::primaryScreen();
    if (screen) {
        QRect geo = screen->availableGeometry();
        move(geo.center() - rect().center());
    }
}

void PinWindow::updateCloseButtonPos()
{
    m_closeBtn->move(width() - m_closeBtn->width() - 2, 2);
}

void PinWindow::enterEvent(QEnterEvent* event)
{
    // 鼠标移入钉图区域：立即显示关闭按钮（取消待执行的隐藏）
    if (m_closeBtnTimer->isActive())
        m_closeBtnTimer->stop();
    m_closeBtn->show();
    QWidget::enterEvent(event);
}

void PinWindow::leaveEvent(QEvent* event)
{
    // 鼠标移出：延迟隐藏（200ms），期间若鼠标进入关闭按钮则不隐藏，
    // 避免"窗口 leave → 按钮 hide → 鼠标落回窗口 → 再 show"的闪烁。
    m_closeBtnTimer->start();
    QWidget::leaveEvent(event);
}

void PinWindow::updateCloseButtonVisibility()
{
    // 鼠标仍在窗口或按钮上 → 保持显示
    const bool inWindow = rect().contains(mapFromGlobal(QCursor::pos()));
    const bool inButton =
        m_closeBtn->rect().contains(m_closeBtn->mapFromGlobal(QCursor::pos()));
    if (!inWindow && !inButton) {
        m_closeBtn->hide();
    }
}

void PinWindow::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    QRectF target(0, 0, m_baseSize.width() * m_scale, m_baseSize.height() * m_scale);
    p.drawPixmap(target, m_pixmap, QRectF(m_pixmap.rect()));
}

void PinWindow::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        m_dragOffset = event->globalPosition().toPoint() - frameGeometry().topLeft();
    }
    QWidget::mousePressEvent(event);
}

void PinWindow::mouseMoveEvent(QMouseEvent* event)
{
    if (event->buttons() & Qt::LeftButton) {
        move(event->globalPosition().toPoint() - m_dragOffset);
    }
    QWidget::mouseMoveEvent(event);
}

void PinWindow::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        m_dragOffset = QPoint();
    }
    QWidget::mouseReleaseEvent(event);
}

void PinWindow::wheelEvent(QWheelEvent* event)
{
    double delta = event->angleDelta().y() > 0 ? 1.1 : 0.9;
    double newScale = m_scale * delta;
    if (newScale < 0.1 || newScale > 10.0) return;
    m_scale = newScale;
    resize(QSize(m_baseSize.width() * m_scale, m_baseSize.height() * m_scale));
    updateCloseButtonPos();
    update();
}

void PinWindow::resizeEvent(QResizeEvent* event)
{
    updateCloseButtonPos();
    QWidget::resizeEvent(event);
}

void PinWindow::closeEvent(QCloseEvent* event)
{
    emit closed();
    QWidget::closeEvent(event);
}

void PinWindow::contextMenuEvent(QContextMenuEvent* event)
{
    ElaMenu menu(this);
    menu.setMenuItemHeight(34);
    QAction* copyAction = menu.addElaIconAction(ElaIconType::Copy, "复制图片");
    QAction* saveAction = menu.addElaIconAction(ElaIconType::FloppyDisk, "另存为...");
    menu.addSeparator();
    QAction* closeAction = menu.addElaIconAction(ElaIconType::Xmark, "关闭");

    QAction* chosen = menu.exec(event->globalPos());
    if (chosen == copyAction) {
        QApplication::clipboard()->setPixmap(m_pixmap);
    } else if (chosen == saveAction) {
        saveAs();
    } else if (chosen == closeAction) {
        close();
    }
}

void PinWindow::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Escape) {
        close();
        return;
    }
    QWidget::keyPressEvent(event);
}

void PinWindow::saveAs()
{
    QString defaultPath = QDir(m_saveDir).filePath(
        "SnapLens_" + QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss") + ".png");
    QString path = QFileDialog::getSaveFileName(
        this, "另存为", defaultPath, "PNG 图片 (*.png);;JPEG 图片 (*.jpg)");
    if (!path.isEmpty()) {
        m_pixmap.save(path);
    }
}
