#include "mainwindow.h"

#include <QApplication>
#include <QEvent>
#include <QIcon>
#include <QLocalServer>
#include <QLocalSocket>
#include <QObject>
#include <QThread>
#include <QWidget>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <shobjidl.h>
#include <windows.h>
#endif

#ifdef Q_OS_WIN
namespace {
class PlainNativeTitleFilter : public QObject
{
public:
    using QObject::QObject;

protected:
    bool eventFilter(QObject *object, QEvent *event) override
    {
        if (event->type() != QEvent::Show && event->type() != QEvent::WindowTitleChange) {
            return QObject::eventFilter(object, event);
        }

        auto *widget = qobject_cast<QWidget *>(object);
        if (!widget || !widget->isWindow()) {
            return QObject::eventFilter(object, event);
        }

        const QString title = widget->windowTitle();
        if (!title.isEmpty()) {
            SetWindowTextW(reinterpret_cast<HWND>(widget->winId()), reinterpret_cast<LPCWSTR>(title.utf16()));
        }
        return QObject::eventFilter(object, event);
    }
};
}
#endif

int main(int argc, char *argv[])
{
#ifdef Q_OS_WIN
    SetCurrentProcessExplicitAppUserModelID(L"BlinqMessenger");
#endif

    QApplication a(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("Exe Innovate"));
    QApplication::setOrganizationDomain(QStringLiteral("exeinnovate.com"));
    QApplication::setApplicationName(QStringLiteral("Blinq Messenger"));
    QApplication::setApplicationDisplayName(QStringLiteral("Blinq Messenger"));
    QApplication::setApplicationVersion(QStringLiteral(BLINQ_APP_VERSION));
    QApplication::setQuitOnLastWindowClosed(false);
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/icons/assets/appicon.ico")));
#ifdef Q_OS_WIN
    PlainNativeTitleFilter titleFilter(&a);
    a.installEventFilter(&titleFilter);
#endif
    const bool startupLaunch = a.arguments().contains(QStringLiteral("--startup"));
    const bool restartLaunch = a.arguments().contains(QStringLiteral("--restarted"));

    const QString serverName = QStringLiteral("BlinqMessenger.SingleInstance");
    QLocalSocket socket;
    socket.connectToServer(serverName);
    if (socket.waitForConnected(10)) {
        if (restartLaunch) {
            socket.disconnectFromServer();
            socket.waitForDisconnected(100);

            bool oldInstanceStillRunning = true;
            for (int attempt = 0; attempt < 60; ++attempt) {
                QThread::msleep(100);
                QLocalSocket probe;
                probe.connectToServer(serverName);
                if (!probe.waitForConnected(25)) {
                    oldInstanceStillRunning = false;
                    break;
                }
                probe.disconnectFromServer();
                probe.waitForDisconnected(25);
            }

            if (oldInstanceStillRunning) {
                return 0;
            }
        } else if (!startupLaunch) {
            socket.write("activate");
            socket.flush();
            socket.waitForBytesWritten(150);
            return 0;
        } else {
            return 0;
        }
    }

    QLocalServer::removeServer(serverName);
    QLocalServer server;
    server.listen(serverName);

    MainWindow w;
    QObject::connect(&server, &QLocalServer::newConnection, &w, [&server, &w] {
        while (QLocalSocket *client = server.nextPendingConnection()) {
            client->deleteLater();
        }
        w.showFromTray();
    });
    if (!startupLaunch) {
        w.showInitialWindow();
    } else {
        w.startConnectionServices();
    }
    return QApplication::exec();
}
