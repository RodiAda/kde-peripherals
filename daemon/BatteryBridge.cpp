#include "BatteryBridge.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QJsonObject>

#include "Log.h"

void BatteryBridge::call(const QString &method, const QVariantList &args)
{
    auto msg = QDBusMessage::createMethodCall(QStringLiteral("io.github.rodiada.PeripheralsBattery"),
                                              QStringLiteral("/io/github/rodiada/PeripheralsBattery"),
                                              QStringLiteral("io.github.rodiada.PeripheralsBattery"), method);
    msg.setArguments(args);
    auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::systemBus().asyncCall(msg), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [method](QDBusPendingCallWatcher *w) {
        if (w->isError())
            qCInfo(lcPd) << "battery bridge:" << method << w->error().message();
        w->deleteLater();
    });
}

void BatteryBridge::update(const QJsonArray &devices)
{
    QHash<QString, State> wanted;
    for (const QJsonValue &v : devices) {
        const QJsonObject e = v.toObject();
        if (!e.value(QStringLiteral("bridgeBattery")).toBool() || !e.contains(QStringLiteral("battery")))
            continue;
        State s;
        s.name = e.value(QStringLiteral("name")).toString();
        s.kind = e.value(QStringLiteral("kind")).toString();
        s.percent = e.value(QStringLiteral("battery")).toInt();
        s.charging = e.value(QStringLiteral("charging")).toBool();
        wanted.insert(e.value(QStringLiteral("id")).toString().replace(QLatin1Char(':'), QLatin1Char('.')), s);
    }
    for (auto it = wanted.cbegin(); it != wanted.cend(); ++it) {
        const auto old = _sent.constFind(it.key());
        if (old != _sent.cend() && old->name == it->name && old->kind == it->kind && old->percent == it->percent
            && old->charging == it->charging)
            continue;
        call(QStringLiteral("Set"), {it.key(), it->name, it->kind, it->percent, it->charging});
    }
    for (auto it = _sent.cbegin(); it != _sent.cend(); ++it) {
        if (!wanted.contains(it.key()))
            call(QStringLiteral("Remove"), {it.key()});
    }
    _sent = wanted;
}
