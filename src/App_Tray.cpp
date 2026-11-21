#include "App.h"

#include <QApplication>
#include <QCoreApplication>
#include <QIcon>
#include <QTimer>

#include "TranslationManager.h"

void App::setupTray()
{
    if (m_trayIcon)
        delete m_trayIcon; // Re-creating or creating first time
    if (m_trayMenu)
        delete m_trayMenu;

    m_trayIcon = new QSystemTrayIcon(this);
    QIcon icon(":/assets/icon.ico");
    if (icon.isNull())
    {
        icon = QIcon("assets/icon.ico");
    }
    m_trayIcon->setIcon(icon);

    m_trayMenu = new QMenu();
    TranslationManager &tm = TranslationManager::instance();

    m_trayMenuOpen = false;
    m_pendingTrayAction = -1;

    // "打开主页" is deliberately first: the user expects a tray click to open
    // the dashboard, and Windows may expand this menu under the cursor.
    m_trayMenu->addAction(tm.tr("tray_home"), this, &App::showHome);
    m_trayMenu->addAction(tm.tr("tray_screenshot"), this, &App::onScreenshotRequested);
    m_trayMenu->addAction(tm.tr("tray_summary"), this, &App::showSummary);
    m_trayMenu->addAction(tm.tr("tray_settings"), this, &App::showConfig);
    m_trayMenu->addSeparator();
    m_trayMenu->addAction(tm.tr("tray_quit"), qApp, &QCoreApplication::quit);

    m_trayIcon->setContextMenu(m_trayMenu);
    m_trayIcon->show();

    connect(m_trayMenu, &QMenu::aboutToShow, this, [this]()
            {
                m_trayMenuOpen = true;
                m_pendingTrayAction = -1; });
    connect(m_trayMenu, &QMenu::aboutToHide, this, [this]()
            { m_trayMenuOpen = false; });

    connect(m_trayIcon, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason)
            {
        if (reason == QSystemTrayIcon::Trigger) {
            // On Windows, right-click/context menu can occasionally emit Trigger.
            // Delay the action; cancel if the context menu opens.
            m_pendingTrayAction = (m_configManager.getConfig().trayClickAction == 1) ? 1 : 0;
            QTimer::singleShot(120, this, [this]()
                               {
                                   const int action = m_pendingTrayAction;
                                   if (action < 0)
                                       return;
                                   m_pendingTrayAction = -1;
                                   if (m_trayMenuOpen)
                                       return;
                                   if (action == 1)
                                       onScreenshotRequested();
                                   else
                                       showHome();
                               });
        } else if (reason == QSystemTrayIcon::DoubleClick) {
            m_pendingTrayAction = -1;
            showSummary();
        } else if (reason == QSystemTrayIcon::Context) {
            m_pendingTrayAction = -1;
        } });
}
