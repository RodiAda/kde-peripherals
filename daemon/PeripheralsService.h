#pragma once

#include <QDBusContext>
#include <QFileSystemWatcher>
#include <QHash>
#include <QJsonArray>
#include <QObject>
#include <QThread>
#include <QTimer>

#include "BatteryBridge.h"

class Device;

class PeripheralsService : public QObject, protected QDBusContext
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "io.github.rodiada.Peripherals")
    Q_PROPERTY(QString Devices READ devices)

public:
    static constexpr auto Service = "io.github.rodiada.Peripherals";
    static constexpr auto Path = "/io/github/rodiada/Peripherals";

    explicit PeripheralsService(QObject *parent = nullptr);
    ~PeripheralsService() override;

    QString devices() const { return _devices; }

public Q_SLOTS:
    void Apply(const QString &id, const QString &json);
    void Rescan();

private:
    struct Slot {
        QThread *thread = nullptr;
        Device *device = nullptr;
        QJsonArray entries;
    };

    void scan();
    void attach(const QString &key, Device *device);
    void remove(const QString &key);
    void publishState();

    QHash<QString, Slot> _slots;
    QFileSystemWatcher _watcher;
    QTimer _rescan;
    QTimer _debounce;
    QString _devices = QStringLiteral("[]");
    BatteryBridge _batteries;
};
