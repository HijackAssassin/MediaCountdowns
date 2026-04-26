#include <QApplication>
#include <QSystemTrayIcon>
#include <QMessageBox>
#include "trayapp.h"

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(false);

    app.setOrganizationName("MediaCountdowns");
    app.setApplicationName("MediaCountdowns");
    app.setApplicationDisplayName("Media Countdowns Notification Manager");

    if (!QSystemTrayIcon::isSystemTrayAvailable()) {
        QMessageBox::critical(nullptr, "Media Countdowns Notification Manager",
            "No system tray available. Notifications will not work.");
        return 1;
    }

    TrayApp tray;
    return app.exec();
}
