#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusError>
#include <QTimer>

#include <csignal>

#include "PeripheralsService.h"

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("peripheralsd"));
    app.setOrganizationName(QStringLiteral("io.github.rodiada"));

    static volatile std::sig_atomic_t stopRequested = 0;
    for (int sig : {SIGTERM, SIGINT})
        std::signal(sig, [](int) { stopRequested = 1; });
    QTimer stopPoll;
    QObject::connect(&stopPoll, &QTimer::timeout, &app, [] {
        if (stopRequested)
            QCoreApplication::quit();
    });
    stopPoll.start(200);

    auto bus = QDBusConnection::sessionBus();
    PeripheralsService service;
    if (!bus.registerObject(QString::fromLatin1(PeripheralsService::Path), &service,
                            QDBusConnection::ExportAllProperties | QDBusConnection::ExportAllSlots)) {
        qCritical("Cannot register D-Bus object: %s", qPrintable(bus.lastError().message()));
        return 1;
    }
    if (!bus.registerService(QString::fromLatin1(PeripheralsService::Service))) {
        qCritical("Cannot own %s (already running?): %s", PeripheralsService::Service, qPrintable(bus.lastError().message()));
        return 1;
    }

    return app.exec();
}
