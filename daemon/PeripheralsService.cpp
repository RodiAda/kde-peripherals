#include "PeripheralsService.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QJsonDocument>

#include "Device.h"
#include "GamepadSource.h"
#include "Log.h"
#include "Registry.h"

PeripheralsService::PeripheralsService(QObject *parent)
    : QObject(parent)
{
    _watcher.addPath(QStringLiteral("/dev"));
    _debounce.setSingleShot(true);
    _debounce.setInterval(500);
    connect(&_watcher, &QFileSystemWatcher::directoryChanged, &_debounce, qOverload<>(&QTimer::start));
    connect(&_debounce, &QTimer::timeout, this, &PeripheralsService::scan);
    _rescan.setInterval(10000);
    connect(&_rescan, &QTimer::timeout, this, &PeripheralsService::scan);
    _rescan.start();
    attach(QStringLiteral("virtual|gamepads"), new GamepadSource(HidrawInfo{}));
    scan();
}

void PeripheralsService::attach(const QString &key, Device *device)
{
    auto *thread = new QThread(this);
    device->moveToThread(thread);
    connect(thread, &QThread::finished, device, &QObject::deleteLater);
    connect(device, &Device::entriesChanged, this, [this, key](const QJsonArray &entries) {
        auto it = _slots.find(key);
        if (it == _slots.end())
            return;
        it->entries = entries;
        publishState();
    });
    connect(device, &Device::gone, this, [this, key] {
        QTimer::singleShot(0, this, [this, key] {
            remove(key);
            publishState();
        });
    });
    _slots.insert(key, Slot{thread, device, {}});
    thread->start();
    QMetaObject::invokeMethod(device, &Device::start, Qt::QueuedConnection);
}

PeripheralsService::~PeripheralsService()
{
    const QStringList keys = _slots.keys();
    for (const QString &key : keys)
        remove(key);
}

void PeripheralsService::scan()
{
    const QList<HidrawInfo> nodes = enumerateHidraw();
    QSet<QString> seen;
    for (const HidrawInfo &info : nodes) {
        for (const Driver &driver : drivers()) {
            if (!driver.matches(info))
                continue;
            const QString key = QString::fromLatin1(driver.name) + QLatin1Char('|') + info.node;
            seen.insert(key);
            if (_slots.contains(key))
                break;
            attach(key, driver.create(info));
            qCInfo(lcPd) << "attached" << driver.name << info.node << info.name;
            break;
        }
    }
    const QStringList keys = _slots.keys();
    bool changed = false;
    for (const QString &key : keys) {
        if (!seen.contains(key) && !key.startsWith(QLatin1String("virtual|"))) {
            remove(key);
            changed = true;
        }
    }
    if (changed)
        publishState();
}

void PeripheralsService::remove(const QString &key)
{
    auto it = _slots.find(key);
    if (it == _slots.end())
        return;
    qCInfo(lcPd) << "detached" << key;
    QThread *thread = it->thread;
    _slots.erase(it);
    thread->quit();
    thread->wait();
    delete thread;
}

void PeripheralsService::publishState()
{
    QJsonArray all;
    for (auto it = _slots.cbegin(); it != _slots.cend(); ++it) {
        for (const QJsonValue &e : it->entries)
            all.append(e);
    }
    QList<QJsonValue> sorted(all.begin(), all.end());
    std::sort(sorted.begin(), sorted.end(), [](const QJsonValue &a, const QJsonValue &b) {
        return a[QStringLiteral("id")].toString() < b[QStringLiteral("id")].toString();
    });
    QJsonArray ordered;
    for (const QJsonValue &v : sorted)
        ordered.append(v);
    const QString json = QString::fromUtf8(QJsonDocument(ordered).toJson(QJsonDocument::Compact));
    if (json == _devices)
        return;
    _devices = json;
    auto signal = QDBusMessage::createSignal(QString::fromLatin1(Path), QStringLiteral("org.freedesktop.DBus.Properties"),
                                             QStringLiteral("PropertiesChanged"));
    signal << QString::fromLatin1(Service) << QVariantMap{{QStringLiteral("Devices"), _devices}} << QStringList();
    QDBusConnection::sessionBus().send(signal);
}

void PeripheralsService::Apply(const QString &id, const QString &json)
{
    QJsonParseError err{};
    const QJsonObject change = QJsonDocument::fromJson(json.toUtf8(), &err).object();
    if (err.error != QJsonParseError::NoError) {
        if (calledFromDBus())
            sendErrorReply(QDBusError::InvalidArgs, err.errorString());
        return;
    }
    for (auto it = _slots.cbegin(); it != _slots.cend(); ++it) {
        for (const QJsonValue &e : it->entries) {
            if (e[QStringLiteral("id")].toString() != id)
                continue;
            QMetaObject::invokeMethod(it->device, "apply", Qt::QueuedConnection, Q_ARG(QString, id),
                                      Q_ARG(QJsonObject, change));
            return;
        }
    }
    if (calledFromDBus())
        sendErrorReply(QStringLiteral("io.github.rodiada.Peripherals.Error.UnknownDevice"), QStringLiteral("No such device"));
}

void PeripheralsService::Rescan()
{
    scan();
}
