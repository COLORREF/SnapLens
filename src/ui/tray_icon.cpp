#include "tray_icon.h"

#include <QIcon>
#include <QPainter>
#include <QPixmap>
#include <QApplication>
#include <QStyle>
#include "ElaWidgetTools.hpp"
#include "snapkit.h"

static QIcon createTrayIcon()
{
    return QIcon(SnapUi::makeAppIcon(32));
}

TrayIcon::TrayIcon(QObject* parent)
    : QSystemTrayIcon(parent)
{
    setIcon(createTrayIcon());
    setToolTip("SnapLens 工具箱");

    auto* fluentMenu = new ElaMenu();
    fluentMenu->setMenuItemHeight(34);
    m_menu = fluentMenu;

    m_snipAction = fluentMenu->addElaIconAction(ElaIconType::CameraViewfinder, "截图");
    connect(m_snipAction, &QAction::triggered, this, &TrayIcon::snipRequested);

    m_menu->addSeparator();

    m_translateAction = fluentMenu->addElaIconAction(ElaIconType::House, "打开工具箱");
    connect(m_translateAction, &QAction::triggered, this, &TrayIcon::translateRequested);

    m_modeAction = fluentMenu->addElaIconAction(ElaIconType::ArrowRightArrowLeft, "切换模式");
    connect(m_modeAction, &QAction::triggered, this, &TrayIcon::modeSwitchRequested);

    m_menu->addSeparator();

    QAction* settingsAction = fluentMenu->addElaIconAction(ElaIconType::GearComplex, "设置…");
    connect(settingsAction, &QAction::triggered, this, &TrayIcon::settingsRequested);

    QAction* quitAction = fluentMenu->addElaIconAction(ElaIconType::ArrowRightFromBracket, "退出");
    connect(quitAction, &QAction::triggered, this, &TrayIcon::quitRequested);

    setContextMenu(m_menu);

    connect(this, &QSystemTrayIcon::activated, this,
            [this](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::DoubleClick)
            emit translateRequested();
        else if (reason == QSystemTrayIcon::MiddleClick)
            emit snipRequested();
    });
}

void TrayIcon::setMode(const QString& mode)
{
    if (mode == "screenshot") {
        m_modeAction->setText("启动时打开工具箱");
    } else {
        m_modeAction->setText("启动时后台运行");
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
