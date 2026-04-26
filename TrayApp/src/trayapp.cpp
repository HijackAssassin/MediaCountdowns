#include "trayapp.h"
#include "jsonmanager.h"
#include <QLocalSocket>
#include <QProcess>
#include <QCoreApplication>
#include <QFileInfo>
#include <QStyle>
#include <QApplication>
#include <QDateTime>
#include <QDebug>
#include <QFile>
#include <QPixmap>
#include <QStandardPaths>
#include <QDir>

#ifdef Q_OS_WIN
#  include <qt_windows.h>
#  include <shobjidl.h>      // IShellLinkW
#  include <shlobj.h>        // SHGetFolderPathW / CSIDL_STARTUP
#  include <objbase.h>       // CoInitialize / CoCreateInstance
#endif

// =============================================================================
TrayApp::TrayApp(QObject* parent) : QObject(parent)
{
#ifdef Q_OS_WIN
    // Required for Windows 10 Action Center notifications to appear correctly
    SetCurrentProcessExplicitAppUserModelID(L"MediaCountdowns.Notifier");
#endif

    // ── Tray icon ─────────────────────────────────────────────────────────────
    QIcon icon = QApplication::style()->standardIcon(QStyle::SP_MediaPlay);

    m_menu = new QMenu;
    auto* heading    = m_menu->addAction("Media Countdowns");
    heading->setEnabled(false);
    m_menu->addSeparator();
    auto* openAct    = m_menu->addAction("Open Manager");
    auto* refreshAct = m_menu->addAction("Refresh");
    m_menu->addSeparator();
    auto* quitAct    = m_menu->addAction("Quit");

    m_tray = new QSystemTrayIcon(icon, this);
    m_tray->setContextMenu(m_menu);
    m_tray->setToolTip("Media Countdowns Notifier");
    m_tray->show();

    connect(openAct,    &QAction::triggered, this, &TrayApp::onOpenMainApp);
    connect(refreshAct, &QAction::triggered, this, &TrayApp::reloadTiles);
    connect(quitAct,    &QAction::triggered, this, &TrayApp::onQuit);
    connect(m_tray, &QSystemTrayIcon::activated, this, &TrayApp::onTrayActivated);

    // ── IPC server ────────────────────────────────────────────────────────────
    m_ipcServer = new QLocalServer(this);
    QLocalServer::removeServer("MediaCountdownsTray");
    if (m_ipcServer->listen("MediaCountdownsTray"))
        connect(m_ipcServer, &QLocalServer::newConnection,
                this, &TrayApp::onNewConnection);

    // ── 1-second tick timer ───────────────────────────────────────────────────
    // onTick() gates the real work to second == 1 of each minute (HH:MM:01).
    // This is resilient to sleep/hibernate — we just check `now.second()`
    // every wakeup rather than relying on a 60-second interval not drifting.
    m_tickTimer = new QTimer(this);
    m_tickTimer->setInterval(1000);
    m_tickTimer->setTimerType(Qt::PreciseTimer);
    connect(m_tickTimer, &QTimer::timeout, this, &TrayApp::onTick);
    m_tickTimer->start();

    // Add ourselves to Windows Startup folder so we auto-launch on login.
    createStartupShortcut();

    // Startup check: catch anything that expired while the notifier wasn't running.
    // We bypass the isMainAppRunning() guard here on purpose — the main app may
    // not be open at boot time, so we must check ourselves.
    reloadTiles();
    onHeartbeat();
}

TrayApp::~TrayApp() = default;

// =============================================================================
//  isMainAppRunning
//
//  Probes the QLocalServer "MediaCountdownsMainApp" that main.cpp starts on
//  launch. Connecting means the app is alive and handling its own notifications.
// =============================================================================
bool TrayApp::isMainAppRunning()
{
    QLocalSocket probe;
    probe.connectToServer("MediaCountdownsMainApp");
    return probe.waitForConnected(100);
}

// =============================================================================
//  onTick — fires every second.
//  Only calls onHeartbeat() at exactly second == 1 of each minute, and only
//  once per minute even if the timer fires slightly early/late.
// =============================================================================
void TrayApp::onTick()
{
    QTime now = QTime::currentTime();
    if (now.second() != 1) return;

    // Unique minute ID — prevents double-fire if timer drifts
    int minuteId = now.hour() * 60 + now.minute();
    if (minuteId == m_lastCheckedMinute) return;
    m_lastCheckedMinute = minuteId;

    qDebug() << "[TrayApp] Tick at" << now.toString("HH:mm:ss");
    onHeartbeat();
}

// =============================================================================
//  onHeartbeat — the real work.
//
//  Conditions to fire a notification for a tile:
//    1. Main app is NOT running (it handles notifications itself when open)
//    2. Tile notifStatus == Active
//    3. Tile date+time is in the past
//
//  Tiles always reloaded fresh from disk so we pick up any edits the user
//  made via the main app before closing it.
// =============================================================================
void TrayApp::onHeartbeat()
{
    if (isMainAppRunning()) {
        qDebug() << "[TrayApp] Main app running — skipping check";
        return;
    }

    // Reload fresh from disk every check — cheap and guarantees we have
    // the latest tile data (dates, times, notifStatus) after main app edits.
    reloadTiles();

    QDateTime now = QDateTime::currentDateTime();
    bool anyChanged = false;

    for (TileData& td : m_tiles) {
        if (td.notifStatus != NotifStatus::Active) continue;
        if (!td.hasDate()) continue;

        QTime t = td.effectiveTime().isValid() ? td.effectiveTime() : QTime(0, 0, 0);
        QDateTime target(td.effectiveDate(), t);
        if (now < target) continue;

        qDebug() << "[TrayApp] Expired:" << td.displayTitle()
                 << "| target:" << target.toString("yyyy-MM-dd HH:mm");

        sendNotification(td);

        // Skip straight to Inactive (Active → notified → Inactive)
        td.notifStatus = NotifStatus::Inactive;
        td.notified    = true;
        anyChanged = true;
    }

    if (anyChanged) {
        qDebug() << "[TrayApp] Saving updated statuses to JSON";
        JsonManager::instance().saveTiles(m_tiles);
    }
}

// =============================================================================
//  reloadTiles
// =============================================================================
void TrayApp::reloadTiles()
{
    m_tiles = JsonManager::instance().loadTiles();
    qDebug() << "[TrayApp] Loaded" << m_tiles.size() << "tiles";
}

// =============================================================================
//  sendNotification
// =============================================================================
void TrayApp::sendNotification(const TileData& td)
{
    QString title = td.displayTitle();
    int bullet = title.indexOf(QChar(0x2022));  // strip "  •  N Seasons" suffix
    if (bullet >= 0) title = title.left(bullet).trimmed();

    QString body;
    if (td.statusLabel.isEmpty()
        || td.statusLabel == "Releases"
        || td.statusLabel == "Released"
        || td.statusLabel == "No Release Date Yet")
    {
        body = "Now available!";
    } else {
        body = QString("%1 is out!").arg(td.statusLabel);
    }

    qDebug() << "[TrayApp] Notification:" << title << "|" << body;

    QIcon notifIcon;
    if (!td.imagePath.isEmpty() && QFile::exists(td.imagePath)) {
        QPixmap px(td.imagePath);
        if (!px.isNull())
            notifIcon = QIcon(px.scaled(256, 144,
                Qt::KeepAspectRatio, Qt::SmoothTransformation));
    }
    if (notifIcon.isNull())
        notifIcon = QApplication::style()->standardIcon(QStyle::SP_MediaPlay);

    m_tray->setIcon(notifIcon);
    m_tray->showMessage(title, body, notifIcon, 10000);

    QTimer::singleShot(12000, this, [this]() {
        m_tray->setIcon(QApplication::style()->standardIcon(QStyle::SP_MediaPlay));
    });
}

// =============================================================================
//  createStartupShortcut
//
//  Creates a .lnk shortcut to this exe in the Windows Startup folder so the
//  notifier launches automatically on login.
//  Does nothing if the shortcut already exists or on non-Windows platforms.
// =============================================================================
void TrayApp::createStartupShortcut()
{
#ifdef Q_OS_WIN
    // Resolve %APPDATA%\Microsoft\Windows\Start Menu\Programs\Startup
    WCHAR startupW[MAX_PATH] = {};
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_STARTUP, nullptr, SHGFP_TYPE_CURRENT, startupW)))
        return;

    QString shortcutPath = QString::fromWCharArray(startupW)
                           + "\\MediaCountdownsNotifier.lnk";

    if (QFile::exists(shortcutPath)) return;   // already installed

    QString exePath = QCoreApplication::applicationFilePath();
    exePath.replace('/', '\\');

    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    IShellLinkW* psl = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_ShellLink, nullptr,
                                   CLSCTX_INPROC_SERVER,
                                   IID_IShellLinkW, reinterpret_cast<void**>(&psl))))
    {
        psl->SetPath(exePath.toStdWString().c_str());
        psl->SetDescription(L"Media Countdowns Notifier — runs in the system tray");
        psl->SetWorkingDirectory(
            QCoreApplication::applicationDirPath().replace('/', '\\')
            .toStdWString().c_str());

        IPersistFile* ppf = nullptr;
        if (SUCCEEDED(psl->QueryInterface(IID_IPersistFile,
                                          reinterpret_cast<void**>(&ppf))))
        {
            ppf->Save(shortcutPath.toStdWString().c_str(), TRUE);
            ppf->Release();
            qDebug() << "[TrayApp] Startup shortcut created:" << shortcutPath;
        }
        psl->Release();
    }

    CoUninitialize();
#endif
}

// =============================================================================
void TrayApp::onNewConnection()
{
    QLocalSocket* sock = m_ipcServer->nextPendingConnection();
    if (!sock) return;
    connect(sock, &QLocalSocket::readyRead, this, [sock, this]() {
        while (sock->canReadLine()) {
            QString cmd = QString::fromUtf8(sock->readLine()).trimmed();
            qDebug() << "[TrayApp] IPC:" << cmd;
            if (cmd == "REFRESH") {
                // Just reload cache; main app is alive so heartbeat won't fire
                reloadTiles();
            } else if (cmd.startsWith("TEST:")) {
                QString tileId = cmd.mid(5);
                reloadTiles();
                for (const TileData& td : std::as_const(m_tiles)) {
                    if (td.id == tileId) { sendNotification(td); break; }
                }
            }
        }
        sock->deleteLater();
    });
    connect(sock, &QLocalSocket::disconnected, sock, &QLocalSocket::deleteLater);
}

void TrayApp::onTrayActivated(QSystemTrayIcon::ActivationReason reason)
{
    if (reason == QSystemTrayIcon::DoubleClick) onOpenMainApp();
}

void TrayApp::onOpenMainApp()
{
    QString path = QCoreApplication::applicationDirPath() + "/MediaCountdowns.exe";
    if (QFileInfo::exists(path))
        QProcess::startDetached(path, {});
    else
        m_tray->showMessage("Media Countdowns",
            "Could not find MediaCountdowns.exe next to this app.",
            QSystemTrayIcon::Warning, 4000);
}

void TrayApp::onQuit()
{
    m_tray->hide();
    QCoreApplication::quit();
}
