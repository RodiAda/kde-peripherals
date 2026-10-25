#include "BatteryHelper.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QRegularExpression>

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <linux/uhid.h>
#include <unistd.h>

namespace {

constexpr int MaxPerOwner = 8;
constexpr int IdleExitMs = 60000;
constexpr uint8_t BatteryReport = 2;

bool writeEvent(int fd, const uhid_event &ev)
{
    return ::write(fd, &ev, sizeof(ev)) == ssize_t(sizeof(ev));
}

}

BatteryHelper::BatteryHelper(QObject *parent)
    : QObject(parent)
{
    QDBusConnection::systemBus().connect(QStringLiteral("org.freedesktop.DBus"), QStringLiteral("/org/freedesktop/DBus"),
                                         QStringLiteral("org.freedesktop.DBus"), QStringLiteral("NameOwnerChanged"), this,
                                         SLOT(onNameOwnerChanged(QString, QString, QString)));
    _idle.setSingleShot(true);
    _idle.setInterval(IdleExitMs);
    connect(&_idle, &QTimer::timeout, qApp, &QCoreApplication::quit);
    _idle.start();
}

BatteryHelper::~BatteryHelper()
{
    const QStringList keys = _batteries.keys();
    for (const QString &k : keys)
        destroy(k);
}

QByteArray BatteryHelper::descriptor(bool keyboard)
{
    const QByteArray device = keyboard
        ? QByteArray::fromHex("05010906a101" "8501" "050719e029e71500250175019508" "8102"
                              "05071900296515002565750895068100")
        : QByteArray::fromHex("05010902a101" "8501" "0901a100" "05091901290315002501950375018102" "950175058103"
                              "0501093009311581257f750895028106" "c0");
    const QByteArray battery = QByteArray::fromHex("8502" "05060920" "15002564" "75089501" "8102"
                                                   "05850944" "15002501" "75019501" "8102" "75079501" "8103");
    return device + battery + QByteArray::fromHex("c0");
}

QString BatteryHelper::sanitize(const QString &text, int maxLength)
{
    QString out = text;
    out.remove(QRegularExpression(QStringLiteral("[^\\p{L}\\p{N} +._:()-]")));
    return out.left(maxLength).trimmed();
}

QString BatteryHelper::key(const QString &owner, const QString &id) const
{
    return owner + QLatin1Char('|') + id;
}

bool BatteryHelper::create(Battery &battery, const QString &uniq)
{
    battery.fd = ::open("/dev/uhid", O_RDWR | O_CLOEXEC | O_NONBLOCK);
    if (battery.fd < 0)
        return false;
    uhid_event ev{};
    ev.type = UHID_CREATE2;
    const QByteArray name = battery.name.toUtf8().left(sizeof(ev.u.create2.name) - 1);
    const QByteArray phys = QByteArrayLiteral("peripherals-batteryd");
    const QByteArray serial = uniq.toLatin1().left(sizeof(ev.u.create2.uniq) - 1);
    std::memcpy(ev.u.create2.name, name.constData(), size_t(name.size()));
    std::memcpy(ev.u.create2.phys, phys.constData(), size_t(phys.size()));
    std::memcpy(ev.u.create2.uniq, serial.constData(), size_t(serial.size()));
    const QByteArray rd = descriptor(battery.keyboard);
    ev.u.create2.rd_size = uint16_t(rd.size());
    std::memcpy(ev.u.create2.rd_data, rd.constData(), size_t(rd.size()));
    ev.u.create2.bus = 0x06;
    ev.u.create2.vendor = 0x1d6b;
    ev.u.create2.product = battery.keyboard ? 0x0105 : 0x0104;
    ev.u.create2.version = 1;
    if (!writeEvent(battery.fd, ev)) {
        ::close(battery.fd);
        battery.fd = -1;
        return false;
    }
    battery.notifier = std::make_unique<QSocketNotifier>(battery.fd, QSocketNotifier::Read);
    connect(battery.notifier.get(), &QSocketNotifier::activated, this, [this, &battery] { service(battery); });
    return true;
}

void BatteryHelper::sendLevel(Battery &battery)
{
    uhid_event ev{};
    ev.type = UHID_INPUT2;
    ev.u.input2.size = 3;
    ev.u.input2.data[0] = BatteryReport;
    ev.u.input2.data[1] = battery.percent;
    ev.u.input2.data[2] = battery.charging ? 1 : 0;
    writeEvent(battery.fd, ev);
}

void BatteryHelper::service(Battery &battery)
{
    uhid_event ev{};
    while (::read(battery.fd, &ev, sizeof(ev)) > 0) {
        if (ev.type == UHID_GET_REPORT) {
            uhid_event reply{};
            reply.type = UHID_GET_REPORT_REPLY;
            reply.u.get_report_reply.id = ev.u.get_report.id;
            if (ev.u.get_report.rnum == BatteryReport) {
                reply.u.get_report_reply.size = 3;
                reply.u.get_report_reply.data[0] = BatteryReport;
                reply.u.get_report_reply.data[1] = battery.percent;
                reply.u.get_report_reply.data[2] = battery.charging ? 1 : 0;
            } else {
                reply.u.get_report_reply.err = EIO;
            }
            writeEvent(battery.fd, reply);
        } else if (ev.type == UHID_SET_REPORT) {
            uhid_event reply{};
            reply.type = UHID_SET_REPORT_REPLY;
            reply.u.set_report_reply.id = ev.u.set_report.id;
            reply.u.set_report_reply.err = EIO;
            writeEvent(battery.fd, reply);
        }
    }
}

void BatteryHelper::Set(const QString &id, const QString &name, const QString &kind, int percent, bool charging)
{
    const QString owner = calledFromDBus() ? message().service() : QString();
    const QString cleanId = sanitize(id, 48);
    const QString cleanName = sanitize(name, 64);
    if (cleanId.isEmpty() || cleanName.isEmpty()) {
        sendErrorReply(QDBusError::InvalidArgs, QStringLiteral("Invalid id or name"));
        return;
    }
    const QString k = key(owner, cleanId);
    const bool keyboard = kind == QLatin1String("keyboard");
    auto it = _batteries.find(k);
    if (it != _batteries.end() && ((*it)->name != cleanName || (*it)->keyboard != keyboard)) {
        destroy(k);
        it = _batteries.end();
    }
    if (it == _batteries.end()) {
        int count = 0;
        for (const auto &b : std::as_const(_batteries))
            count += b->owner == owner ? 1 : 0;
        if (count >= MaxPerOwner) {
            sendErrorReply(QDBusError::LimitsExceeded, QStringLiteral("Too many batteries"));
            return;
        }
        auto battery = std::make_shared<Battery>();
        battery->owner = owner;
        battery->name = cleanName;
        battery->keyboard = keyboard;
        battery->percent = uint8_t(std::clamp(percent, 0, 100));
        battery->charging = charging;
        const uid_t uid = calledFromDBus() ? connection().interface()->serviceUid(owner).value() : 0;
        const QByteArray digest = QCryptographicHash::hash(cleanId.toUtf8(), QCryptographicHash::Sha1).toHex().left(10);
        if (!create(*battery, QStringLiteral("u%1-%2").arg(uid).arg(QString::fromLatin1(digest)))) {
            sendErrorReply(QDBusError::Failed, QStringLiteral("Cannot create uhid device: %1").arg(QString::fromLocal8Bit(std::strerror(errno))));
            return;
        }
        it = _batteries.insert(k, battery);
    }
    (*it)->percent = uint8_t(std::clamp(percent, 0, 100));
    (*it)->charging = charging;
    sendLevel(**it);
    updateIdle();
}

void BatteryHelper::Remove(const QString &id)
{
    const QString owner = calledFromDBus() ? message().service() : QString();
    destroy(key(owner, sanitize(id, 48)));
    updateIdle();
}

void BatteryHelper::destroy(const QString &k)
{
    auto it = _batteries.find(k);
    if (it == _batteries.end())
        return;
    auto battery = *it;
    _batteries.erase(it);
    battery->notifier.reset();
    if (battery->fd >= 0) {
        uhid_event ev{};
        ev.type = UHID_DESTROY;
        writeEvent(battery->fd, ev);
        ::close(battery->fd);
    }
}

void BatteryHelper::onNameOwnerChanged(const QString &name, const QString &, const QString &newOwner)
{
    if (!newOwner.isEmpty() || !name.startsWith(QLatin1Char(':')))
        return;
    const QStringList keys = _batteries.keys();
    for (const QString &k : keys) {
        if (_batteries.value(k)->owner == name)
            destroy(k);
    }
    updateIdle();
}

void BatteryHelper::updateIdle()
{
    if (_batteries.isEmpty())
        _idle.start();
    else
        _idle.stop();
}
