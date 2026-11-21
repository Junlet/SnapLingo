#include "App.h"
#include "ThemeUtils.h"
#include "InstanceIpc.h"
#include <QApplication>
#include <QIcon>
#include <QBuffer>
#include <QDebug>
#include <QLocalServer>
#include <QLocalSocket>
#include <QSettings>
#include <QTimer>
#include <QMessageBox>
#include <QUuid>
#include <QDateTime>
#include <QtConcurrent>
#include <QFuture>
#include <QFutureWatcher>
#include <QScreen>

#include "TranslationManager.h"

#include "StartupWindow.h"

#ifdef _WIN32
#include "WinKeyForwarder.h"
#endif

extern QString g_logDirectoryPath;

App::App(const QString &launchCommand, QObject *parent)
    : QObject(parent), m_configManager(),
      m_screenshotHotkey(100, this),
      m_summaryHotkey(101, this),
      m_settingsHotkey(102, this),
      m_quitHotkey(103, this)
{
    m_launchCommand = launchCommand.isEmpty() ? QStringLiteral("home") : launchCommand;

    // First start after install/upgrade opens the dashboard instead of taking a
    // screenshot. The marker lives outside the profiles so switching profiles
    // does not replay this.
    QSettings firstRun(ConfigManager::settingsIniPath(), QSettings::IniFormat);
    if (!firstRun.value(QStringLiteral("startup/firstLaunchDone"), false).toBool())
    {
        m_launchCommand = QStringLiteral("home");
        firstRun.setValue(QStringLiteral("startup/firstLaunchDone"), true);
        firstRun.sync();
    }
#ifdef _WIN32
    // WebView2 often consumes WM_KEYDOWN in its child HWND, bypassing Qt shortcuts.
    // Use a low-level keyboard hook as a reliable path.
    static bool s_hookInstalled = false;
    if (!s_hookInstalled)
    {
        WinKeyForwarder::trace("[WKF] installing forwarder");
        WinKeyForwarder::instance().install();
        s_hookInstalled = true;
    }
#endif

    AppConfig cfg = m_configManager.getConfig();
    QString storageWriteError;
    bool storageFallbackUsed = false;
    const QString configuredStoragePath = ConfigManager::resolveStoragePath(cfg.storagePath);
    const QString effectiveStoragePath =
        ConfigManager::resolveWritableStoragePath(cfg.storagePath, &storageFallbackUsed, &storageWriteError);
    g_enableLogging = cfg.debugMode;
    g_logDirectoryPath = effectiveStoragePath;
    TranslationManager::instance().setLanguage(cfg.language);
    const QString autoStartError = syncLaunchAtStartup(cfg.launchAtStartup);
    if (!autoStartError.isEmpty())
        qWarning() << "[AutoStart]" << autoStartError;

    if (storageFallbackUsed)
    {
        QMessageBox::warning(
            nullptr, "Storage Path Not Writable",
            QString("The configured storage directory is not writable:\n%1\n\nReason: %2\n\n"
                    "This session will use:\n%3\n\nPlease update the storage path in Settings.")
                .arg(QDir::toNativeSeparators(configuredStoragePath),
                     storageWriteError.isEmpty() ? QString("Write access denied.") : storageWriteError,
                     QDir::toNativeSeparators(effectiveStoragePath)));
    }

    m_preferredLockState = cfg.defaultResultWindowLocked;

    m_apiClient = new ApiClient(this);
    connect(m_apiClient, &ApiClient::success, this, &App::onApiSuccess);
    connect(m_apiClient, &ApiClient::error, this, &App::onApiError);

    m_summaryWindow = new SummaryWindow();
    m_summaryWindow->setConfig(cfg);
    m_summaryWindow->setHistoryManager(&m_historyManager);
    connect(m_summaryWindow, &SummaryWindow::restorePreviewRequested, this, [this](const QString &id)
            { restorePreview(id); });

    connect(m_summaryWindow, &SummaryWindow::requestDeleteEntry, this, [this](const QString &id)
            {
        if (m_historyManager.deleteEntry(id)) {
            qDebug() << "Deleted entry:" << id;
        } });

    // Remember the archive sort order across restarts.
    connect(m_summaryWindow, &SummaryWindow::sortOrderChanged, this, [this](bool newestFirst)
            {
        AppConfig cfg = m_configManager.getConfig();
        if (cfg.archiveSortNewestFirst == newestFirst)
            return;
        cfg.archiveSortNewestFirst = newestFirst;
        m_configManager.setConfig(cfg); });

    // Persist edits from SummaryWindow back to history.json
    connect(m_summaryWindow, &SummaryWindow::entryEdited, this, [this](const QString &id, const QString &content)
            {
        qDebug() << "[SummaryWindow] entryEdited -> persist to history.json for id" << id;
        m_historyManager.updateEntryContent(id, content); });

    connect(&m_historyManager, &HistoryManager::entryMarkdownChanged, this, [this](const QString &id, const QString &content)
            {
        pruneActiveWindows();
        if (m_summaryWindow) {
            m_summaryWindow->updateEntryContent(id, content);
        }
        for (auto w : m_activeWindows) {
            if (auto res = qobject_cast<ResultWindow*>(w.data())) {
                if (res->entryId() == id) {
                    res->externalContentUpdate(content);
                }
            }
        } });

    // Reload summary list when history.json changes externally
    connect(&m_historyManager, &HistoryManager::historyFileChanged, this, [this]()
            {
        if (m_summaryWindow) {
            if (m_summaryWindow->isVisible())
                m_summaryWindow->reloadFromStorage(true);
            // Reload tags list for filters/batch tag ops
            m_summaryWindow->setHistoryManager(&m_historyManager);
        } });

    setupTray();
    m_historyManager.setStoragePath(effectiveStoragePath);

    // Initialize summary hotkeys (view/edit/screenshot/bold/underline/highlight)
    m_summaryWindow->configureHotkeys(cfg.editHotkey, cfg.viewToggleHotkey, cfg.screenshotToggleHotkey,
                                      cfg.boldHotkey, cfg.underlineHotkey, cfg.highlightHotkey);

    const QString hotkeyConflictMessage = reloadHotkeys();
    m_lastHotkeyConflictMessage = hotkeyConflictMessage;
    if (!hotkeyConflictMessage.isEmpty())
        notifyHotkeyConflicts(hotkeyConflictMessage, true);

    connect(&m_screenshotHotkey, &GlobalHotkey::activated, this, &App::onScreenshotRequested);
    connect(&m_summaryHotkey, &GlobalHotkey::activated, this, &App::showSummary);
    connect(&m_settingsHotkey, &GlobalHotkey::activated, this, &App::showConfig);
    connect(&m_quitHotkey, &GlobalHotkey::activated, this, &App::quitApp);

    const bool needsConfig = m_configManager.getConfig().apiKey.isEmpty() || storageFallbackUsed;
    if (needsConfig)
    {
        if (m_lastConflictingGlobalHotkeyKeys.isEmpty())
        {
            m_forceConfigDialogForegroundOnce = true;
            QTimer::singleShot(100, this, &App::showConfig);
        }
    }

    // Theme Init
    m_lastTopBarDark = ThemeUtils::isSystemDark();
    connect(&m_themeTimer, &QTimer::timeout, this, &App::checkForThemeChange);
    m_themeTimer.start(2000);

    // Periodically rebind global hotkeys so runtime conflicts or OS-level loss do not stay silent.
    connect(&m_hotkeyHealthTimer, &QTimer::timeout, this, &App::checkHotkeyRegistrationHealth);
    m_hotkeyHealthTimer.start(5000);

    // Analytics (Umami): start 5s after launch to avoid startup stalls
    m_analytics = new AnalyticsManager(this);
    m_analytics->setEnabled(cfg.enableUmamiAnalytics);
    if (cfg.enableUmamiAnalytics)
        m_analytics->startDelayed(5000);
    connect(qApp, &QCoreApplication::aboutToQuit, this, [this]()
            {
        if (m_analytics)
            m_analytics->stop(); });

    // A second launch (for example clicking the app icon in a quick-launch toolbar)
    // is forwarded here; the command decides whether it screenshots, opens the
    // archive, opens settings, or shows the dashboard.
    setupInstanceServer();
    if (!needsConfig)
        handleLaunchCommand(m_launchCommand);
}

void App::setupInstanceServer()
{
    m_instanceServer = new QLocalServer(this);
    m_instanceServer->setSocketOptions(QLocalServer::UserAccessOption);
    QLocalServer::removeServer(AppIpc::serverName());
    if (!m_instanceServer->listen(AppIpc::serverName()))
    {
        qWarning() << "[IPC] failed to listen:" << m_instanceServer->errorString();
        return;
    }
    connect(m_instanceServer, &QLocalServer::newConnection, this, &App::onInstanceConnection);
}

void App::onInstanceConnection()
{
    while (QLocalSocket *socket = m_instanceServer->nextPendingConnection())
    {
        connect(socket, &QLocalSocket::readyRead, this, [this, socket]()
                {
            const QString command = QString::fromUtf8(socket->readAll()).trimmed();
            socket->disconnectFromServer();
            socket->deleteLater();
            if (!command.isEmpty())
                handleLaunchCommand(command); });
        connect(socket, &QLocalSocket::disconnected, socket, &QLocalSocket::deleteLater);
    }
}

void App::handleLaunchCommand(const QString &command)
{
    const QString normalized = command.trimmed().toLower();
    if (normalized == QStringLiteral("tray"))
        return;
    if (normalized == QStringLiteral("archive"))
    {
        showSummary();
        return;
    }
    if (normalized == QStringLiteral("settings"))
    {
        showConfig();
        return;
    }
    if (normalized == QStringLiteral("home"))
    {
        showHome();
        return;
    }
    // Default (and "screenshot"): start a capture. Deferred so the launching
    // process / modal window is gone before the overlay takes focus.
    QTimer::singleShot(0, this, &App::onScreenshotRequested);
}

void App::showHome()
{
    if (m_homeVisible)
        return;
    m_homeVisible = true;

    AppConfig cfg = m_configManager.getConfig();
    StartupWindow home(cfg, &m_historyManager, m_configManager.currentProfileName());
    home.exec();

    m_homeVisible = false;

    const QString recentEntryId = home.requestedEntryId();
    if (!recentEntryId.isEmpty())
    {
        showResult(recentEntryId);
        return;
    }

    switch (home.requestedAction())
    {
    case StartupWindow::Action::Screenshot:
        // Deferred: the modal window has to be gone before the overlay takes focus.
        QTimer::singleShot(0, this, &App::onScreenshotRequested);
        break;
    case StartupWindow::Action::Archive:
        showSummary();
        break;
    case StartupWindow::Action::Settings:
        showConfig();
        break;
    case StartupWindow::Action::None:
        break;
    }
}

App::~App()
{
#ifdef _WIN32
    WinKeyForwarder::trace("[WKF] uninstall forwarder");
    WinKeyForwarder::instance().uninstall();
#endif
    pruneActiveWindows();
    while (!m_activeWindows.isEmpty())
    {
        QPointer<QWidget> window = m_activeWindows.takeLast();
        if (window)
            delete window.data();
    }
    if (m_activeConfigDialog)
    {
        delete m_activeConfigDialog;
        m_activeConfigDialog = nullptr;
    }
    if (m_summaryWindow)
        delete m_summaryWindow;
}

void App::trackActiveWindow(QWidget *window)
{
    if (!window)
        return;

    pruneActiveWindows();
    if (!m_activeWindows.contains(window))
        m_activeWindows.append(window);

    connect(window, &QObject::destroyed, this, [this, window]()
            {
                m_activeWindows.removeAll(window);
                pruneActiveWindows();
            });
}

void App::pruneActiveWindows()
{
    for (int i = m_activeWindows.size() - 1; i >= 0; --i)
    {
        if (m_activeWindows.at(i).isNull())
            m_activeWindows.removeAt(i);
    }
}
