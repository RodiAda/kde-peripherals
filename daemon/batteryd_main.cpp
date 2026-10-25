#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusError>

#include "BatteryHelper.h"

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("peripherals-batteryd"));
    auto bus = QDBusConnection::systemBus();
    BatteryHelper helper;
    if (!bus.registerObject(QString::fromLatin1(BatteryHelper::Path), &helper, QDBusConnection::ExportAllSlots)) {
        qCritical("Cannot register object: %s", qPrintable(bus.lastError().message()));
        return 1;
    }
    if (!bus.registerService(QString::fromLatin1(BatteryHelper::Service))) {
        qCritical("Cannot own %s: %s", BatteryHelper::Service, qPrintable(bus.lastError().message()));
        return 1;
    }
    return app.exec();
}
