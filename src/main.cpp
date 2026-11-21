#include "App.h"
#include "InstanceIpc.h"
#include <QApplication>
#include <QIcon>
#include <QLocalSocket>
#include <QLockFile>
#include <QDir>
#include <QMessageBox>
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>
#include <QFileInfo>
#include <QProcessEnvironment>

#include <QFile>
#include <QTextStream>
#include <QFileDevice>
#include <QDateTime>
#include "ConfigManager.h"

#ifndef APP_NAME
#define APP_NAME "SnapLingo"
#endif

#ifndef APP_VERSION
#define APP_VERSION "0.0.0"
#endif

#ifdef _WIN32
#include <windows.h>
#endif

bool g_enableLogging = false;
QString g_logDirectoryPath;

static QString logFilePath(const QString &fileName)
{
    if (g_logDirectoryPath.isEmpty())
        return fileName;

    return QDir(g_logDirectoryPath).filePath(fileName);
}

#ifdef _WIN32
static const char *kWebViewRuntimeKey =
    "HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\EdgeUpdate\\Clients\\{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}";
static const char *kWebViewRuntimeKeyWow =
    "HKEY_LOCAL_MACHINE\\SOFTWARE\\WOW6432Node\\Microsoft\\EdgeUpdate\\Clients\\{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}";
static const char *kWebViewRuntimeKeyUser =
    "HKEY_CURRENT_USER\\SOFTWARE\\Microsoft\\EdgeUpdate\\Clients\\{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}";

bool isWebView2RuntimeInstalled()
{
    const QStringList keys{QString::fromLatin1(kWebViewRuntimeKey), QString::fromLatin1(kWebViewRuntimeKeyWow),
                           QString::fromLatin1(kWebViewRuntimeKeyUser)};
    for (const QString &k : keys)
    {
        QSettings reg(k, QSettings::NativeFormat);
        if (reg.contains("pv"))
            return true;
    }
    return false;
}

QString ensureWebViewInstallerScript()
{
    const QString appDir = QCoreApplication::applicationDirPath();
    const QString diskPath = QDir(appDir).filePath("check-webview2.ps1");
    if (QFile::exists(diskPath))
        return diskPath;

    QFile res(":/check-webview2.ps1");
    if (!res.exists())
        return QString();
    if (!res.open(QIODevice::ReadOnly))
        return QString();

    const QString tempDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    QDir().mkpath(tempDir);
    const QString tempPath = QDir(tempDir).filePath("check-webview2.ps1");

    QFile out(tempPath);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return QString();

    out.write(res.readAll());
    out.close();

    QFile::setPermissions(tempPath, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ReadGroup |
                                          QFileDevice::ReadOther);
    return tempPath;
}

bool runWebViewInstaller(QWidget *parent)
{
    const QString scriptPath = ensureWebViewInstallerScript();
    if (scriptPath.isEmpty())
    {
        QMessageBox::critical(parent, "Missing installer script",
                              "Cannot extract WebView2 installer script. Please reinstall or download WebView2 manually.");
        return false;
    }

    QProcess proc;
    proc.setProgram("powershell");
    QStringList args{
        "-NoProfile", "-ExecutionPolicy", "Bypass", "-WindowStyle", "Hidden", "-File", scriptPath};
    proc.setArguments(args);

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert("NO_PAUSE", "1"); // avoid waiting for keypress in bundled automation
    proc.setProcessEnvironment(env);
    proc.setWorkingDirectory(QFileInfo(scriptPath).absolutePath());

    proc.start();
    if (!proc.waitForStarted())
    {
        QMessageBox::critical(parent, "WebView2 install failed", "Failed to start installer PowerShell process.");
        return false;
    }
    proc.waitForFinished(-1);

    const int code = proc.exitCode();
    if (code == 0 || code == 3010)
    {
        if (code == 3010)
        {
            QMessageBox::information(parent, "WebView2 installed",
                                     "WebView2 Runtime installed successfully. A restart is recommended to finalize setup.");
        }
        return true;
    }

    const QString err = QString::fromLocal8Bit(proc.readAllStandardError());
    QMessageBox::critical(parent, "WebView2 install failed",
                          QString("Installer exited with code %1.\n\n%2").arg(code).arg(err));
    return false;
}

bool ensureWebViewRuntime(QWidget *parent)
{
    if (isWebView2RuntimeInstalled())
        return true;

    const auto choice = QMessageBox::question(
        parent, "WebView2 Runtime required",
        "Microsoft Edge WebView2 Runtime is not detected.\nIt is required to run this application.\n\nInstall it now?");
    if (choice != QMessageBox::Yes)
        return false;

    if (!runWebViewInstaller(parent))
        return false;

    return isWebView2RuntimeInstalled();
}
#endif

void customMessageHandler(QtMsgType type, const QMessageLogContext &context, const QString &msg)
{
    QString text;
    switch (type)
    {
    case QtDebugMsg:
        text = "DEBUG: " + msg;
        break;
    case QtInfoMsg:
        text = "INFO: " + msg;
        break;
    case QtWarningMsg:
        text = "WARN: " + msg;
        break;
    case QtCriticalMsg:
        text = "CRIT: " + msg;
        break;
    case QtFatalMsg:
        text = "FATAL: " + msg;
        break;
    }

#ifdef _WIN32
    OutputDebugStringW(reinterpret_cast<const wchar_t *>(text.utf16()));
    OutputDebugStringW(L"\n");
#endif

    // Only write debug.log when debug mode enabled (or env override)
    bool allowFile = g_enableLogging || qEnvironmentVariableIsSet("FORCE_DEBUG_LOG");
    if (!allowFile)
        return;
    // Gate DEBUG entries if disabled
    bool shouldLog = (type != QtDebugMsg) || allowFile;
    if (!shouldLog)
        return;

    static bool logCleared = false;
    QFile outFile(logFilePath("debug.log"));
    const QIODevice::OpenMode mode = QIODevice::WriteOnly | (logCleared ? QIODevice::Append : QIODevice::Truncate);
    if (outFile.open(mode))
    {
        if (!logCleared)
            logCleared = true;
        QTextStream ts(&outFile);
        ts << QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss.zzz ") << text << Qt::endl;
    }
}

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);

    // Determine debug mode from config before initializing logging
    {
        ConfigManager tempConfig;
        g_enableLogging = tempConfig.getConfig().debugMode;
        g_logDirectoryPath = ConfigManager::resolveWritableStoragePath(tempConfig.getConfig().storagePath);
    }
    bool forceDebug = qEnvironmentVariableIsSet("FORCE_DEBUG_LOG");
    bool enableFileLog = g_enableLogging || forceDebug;
    if (enableFileLog)
    {
        // Clear previous logs on startup when file logging is enabled.
        QFile::remove(logFilePath("debug.log"));
        QFile::remove(logFilePath("wkf.log"));
    }

    qInstallMessageHandler(customMessageHandler);
    qDebug() << "Application starting...";

    QCoreApplication::setApplicationName(QString::fromUtf8(APP_NAME));
    QCoreApplication::setApplicationVersion(QString::fromUtf8(APP_VERSION));

    // Launch intent. The app icon is a screenshot tool entry point: a plain launch
    // starts a capture; --archive/--settings open the other surfaces explicitly.
    QString launchCommand = QStringLiteral("screenshot");
    const QStringList launchArgs = QCoreApplication::arguments().mid(1);
    for (const QString &arg : launchArgs)
    {
        const QString a = arg.trimmed().toLower();
        if (a == QStringLiteral("--archive") || a == QStringLiteral("-a") || a == QStringLiteral("archive"))
            launchCommand = QStringLiteral("archive");
        else if (a == QStringLiteral("--settings") || a == QStringLiteral("settings"))
            launchCommand = QStringLiteral("settings");
        else if (a == QStringLiteral("--tray") || a == QStringLiteral("tray"))
            launchCommand = QStringLiteral("tray");
        // Anything else (including a stale "--home" shortcut left on a desktop)
        // falls through to the default screenshot action. The dashboard is opened
        // from the tray menu, so an app icon can never silently turn into "home".
    }

    // If SnapLingo is already running, forward the intent and exit. This is what
    // makes clicking the app icon in a quick-launch toolbar take a screenshot
    // instead of showing an "already running" warning.
    const auto forwardToRunningInstance = [&launchCommand]() -> bool
    {
        QLocalSocket socket;
        socket.connectToServer(AppIpc::serverName(), QIODevice::WriteOnly);
        if (!socket.waitForConnected(400))
            return false;
        socket.write(launchCommand.toUtf8());
        socket.flush();
        socket.waitForBytesWritten(400);
        return true;
    };
    if (forwardToRunningInstance())
        return 0;

    // Ensure we don't quit when the last window closes (because we live in the tray)
    a.setQuitOnLastWindowClosed(false);
    a.setWindowIcon(QIcon(":/assets/icon.ico"));

    // Single Instance Check
    // Keep the historical lock name: an old AI-Screenshot-Translator build and a
    // new SnapLingo build must not run at the same time during an upgrade.
    QLockFile lockFile(QDir::temp().filePath("AI-Screenshot-Translator.lock"));
    if (!lockFile.tryLock(100))
    {
        // The primary may still be bringing its local server up.
        if (forwardToRunningInstance())
            return 0;
        qDebug() << "App already running, exiting.";
        const QString appName = QString::fromUtf8(APP_NAME);
        QMessageBox::warning(nullptr, appName,
                             QString("%1 is already running.\nCheck the system tray.").arg(appName));
        return 1;
    }

#ifdef _WIN32
    if (!ensureWebViewRuntime(nullptr))
    {
        qWarning() << "WebView2 runtime missing or installation declined.";
        return 1;
    }
#endif

    try
    {
        qDebug() << "Initializing App...";
        App app(launchCommand);
        qDebug() << "App initialized. Entering exec loop.";
        return a.exec();
    }
    catch (const std::exception &e)
    {
        qCritical() << "Uncaught exception: " << e.what();
        QMessageBox::critical(nullptr, "Crash", QString("Uncaught exception failed: %1").arg(e.what()));
        return -1;
    }
    catch (...)
    {
        qCritical() << "Unknown crash occurred.";
        QMessageBox::critical(nullptr, "Crash", "Unknown error occurred.");
        return -1;
    }
}
