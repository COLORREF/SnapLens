#include "tray_icon.h"

#include <QIcon>
#include <QPainter>
#include <QPixmap>
#include <QApplication>
#include <QStyle>

static QIcon createTrayIcon()
{
    // Programmatically draw a tray icon: blue rounded rect + "截" character
    QPixmap pix(32, 32);
    pix.fill(Qt::transparent);
    QPainter painter(&pix);
    painter.setRenderHint(QPainter::Antialiasing);
    // Blue rounded background
    painter.setBrush(QColor(24, 144, 255));
    painter.setPen(Qt::NoPen);
    painter.drawRoundedRect(2, 2, 28, 28, 6, 6);
    // White "截" character
    painter.setPen(Qt::white);
    QFont font("Microsoft YaHei", 16, QFont::Bold);
    painter.setFont(font);
    painter.drawText(QRect(2, 2, 28, 28), Qt::AlignCenter, "截");
    painter.end();
    return QIcon(pix);
}

TrayIcon::TrayIcon(QObject* parent)
    : QSystemTrayIcon(parent)
{
    setIcon(createTrayIcon());
    setToolTip("SnapLens 截图工具");

    m_menu = new QMenu();

    m_snipAction = m_menu->addAction("截图");
    connect(m_snipAction, &QAction::triggered, this, &TrayIcon::snipRequested);

    m_menu->addSeparator();

    m_translateAction = m_menu->addAction("文本翻译");
    connect(m_translateAction, &QAction::triggered, this, &TrayIcon::translateRequested);

    m_modeAction = m_menu->addAction("切换模式");
    connect(m_modeAction, &QAction::triggered, this, &TrayIcon::modeSwitchRequested);

    m_menu->addSeparator();

    QAction* settingsAction = m_menu->addAction("设置…");
    connect(settingsAction, &QAction::triggered, this, &TrayIcon::settingsRequested);

    QAction* quitAction = m_menu->addAction("退出");
    connect(quitAction, &QAction::triggered, this, &TrayIcon::quitRequested);

    setContextMenu(m_menu);
}

void TrayIcon::setMode(const QString& mode)
{
    if (mode == "screenshot") {
        m_modeAction->setText("切换至翻译模式");
    } else {
        m_modeAction->setText("切换至截图模式");
    }
}

void TrayIcon::setHotkeyText(const QString& hotkey)
{
    if (!hotkey.isEmpty()) {
        m_snipAction->setText(QString("截图 (%1)").arg(hotkey));
    } else {
        m_snipAction->setText("截图");
    }
}