#include "LogitechDevice.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QJsonDocument>
#include <QSettings>

#include <cstdlib>

#include "Log.h"

using namespace hidpp;

namespace {

constexpr int ReplyTimeoutMs = 600;
constexpr int TickIntervalMs = 30000;
constexpr uint8_t ErrUnknownDevice = 0x08;

const QList<uint16_t> ReceiverProducts{0xC52B, 0xC532, 0xC534, 0xC539, 0xC53A, 0xC53D, 0xC53F,
                                       0xC541, 0xC545, 0xC547, 0xC548, 0xC54D};

QByteArray bytes(std::initializer_list<int> values)
{
    QByteArray b;
    for (int v : values)
        b.append(char(v & 0xFF));
    return b;
}

struct ButtonInfo {
    uint16_t cid;
    const char *label;
};

constexpr ButtonInfo KnownButtons[] = {
    {0x00C3, "Gesture button"},
    {0x01A0, "Actions button"},
    {0x00C4, "Mode shift button"},
    {0x0052, "Middle button"},
    {0x0053, "Back button"},
    {0x0056, "Forward button"},
};

constexpr uint16_t GestureButton = 0x00C3;
constexpr int GestureThreshold = 150;
const char *const GestureDirections[] = {"up", "down", "left", "right", "click"};
const char *const GestureLabels[] = {"Gesture up", "Gesture down", "Gesture left", "Gesture right", "Gesture click"};

struct ButtonAction {
    const char *id;
    const char *label;
    const char *component;
    const char *shortcut;
};

constexpr ButtonAction ButtonActions[] = {
    {"default", "Default", nullptr, nullptr},
    {"gestures", "Gestures", nullptr, nullptr},
    {"none", "Do nothing", nullptr, nullptr},
    {"overview", "Overview", "kwin", "Overview"},
    {"grid", "Desktop grid", "kwin", "Grid View"},
    {"expose", "Windows of the current desktop", "kwin", "Expose"},
    {"exposeAll", "All windows", "kwin", "ExposeAll"},
    {"desktop", "Show desktop", "kwin", "Show Desktop"},
    {"desktopLeft", "Previous desktop", "kwin", "Switch One Desktop to the Left"},
    {"desktopRight", "Next desktop", "kwin", "Switch One Desktop to the Right"},
    {"maximize", "Maximize window", "kwin", "Window Maximize"},
    {"minimize", "Minimize window", "kwin", "Window Minimize"},
    {"close", "Close window", "kwin", "Window Close"},
    {"launcher", "Application launcher", "plasmashell", "activate application launcher"},
    {"krunner", "KRunner", "org_kde_krunner_desktop", "_launch"},
    {"screenshot", "Screenshot of a region", "org_kde_spectacle_desktop", "RectangularRegionScreenShot"},
    {"playPause", "Play/pause", "mediacontrol", "playpausemedia"},
    {"nextTrack", "Next track", "mediacontrol", "nextmedia"},
    {"previousTrack", "Previous track", "mediacontrol", "previousmedia"},
    {"volumeUp", "Volume up", "kmix", "increase_volume"},
    {"volumeDown", "Volume down", "kmix", "decrease_volume"},
    {"mute", "Mute", "kmix", "mute"},
};

const ButtonAction *findAction(const QString &id)
{
    for (const ButtonAction &a : ButtonActions) {
        if (id == QLatin1String(a.id))
            return &a;
    }
    return nullptr;
}

QString kindFromType(int type)
{
    switch (type) {
    case 0:
    case 2:
        return QStringLiteral("keyboard");
    }
    return QStringLiteral("mouse");
}

}

bool LogitechDevice::matches(const HidrawInfo &info)
{
    if (info.vendor != 0x046D || !info.hasReportId(LongReport) || info.driver == QLatin1String("logitech-djreceiver"))
        return false;
    return info.hasUsagePage(0xFF00) || info.hasUsagePage(0xFF43);
}

void LogitechDevice::start()
{
    if (!_dev.open(_node.node)) {
        qCWarning(lcPd) << "logitech:" << _node.node << _dev.errorString();
        Q_EMIT gone();
        return;
    }
    _receiver = _node.bus == 0x03 && ReceiverProducts.contains(_node.product);
    if (_node.receiverIndex() > 0)
        _directIndex = uint8_t(_node.receiverIndex());
    _notifier = std::make_unique<QSocketNotifier>(_dev.fd(), QSocketNotifier::Read);
    connect(_notifier.get(), &QSocketNotifier::activated, this, [this] {
        while (auto r = _dev.read(0))
            handleNotification(*r);
        if (!_reprobe.isEmpty())
            QTimer::singleShot(300, this, &LogitechDevice::tick);
    });
    _timer.setInterval(TickIntervalMs);
    connect(&_timer, &QTimer::timeout, this, &LogitechDevice::tick);
    _timer.start();
    if (_receiver) {
        for (uint8_t i = 1; i <= 6; ++i)
            probe(i);
    } else {
        probe(_directIndex);
    }
    _sinceBattery.start();
    render();
}

void LogitechDevice::handleNotification(const QByteArray &report)
{
    if (report.size() < 4)
        return;
    const uint8_t index = uint8_t(report[1]);
    if (uint8_t(report[2]) == 0x41 && !_reprobe.contains(index))
        _reprobe.append(index);
    auto it = _slots.find(index);
    if (it == _slots.end())
        return;
    const auto reprog = it->features.constFind(ReprogControls);
    if (reprog == it->features.cend() || !*reprog || uint8_t(report[2]) != *reprog)
        return;
    if (uint8_t(report[3]) == 0x00)
        handleButtons(*it, report);
    else if (uint8_t(report[3]) == 0x10)
        handleRawXY(*it, report);
}

void LogitechDevice::runAction(const QString &id)
{
    const ButtonAction *action = findAction(id);
    if (!action || !action->component)
        return;
    auto msg = QDBusMessage::createMethodCall(QStringLiteral("org.kde.kglobalaccel"),
                                              QStringLiteral("/component/") + QLatin1String(action->component),
                                              QStringLiteral("org.kde.kglobalaccel.Component"),
                                              QStringLiteral("invokeShortcut"));
    msg << QString::fromLatin1(action->shortcut);
    QDBusConnection::sessionBus().asyncCall(msg);
}

void LogitechDevice::handleRawXY(Slot &slot, const QByteArray &report)
{
    if (!slot.gestureActive || report.size() < 8)
        return;
    slot.gestureDx += int16_t((uint8_t(report[4]) << 8) | uint8_t(report[5]));
    slot.gestureDy += int16_t((uint8_t(report[6]) << 8) | uint8_t(report[7]));
}

void LogitechDevice::finishGesture(Slot &slot)
{
    slot.gestureActive = false;
    const int dx = slot.gestureDx;
    const int dy = slot.gestureDy;
    QString direction;
    if (std::abs(dx) < GestureThreshold && std::abs(dy) < GestureThreshold)
        direction = QStringLiteral("click");
    else if (std::abs(dx) > std::abs(dy))
        direction = dx > 0 ? QStringLiteral("right") : QStringLiteral("left");
    else
        direction = dy > 0 ? QStringLiteral("down") : QStringLiteral("up");
    runAction(slot.gestures.value(direction));
}

QString LogitechDevice::settingsGroup(const Slot &slot) const
{
    return QStringLiteral("logitech-") + QString::fromLatin1(slot.name.toUtf8().toHex());
}

void LogitechDevice::loadButtons(Slot &slot)
{
    slot.buttons.clear();
    const Reply count = call(slot, ReprogControls, 0);
    for (int i = 0; count.ok && i < uint8_t(count.params[0]); ++i) {
        const Reply info = call(slot, ReprogControls, 1, QByteArray(1, char(i)));
        if (!info.ok || !(uint8_t(info.params[4]) & 0x20))
            continue;
        const uint16_t cid = uint16_t((uint8_t(info.params[0]) << 8) | uint8_t(info.params[1]));
        for (const ButtonInfo &b : KnownButtons) {
            if (b.cid == cid)
                slot.buttons.append(cid);
        }
    }
    QSettings settings;
    settings.beginGroup(settingsGroup(slot));
    for (uint16_t cid : std::as_const(slot.buttons)) {
        const QString action = settings.value(QStringLiteral("button-%1").arg(cid, 4, 16, QLatin1Char('0'))).toString();
        if (findAction(action) && action != QLatin1String("default")
            && (action != QLatin1String("gestures") || cid == GestureButton))
            slot.actions.insert(cid, action);
    }
    slot.gestures = {{QStringLiteral("up"), QStringLiteral("overview")},
                     {QStringLiteral("down"), QStringLiteral("desktop")},
                     {QStringLiteral("left"), QStringLiteral("desktopLeft")},
                     {QStringLiteral("right"), QStringLiteral("desktopRight")},
                     {QStringLiteral("click"), QStringLiteral("overview")}};
    for (const char *direction : GestureDirections) {
        const QString action = settings.value(QStringLiteral("gesture-%1").arg(QLatin1String(direction))).toString();
        if (findAction(action))
            slot.gestures.insert(QString::fromLatin1(direction), action);
    }
}

void LogitechDevice::applyDiverts(Slot &slot)
{
    for (uint16_t cid : std::as_const(slot.buttons)) {
        uint8_t flags = slot.actions.contains(cid) ? 0x03 : 0x02;
        if (cid == GestureButton)
            flags |= slot.actions.value(cid) == QLatin1String("gestures") ? 0x30 : 0x20;
        call(slot, ReprogControls, 3, bytes({cid >> 8, cid, flags, 0, 0}));
    }
}

void LogitechDevice::handleButtons(Slot &slot, const QByteArray &report)
{
    QList<uint16_t> now;
    for (int i = 4; i + 1 < report.size() && i < 12; i += 2) {
        const uint16_t cid = uint16_t((uint8_t(report[i]) << 8) | uint8_t(report[i + 1]));
        if (cid)
            now.append(cid);
    }
    const bool gestureMode = slot.actions.value(GestureButton) == QLatin1String("gestures");
    for (uint16_t cid : std::as_const(now)) {
        if (slot.pressed.contains(cid))
            continue;
        if (cid == GestureButton && gestureMode) {
            slot.gestureActive = true;
            slot.gestureDx = 0;
            slot.gestureDy = 0;
            continue;
        }
        runAction(slot.actions.value(cid));
    }
    if (gestureMode && slot.gestureActive && !now.contains(GestureButton))
        finishGesture(slot);
    slot.pressed = now;
}

LogitechDevice::Reply LogitechDevice::transact(uint8_t device, uint8_t featureIndex, uint8_t function,
                                               const QByteArray &params)
{
    Reply reply;
    if (_notifier)
        _notifier->setEnabled(false);
    while (auto r = _dev.read(0))
        handleNotification(*r);
    if (_dev.write(request(device, featureIndex, function, params))) {
        QElapsedTimer t;
        t.start();
        while (t.elapsed() < ReplyTimeoutMs) {
            const auto r = _dev.read(int(ReplyTimeoutMs - t.elapsed()));
            if (!r)
                break;
            if (isError(*r, device, featureIndex, function)) {
                reply.error = uint8_t(r->at(5));
                break;
            }
            if (isReply(*r, device, featureIndex, function)) {
                reply.ok = true;
                reply.params = r->mid(4);
                reply.params.resize(16, '\0');
                break;
            }
            handleNotification(*r);
        }
    }
    if (_notifier)
        _notifier->setEnabled(true);
    return reply;
}

std::optional<uint8_t> LogitechDevice::featureIndex(Slot &slot, uint16_t feature)
{
    if (feature == Root)
        return 0;
    const auto it = slot.features.constFind(feature);
    if (it != slot.features.cend())
        return *it ? std::optional<uint8_t>(*it) : std::nullopt;
    const Reply r = transact(slot.index, 0, 0, bytes({feature >> 8, feature}));
    if (!r.ok)
        return std::nullopt;
    const uint8_t index = uint8_t(r.params[0]);
    slot.features.insert(feature, index);
    return index ? std::optional<uint8_t>(index) : std::nullopt;
}

LogitechDevice::Reply LogitechDevice::call(Slot &slot, uint16_t feature, uint8_t function, const QByteArray &params)
{
    const auto index = featureIndex(slot, feature);
    if (!index)
        return {};
    return transact(slot.index, *index, function, params);
}

void LogitechDevice::probe(uint8_t index)
{
    const Reply ping = transact(index, 0, 1, bytes({0, 0, 0x5A}));
    auto it = _slots.find(index);
    if (!ping.ok) {
        if (it != _slots.end())
            it->online = false;
        if (ping.error == ErrUnknownDevice && it != _slots.end())
            _slots.erase(it);
        return;
    }
    if (it == _slots.end()) {
        it = _slots.insert(index, Slot{});
        it->index = index;
    }
    const bool wasOnline = it->online;
    it->online = true;
    if (!it->loaded) {
        load(*it);
        loadButtons(*it);
        applyDiverts(*it);
    } else if (!wasOnline) {
        applyDiverts(*it);
    }
    readState(*it);
}

void LogitechDevice::load(Slot &slot)
{
    const Reply count = call(slot, DeviceName, 0);
    if (count.ok) {
        const int length = uint8_t(count.params[0]);
        QByteArray name;
        while (name.size() < length) {
            const Reply chunk = call(slot, DeviceName, 1, QByteArray(1, char(name.size())));
            if (!chunk.ok)
                break;
            name += chunk.params.left(std::min<int>(16, length - name.size()));
        }
        slot.name = QString::fromUtf8(name).trimmed();
        const Reply type = call(slot, DeviceName, 2);
        if (type.ok)
            slot.type = uint8_t(type.params[0]);
    }
    const Reply entities = call(slot, FirmwareVersion, 0);
    for (int e = 0; entities.ok && e < uint8_t(entities.params[0]); ++e) {
        const Reply fw = call(slot, FirmwareVersion, 1, QByteArray(1, char(e)));
        if (!fw.ok || uint8_t(fw.params[0]) != 0)
            continue;
        QByteArray prefix = fw.params.mid(1, 3);
        prefix.replace('\0', QByteArray());
        slot.firmware = QStringLiteral("%1 %2.%3.B%4")
                            .arg(QString::fromLatin1(prefix).trimmed())
                            .arg(uint8_t(fw.params[4]), 2, 16, QLatin1Char('0'))
                            .arg(uint8_t(fw.params[5]), 2, 16, QLatin1Char('0'))
                            .arg((uint8_t(fw.params[6]) << 8) | uint8_t(fw.params[7]), 4, 16, QLatin1Char('0'));
        break;
    }
    const Reply dpis = call(slot, AdjustableDpi, 1, QByteArray(1, '\0'));
    if (dpis.ok)
        slot.dpiList = parseDpiList(dpis.params);
    const Reply rates = call(slot, ReportRate, 0);
    if (rates.ok)
        slot.rates = reportRatesFromMask(uint8_t(rates.params[0]));
    const Reply hosts = call(slot, ChangeHost, 0);
    if (hosts.ok) {
        slot.hosts = uint8_t(hosts.params[0]);
        slot.currentHost = uint8_t(hosts.params[1]);
    }
    slot.loaded = !slot.name.isEmpty();
    readBattery(slot);
}

void LogitechDevice::readBattery(Slot &slot)
{
    if (const Reply r = call(slot, UnifiedBattery, 1); r.ok) {
        const int soc = uint8_t(r.params[0]);
        slot.battery = soc > 0 ? std::min(soc, 100) : unifiedBatteryLevel(uint8_t(r.params[1]));
        const int status = uint8_t(r.params[2]);
        slot.charging = status >= 1 && status <= 3;
        return;
    }
    if (const Reply r = call(slot, BatteryStatus, 0); r.ok) {
        slot.battery = std::min<int>(uint8_t(r.params[0]), 100);
        const int status = uint8_t(r.params[2]);
        slot.charging = status >= 1 && status <= 3;
        return;
    }
    if (const Reply r = call(slot, BatteryVoltage, 0); r.ok) {
        const int mv = (uint8_t(r.params[0]) << 8) | uint8_t(r.params[1]);
        slot.battery = batteryPercentFromVoltage(mv);
        slot.charging = (uint8_t(r.params[2]) & 0x80) != 0;
    }
}

void LogitechDevice::readState(Slot &slot)
{
    if (const Reply r = call(slot, AdjustableDpi, 2, QByteArray(1, '\0')); r.ok)
        slot.dpi = (uint8_t(r.params[1]) << 8) | uint8_t(r.params[2]);
    if (const Reply r = call(slot, ReportRate, 1); r.ok && r.params[0])
        slot.rate = 1000 / uint8_t(r.params[0]);
    Reply ss = call(slot, SmartShiftEnhanced, 1);
    if (!ss.ok)
        ss = call(slot, SmartShift, 0);
    if (ss.ok) {
        slot.wheelMode = uint8_t(ss.params[0]);
        slot.autoDisengage = uint8_t(ss.params[1]);
        slot.autoDisengageDefault = uint8_t(ss.params[2]);
    }
    if (const Reply r = call(slot, HiresWheel, 1); r.ok)
        slot.hiresFlags = uint8_t(r.params[0]);
    if (const Reply r = call(slot, ThumbWheel, 1); r.ok)
        slot.thumbInvert = uint8_t(r.params[1]) & 1;
    if (const Reply r = call(slot, NewFnInversion, 0); r.ok)
        slot.fnInversion = uint8_t(r.params[0]) & 1;
    else if (const Reply r2 = call(slot, FnInversion, 0); r2.ok)
        slot.fnInversion = uint8_t(r2.params[0]) & 1;
}

void LogitechDevice::tick()
{
    const QList<uint8_t> pending = _reprobe;
    _reprobe.clear();
    for (uint8_t index : pending)
        probe(index);
    if (_sinceBattery.elapsed() >= TickIntervalMs) {
        _sinceBattery.start();
        if (_receiver) {
            for (uint8_t i = 1; i <= 6; ++i) {
                auto it = _slots.find(i);
                if (it == _slots.end() || !it->online)
                    probe(i);
                else
                    readBattery(*it);
            }
        } else {
            auto it = _slots.find(_directIndex);
            if (it == _slots.end() || !it->online)
                probe(_directIndex);
            else
                readBattery(*it);
        }
    }
    if (_dev.errorString() == QLatin1String("device gone")) {
        _timer.stop();
        Q_EMIT gone();
        return;
    }
    render();
}

QString LogitechDevice::entryId(const Slot &slot) const
{
    return QStringLiteral("logitech:%1:%2").arg(_node.physicalKey()).arg(slot.index);
}

void LogitechDevice::render()
{
    QJsonArray entries;
    for (const Slot &slot : std::as_const(_slots)) {
        if (!slot.loaded)
            continue;
        QJsonObject e;
        e[QStringLiteral("id")] = entryId(slot);
        e[QStringLiteral("kind")] = kindFromType(slot.type);
        e[QStringLiteral("vendor")] = QStringLiteral("Logitech");
        e[QStringLiteral("name")] = slot.name;
        e[QStringLiteral("transport")] = _receiver || _node.receiverIndex() > 0 ? QStringLiteral("dongle")
                                         : _node.bus == 0x05 ? QStringLiteral("bluetooth")
                                                             : QStringLiteral("usb");
        e[QStringLiteral("online")] = slot.online;
        e[QStringLiteral("untested")] = !slot.name.startsWith(QLatin1String("MX Master 4"));
        if (slot.battery >= 0) {
            e[QStringLiteral("battery")] = slot.battery;
            e[QStringLiteral("charging")] = slot.charging;
        }
        if (!slot.firmware.isEmpty())
            e[QStringLiteral("firmware")] = slot.firmware;
        if (!slot.error.isEmpty())
            e[QStringLiteral("error")] = slot.error;

        QJsonArray controls;
        if (slot.dpi > 0 && slot.dpiList.size() > 1) {
            const int min = slot.dpiList.first();
            const int max = slot.dpiList.last();
            if (slot.dpiList.size() > 12) {
                controls.append(slider(QStringLiteral("dpi"), QStringLiteral("performance"), QStringLiteral("DPI"), slot.dpi,
                                       min, max, slot.dpiList[1] - slot.dpiList[0]));
            } else {
                QJsonArray options;
                for (int d : slot.dpiList)
                    options.append(option(d));
                controls.append(choice(QStringLiteral("dpi"), QStringLiteral("performance"), QStringLiteral("DPI"),
                                       slot.dpi, options));
            }
        }
        if (slot.rate > 0 && slot.rates.size() > 1) {
            QJsonArray options;
            for (int hz : slot.rates)
                options.append(option(hz));
            controls.append(choice(QStringLiteral("pollingRate"), QStringLiteral("performance"), QStringLiteral("Polling rate"),
                                   slot.rate, options, QStringLiteral("Hz")));
        }
        if (slot.wheelMode > 0) {
            controls.append(choice(QStringLiteral("wheelMode"), QStringLiteral("buttons"), QStringLiteral("Scroll wheel"),
                                   slot.wheelMode,
                                   {option(2, QStringLiteral("Ratchet")), option(1, QStringLiteral("Free spin"))}));
            const bool smart = slot.autoDisengage != 0xFF;
            controls.append(toggle(QStringLiteral("smartShift"), QStringLiteral("buttons"), QStringLiteral("SmartShift"), smart));
            if (smart)
                controls.append(slider(QStringLiteral("smartShiftThreshold"), QStringLiteral("buttons"),
                                       QStringLiteral("SmartShift sensitivity"), std::clamp(slot.autoDisengage, 1, 50), 1, 50, 1));
        }
        if (slot.hiresFlags >= 0)
            controls.append(toggle(QStringLiteral("invertScroll"), QStringLiteral("buttons"), QStringLiteral("Natural scrolling"),
                                   slot.hiresFlags & 0x04));
        if (slot.thumbInvert >= 0)
            controls.append(toggle(QStringLiteral("thumbInvert"), QStringLiteral("buttons"), QStringLiteral("Invert thumb wheel"),
                                   slot.thumbInvert == 1));
        if (slot.fnInversion >= 0)
            controls.append(toggle(QStringLiteral("fnInversion"), QStringLiteral("keys"),
                                   QStringLiteral("Media keys without Fn"), slot.fnInversion == 1));
        for (uint16_t cid : slot.buttons) {
            QJsonArray options;
            for (const ButtonAction &a : ButtonActions) {
                if (QLatin1String(a.id) == QLatin1String("gestures") && cid != GestureButton)
                    continue;
                options.append(option(QString::fromLatin1(a.id), QString::fromLatin1(a.label)));
            }
            const char *label = "";
            for (const ButtonInfo &b : KnownButtons) {
                if (b.cid == cid)
                    label = b.label;
            }
            controls.append(choice(QStringLiteral("button.%1").arg(cid, 4, 16, QLatin1Char('0')), QStringLiteral("keys"),
                                   QString::fromLatin1(label), slot.actions.value(cid, QStringLiteral("default")), options));
        }
        if (slot.actions.value(GestureButton) == QLatin1String("gestures")) {
            QJsonArray options;
            for (const ButtonAction &a : ButtonActions) {
                const QLatin1String id(a.id);
                if (id == QLatin1String("default") || id == QLatin1String("gestures"))
                    continue;
                options.append(option(QString::fromLatin1(a.id), QString::fromLatin1(a.label)));
            }
            for (size_t i = 0; i < std::size(GestureDirections); ++i) {
                const QString direction = QString::fromLatin1(GestureDirections[i]);
                controls.append(choice(QStringLiteral("gesture.") + direction, QStringLiteral("gestures"),
                                       QString::fromLatin1(GestureLabels[i]), slot.gestures.value(direction),
                                       options));
            }
        }
        if (slot.hosts > 1 && slot.currentHost >= 0) {
            QJsonArray options;
            for (int h = 0; h < slot.hosts; ++h)
                options.append(option(h, QStringLiteral("Channel %1").arg(h + 1)));
            controls.append(choice(QStringLiteral("easySwitch"), QStringLiteral("other"), QStringLiteral("Easy-Switch channel"),
                                   slot.currentHost, options));
        }
        e[QStringLiteral("controls")] = controls;
        entries.append(e);
    }
    publish(entries);
}

void LogitechDevice::apply(const QString &id, const QJsonObject &change)
{
    for (Slot &slot : _slots) {
        if (entryId(slot) != id)
            continue;
        slot.error = slot.online ? applyChange(slot, change) : QStringLiteral("Device is asleep");
        if (!slot.error.isEmpty())
            qCWarning(lcPd) << "logitech apply:" << slot.error << QJsonDocument(change).toJson(QJsonDocument::Compact);
        if (slot.online)
            readState(slot);
        render();
        return;
    }
}

QString LogitechDevice::applyChange(Slot &slot, const QJsonObject &change)
{
    const QString failed = QStringLiteral("Write failed");
    if (change.contains(QStringLiteral("dpi"))) {
        const int dpi = change.value(QStringLiteral("dpi")).toInt();
        if (!call(slot, AdjustableDpi, 3, bytes({0, dpi >> 8, dpi})).ok)
            return failed;
    }
    if (change.contains(QStringLiteral("pollingRate"))) {
        const int hz = change.value(QStringLiteral("pollingRate")).toInt();
        if (!slot.rates.contains(hz) || !call(slot, ReportRate, 2, QByteArray(1, char(1000 / hz))).ok)
            return failed;
    }
    const bool wheel = change.contains(QStringLiteral("wheelMode")) || change.contains(QStringLiteral("smartShift"))
        || change.contains(QStringLiteral("smartShiftThreshold"));
    if (wheel) {
        uint8_t mode = 0;
        uint8_t disengage = 0;
        if (change.contains(QStringLiteral("wheelMode")))
            mode = uint8_t(std::clamp(change.value(QStringLiteral("wheelMode")).toInt(), 1, 2));
        if (change.contains(QStringLiteral("smartShift"))) {
            const bool on = change.value(QStringLiteral("smartShift")).toBool();
            disengage = on ? uint8_t(std::clamp(slot.autoDisengageDefault, 1, 50)) : 0xFF;
            if (on && slot.autoDisengageDefault == 0)
                disengage = 10;
        }
        if (change.contains(QStringLiteral("smartShiftThreshold")))
            disengage = uint8_t(std::clamp(change.value(QStringLiteral("smartShiftThreshold")).toInt(), 1, 50));
        const QByteArray params = bytes({mode, disengage, 0});
        const bool enhanced = featureIndex(slot, SmartShiftEnhanced).has_value();
        if (!(enhanced ? call(slot, SmartShiftEnhanced, 2, params) : call(slot, SmartShift, 1, params)).ok)
            return failed;
    }
    if (change.contains(QStringLiteral("thumbInvert"))) {
        const char v = change.value(QStringLiteral("thumbInvert")).toBool() ? 1 : 0;
        if (!call(slot, ThumbWheel, 2, bytes({0, v})).ok)
            return failed;
    }
    if (change.contains(QStringLiteral("invertScroll")) && slot.hiresFlags >= 0) {
        uint8_t flags = uint8_t(slot.hiresFlags & 0x07);
        if (change.contains(QStringLiteral("invertScroll")))
            flags = change.value(QStringLiteral("invertScroll")).toBool() ? (flags | 0x04) : (flags & ~0x04);
        if (!call(slot, HiresWheel, 2, QByteArray(1, char(flags))).ok)
            return failed;
    }
    if (change.contains(QStringLiteral("fnInversion"))) {
        const char v = change.value(QStringLiteral("fnInversion")).toBool() ? 1 : 0;
        const bool ok = featureIndex(slot, NewFnInversion) ? call(slot, NewFnInversion, 1, QByteArray(1, v)).ok
                                                           : call(slot, FnInversion, 1, QByteArray(1, v)).ok;
        if (!ok)
            return failed;
    }
    bool buttonsChanged = false;
    for (uint16_t cid : std::as_const(slot.buttons)) {
        const QString key = QStringLiteral("button.%1").arg(cid, 4, 16, QLatin1Char('0'));
        if (!change.contains(key))
            continue;
        const QString action = change.value(key).toString();
        if (!findAction(action) || (action == QLatin1String("gestures") && cid != GestureButton))
            return QStringLiteral("Invalid button action");
        if (action == QLatin1String("default"))
            slot.actions.remove(cid);
        else
            slot.actions.insert(cid, action);
        QSettings settings;
        settings.beginGroup(settingsGroup(slot));
        settings.setValue(QStringLiteral("button-%1").arg(cid, 4, 16, QLatin1Char('0')), action);
        buttonsChanged = true;
    }
    for (const char *direction : GestureDirections) {
        const QString key = QStringLiteral("gesture.") + QLatin1String(direction);
        if (!change.contains(key))
            continue;
        const QString action = change.value(key).toString();
        if (!findAction(action) || action == QLatin1String("gestures") || action == QLatin1String("default"))
            return QStringLiteral("Invalid button action");
        slot.gestures.insert(QString::fromLatin1(direction), action);
        QSettings settings;
        settings.beginGroup(settingsGroup(slot));
        settings.setValue(QStringLiteral("gesture-%1").arg(QLatin1String(direction)), action);
    }
    if (buttonsChanged)
        applyDiverts(slot);
    if (change.contains(QStringLiteral("easySwitch"))) {
        const int host = change.value(QStringLiteral("easySwitch")).toInt();
        if (host < 0 || host >= slot.hosts)
            return QStringLiteral("Invalid value");
        call(slot, ChangeHost, 1, QByteArray(1, char(host)));
        slot.online = false;
    }
    return {};
}
