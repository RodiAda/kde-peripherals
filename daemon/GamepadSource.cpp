#include "GamepadSource.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusObjectPath>
#include <QDir>
#include <QFile>
#include <QFileInfo>

#include "Log.h"

namespace {

using InterfaceMap = QMap<QString, QVariantMap>;
using ManagedObjects = QMap<QDBusObjectPath, InterfaceMap>;

constexpr int RefreshIntervalMs = 5000;
constexpr uint16_t GamepadAppearance = 0x03C4;
constexpr uint16_t JoystickAppearance = 0x03C3;

QString readText(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return QString::fromUtf8(f.readAll()).trimmed();
}

bool hasJoystick(const QString &hidDevice)
{
    const QDir input(hidDevice + QStringLiteral("/input"));
    for (const QString &entry : input.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        const QDir dir(input.filePath(entry));
        if (!dir.entryList({QStringLiteral("js*")}, QDir::Dirs).isEmpty())
            return true;
    }
    return false;
}

QString gamepadVendor(const QString &driver, const QString &name)
{
    if (driver == QLatin1String("hid-steam") || name.contains(QLatin1String("Steam"), Qt::CaseInsensitive))
        return QStringLiteral("Valve");
    if (driver.contains(QLatin1String("xpad")) || driver == QLatin1String("microsoft")
        || name.contains(QLatin1String("Xbox"), Qt::CaseInsensitive))
        return QStringLiteral("Microsoft");
    if (driver == QLatin1String("playstation") || driver == QLatin1String("sony"))
        return QStringLiteral("Sony");
    if (driver == QLatin1String("nintendo"))
        return QStringLiteral("Nintendo");
    return {};
}

QString defaultName(const QString &vendor)
{
    if (vendor == QLatin1String("Valve"))
        return QStringLiteral("Steam Controller");
    if (vendor == QLatin1String("Microsoft"))
        return QStringLiteral("Xbox Wireless Controller");
    return QStringLiteral("Gamepad");
}

}

Q_DECLARE_METATYPE(InterfaceMap)
Q_DECLARE_METATYPE(ManagedObjects)

void GamepadSource::start()
{
    qDBusRegisterMetaType<InterfaceMap>();
    qDBusRegisterMetaType<ManagedObjects>();
    _timer.setInterval(RefreshIntervalMs);
    connect(&_timer, &QTimer::timeout, this, &GamepadSource::refresh);
    _timer.start();
    refresh();
}

void GamepadSource::refresh()
{
    const QJsonArray supplies = fromPowerSupplies();
    const QJsonArray bluez = fromBluez(supplies);
    QJsonArray entries;
    for (const QJsonValue &v : supplies) {
        QJsonObject e = v.toObject();
        if (_bluezPaths.contains(e.value(QStringLiteral("address")).toString()))
            e[QStringLiteral("controls")] = QJsonArray{
                actionButton(QStringLiteral("disconnect"), QStringLiteral("other"), QStringLiteral("Disconnect"))};
        entries.append(e);
    }
    for (const QJsonValue &v : bluez)
        entries.append(v);
    publish(entries);
}

QJsonArray GamepadSource::fromPowerSupplies()
{
    QJsonArray entries;
    const QDir dir(QStringLiteral("/sys/class/power_supply"));
    for (const QString &entry : dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::System)) {
        const QString base = dir.filePath(entry);
        if (readText(base + QStringLiteral("/scope")) != QLatin1String("Device"))
            continue;
        const QString hidDevice = QFileInfo(base + QStringLiteral("/device")).canonicalFilePath();
        if (hidDevice.isEmpty())
            continue;
        const QString driver = QFileInfo(hidDevice + QStringLiteral("/driver")).canonicalFilePath().section(QLatin1Char('/'), -1);
        QString name = readText(base + QStringLiteral("/model_name"));
        const QString vendor = gamepadVendor(driver, name + entry);
        if (vendor.isEmpty() && !hasJoystick(hidDevice))
            continue;
        if (name.isEmpty())
            name = defaultName(vendor);

        QString uniq;
        uint16_t bus = 0;
        for (const QString &line : readText(hidDevice + QStringLiteral("/uevent")).split(QLatin1Char('\n'))) {
            if (line.startsWith(QLatin1String("HID_UNIQ=")))
                uniq = line.mid(9).toLower();
            else if (line.startsWith(QLatin1String("HID_ID=")))
                bus = uint16_t(line.mid(7).section(QLatin1Char(':'), 0, 0).toUInt(nullptr, 16));
        }

        QJsonObject e;
        e[QStringLiteral("id")] = QStringLiteral("gamepad:") + (uniq.isEmpty() ? entry : uniq);
        e[QStringLiteral("kind")] = QStringLiteral("gamepad");
        if (!vendor.isEmpty())
            e[QStringLiteral("vendor")] = vendor;
        e[QStringLiteral("name")] = name;
        e[QStringLiteral("transport")] = bus == 0x05 ? QStringLiteral("bluetooth")
                                         : entry.startsWith(QLatin1String("steam-controller")) ? QStringLiteral("dongle")
                                                                                              : QStringLiteral("usb");
        if (!uniq.isEmpty())
            e[QStringLiteral("address")] = uniq;
        const QString present = readText(base + QStringLiteral("/present"));
        e[QStringLiteral("online")] = present.isEmpty() || present == QLatin1String("1");
        bool ok = false;
        const int capacity = readText(base + QStringLiteral("/capacity")).toInt(&ok);
        int level = ok ? capacity : -1;
        if (!ok) {
            const QString l = readText(base + QStringLiteral("/capacity_level"));
            level = l == QLatin1String("Full") ? 100
                : l == QLatin1String("High") ? 75
                : l == QLatin1String("Normal") ? 50
                : l == QLatin1String("Low") ? 20
                : l == QLatin1String("Critical") ? 5
                : -1;
        }
        if (level >= 0) {
            const QString status = readText(base + QStringLiteral("/status"));
            e[QStringLiteral("battery")] = level;
            e[QStringLiteral("charging")] = status == QLatin1String("Charging") || status == QLatin1String("Full");
        }
        e[QStringLiteral("controls")] = QJsonArray{};
        entries.append(e);
    }
    return entries;
}

QJsonArray GamepadSource::fromBluez(const QJsonArray &known)
{
    QJsonArray entries;
    _bluezPaths.clear();
    auto call = QDBusMessage::createMethodCall(QStringLiteral("org.bluez"), QStringLiteral("/"),
                                               QStringLiteral("org.freedesktop.DBus.ObjectManager"),
                                               QStringLiteral("GetManagedObjects"));
    const QDBusMessage reply = QDBusConnection::systemBus().call(call, QDBus::Block, 2000);
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty())
        return entries;
    ManagedObjects objects;
    reply.arguments().constFirst().value<QDBusArgument>() >> objects;
    for (auto it = objects.cbegin(); it != objects.cend(); ++it) {
        const auto dev = it->constFind(QStringLiteral("org.bluez.Device1"));
        if (dev == it->cend() || !dev->value(QStringLiteral("Connected")).toBool())
            continue;
        const uint16_t appearance = uint16_t(dev->value(QStringLiteral("Appearance")).toUInt());
        if (dev->value(QStringLiteral("Icon")).toString() != QLatin1String("input-gaming") && appearance != GamepadAppearance
            && appearance != JoystickAppearance)
            continue;
        const QString address = dev->value(QStringLiteral("Address")).toString().toLower();
        _bluezPaths.insert(address, it.key().path());
        bool duplicate = false;
        for (int i = 0; i < known.size(); ++i)
            duplicate = duplicate || known[i][QStringLiteral("address")].toString() == address;
        if (duplicate)
            continue;
        const QString name = dev->value(QStringLiteral("Alias")).toString();
        QJsonObject e;
        e[QStringLiteral("id")] = QStringLiteral("gamepad:") + address;
        e[QStringLiteral("kind")] = QStringLiteral("gamepad");
        const QString vendor = gamepadVendor({}, name);
        if (!vendor.isEmpty())
            e[QStringLiteral("vendor")] = vendor;
        e[QStringLiteral("name")] = name;
        e[QStringLiteral("transport")] = QStringLiteral("bluetooth");
        e[QStringLiteral("address")] = address;
        e[QStringLiteral("online")] = true;
        const auto battery = it->constFind(QStringLiteral("org.bluez.Battery1"));
        if (battery != it->cend()) {
            e[QStringLiteral("battery")] = int(battery->value(QStringLiteral("Percentage")).toUInt());
            e[QStringLiteral("charging")] = false;
        }
        e[QStringLiteral("controls")] = QJsonArray{actionButton(QStringLiteral("disconnect"), QStringLiteral("other"),
                                                                QStringLiteral("Disconnect"))};
        entries.append(e);
    }
    return entries;
}

void GamepadSource::apply(const QString &id, const QJsonObject &change)
{
    if (!change.contains(QStringLiteral("disconnect")))
        return;
    const QString address = id.section(QLatin1Char(':'), 1);
    const QString path = _bluezPaths.value(address);
    if (path.isEmpty())
        return;
    auto call = QDBusMessage::createMethodCall(QStringLiteral("org.bluez"), path, QStringLiteral("org.bluez.Device1"),
                                               QStringLiteral("Disconnect"));
    QDBusConnection::systemBus().call(call, QDBus::Block, 10000);
    refresh();
}
