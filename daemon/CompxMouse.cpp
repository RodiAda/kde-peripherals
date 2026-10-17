#include "CompxMouse.h"

#include <QJsonDocument>
#include <QSettings>
#include <QThread>

#include "Log.h"

using namespace compx;

namespace {

constexpr int PollIntervalMs = 2000;
constexpr qint64 BatteryIntervalMs = 60000;
constexpr int ReplyTimeoutMs = 400;
constexpr uint16_t AddrBlock0 = 0x0000;
constexpr uint16_t AddrLod = 0x000A;
constexpr uint16_t AddrDpi = 0x000C;
constexpr uint16_t AddrLedColors = 0x002C;
constexpr uint16_t AddrLedEffect = 0x004C;
constexpr uint16_t AddrButtons = 0x0060;
constexpr uint16_t AddrFunction = 0x00A9;
constexpr uint16_t AddrSensor = 0x00B5;
constexpr uint16_t AddrAngle = 0x00BD;
constexpr int ButtonCount = 5;
constexpr int MaxStages = 8;

const char *const ButtonLabels[ButtonCount] = {"Left button", "Right button", "Middle button", "Back button",
                                               "Forward button"};

struct ButtonAction {
    const char *value;
    const char *label;
};

constexpr ButtonAction ButtonActions[] = {
    {"1:1:0", "Left click"},    {"1:2:0", "Right click"},  {"1:4:0", "Middle click"}, {"1:8:0", "Back"},
    {"1:16:0", "Forward"},      {"2:1:0", "DPI cycle"},    {"2:2:0", "DPI up"},       {"2:3:0", "DPI down"},
    {"11:1:0", "Scroll up"},    {"11:2:0", "Scroll down"}, {"3:1:0", "Scroll left"},  {"3:2:0", "Scroll right"},
    {"0:0:0", "Disabled"},
};

void setPair(QByteArray &block, int offset, uint8_t v)
{
    if (block.size() < offset + 2)
        block.resize(offset + 2, '\0');
    block[offset] = char(v);
    block[offset + 1] = char(pairCrc(v));
}

QString hex(uint8_t v)
{
    return QStringLiteral("%1").arg(v, 2, 16, QLatin1Char('0'));
}

QString buttonValue(const QByteArray &buttons, int index)
{
    if (buttons.size() < (index + 1) * 4)
        return {};
    const auto *b = reinterpret_cast<const uint8_t *>(buttons.constData()) + index * 4;
    return QStringLiteral("%1:%2:%3").arg(b[0]).arg(b[1]).arg(b[2]);
}

std::optional<std::array<uint8_t, 3>> parseButton(const QString &value)
{
    const QStringList parts = value.split(QLatin1Char(':'));
    if (parts.size() != 3)
        return std::nullopt;
    std::array<uint8_t, 3> r{};
    for (int i = 0; i < 3; ++i) {
        bool ok = false;
        const uint v = parts[i].toUInt(&ok);
        if (!ok || v > 255)
            return std::nullopt;
        r[size_t(i)] = uint8_t(v);
    }
    return r;
}

}

bool CompxMouse::matches(const HidrawInfo &info)
{
    return info.vendor == 0x373B && info.hasReportId(ReportId) && info.hasUsagePage(0xFF02);
}

void CompxMouse::start()
{
    if (!_dev.open(_node.node)) {
        qCWarning(lcPd) << "compx:" << _node.node << _dev.errorString();
        Q_EMIT gone();
        return;
    }
    recall();
    _notifier = std::make_unique<QSocketNotifier>(_dev.fd(), QSocketNotifier::Read);
    connect(_notifier.get(), &QSocketNotifier::activated, this, [this] {
        while (auto r = _dev.read(0))
            handlePush(*r);
        if (_pushPending)
            QTimer::singleShot(30, this, &CompxMouse::poll);
    });
    _timer.setInterval(PollIntervalMs);
    connect(&_timer, &QTimer::timeout, this, &CompxMouse::poll);
    _timer.start();
    poll();
}

void CompxMouse::handlePush(const QByteArray &report)
{
    if (report.size() > 1 && uint8_t(report[0]) == ReportId && uint8_t(report[1]) == StatusPush)
        _pushPending = true;
}

void CompxMouse::drain()
{
    while (auto r = _dev.read(0))
        handlePush(*r);
}

std::optional<QByteArray> CompxMouse::transact(uint8_t command, uint16_t address, uint8_t length, const QByteArray &data)
{
    if (_notifier)
        _notifier->setEnabled(false);
    std::optional<QByteArray> result;
    for (int attempt = 0; attempt < 2 && !result; ++attempt) {
        drain();
        if (!_dev.write(request(command, address, length, data)))
            break;
        QElapsedTimer t;
        t.start();
        while (t.elapsed() < ReplyTimeoutMs) {
            const auto r = _dev.read(int(ReplyTimeoutMs - t.elapsed()));
            if (!r)
                break;
            if (r->size() < PayloadSize + 1 || uint8_t(r->at(0)) != ReportId)
                continue;
            if (uint8_t(r->at(1)) == StatusPush) {
                _pushPending = true;
                continue;
            }
            if (uint8_t(r->at(1)) != command || !checksumValid(*r))
                continue;
            result = r->mid(1, PayloadSize);
            break;
        }
    }
    if (_notifier)
        _notifier->setEnabled(true);
    return result;
}

std::optional<QByteArray> CompxMouse::readEeprom(uint16_t address, uint8_t length)
{
    QByteArray out;
    while (length > 0) {
        const uint8_t n = std::min<uint8_t>(length, MaxData);
        const auto r = transact(GetEeprom, address, n);
        if (!r || uint8_t(r->at(1)) != 0)
            return std::nullopt;
        out += r->mid(5, n);
        address = uint16_t(address + n);
        length = uint8_t(length - n);
    }
    return out;
}

bool CompxMouse::writeEeprom(uint16_t address, const QByteArray &data)
{
    for (int off = 0; off < data.size(); off += MaxData) {
        const QByteArray chunk = data.mid(off, MaxData);
        const auto r = transact(SetEeprom, uint16_t(address + off), uint8_t(chunk.size()), chunk);
        if (!r || uint8_t(r->at(1)) != 0) {
            qCWarning(lcPd) << "compx: write failed at" << Qt::hex << address + off;
            return false;
        }
        QThread::msleep(10);
    }
    return true;
}

void CompxMouse::readBattery()
{
    _sinceBattery.start();
    if (const auto r = transact(Battery)) {
        _battery = std::min<int>(uint8_t(r->at(5)), 100);
        _charging = uint8_t(r->at(6)) != 0;
        _millivolts = (uint8_t(r->at(7)) << 8) | uint8_t(r->at(8));
        if (_loaded)
            remember();
    }
}

void CompxMouse::readBlock0()
{
    if (const auto b = readEeprom(AddrBlock0, 10))
        _block0 = *b;
}

const CompxMouse::Model *CompxMouse::findModel() const
{
    static const Model models[] = {
        {2, 54, "MAD R+", Codec::Paw3395},       {2, 49, "MAD R", Codec::Paw3395},
        {2, 47, "MAD R Major", Codec::Paw3950},  {2, 55, "MAD R Major+", Codec::Paw3950},
        {2, 133, "MAD R", Codec::Paw3950Ultra},
    };
    for (const Model &m : models) {
        if (m.cid == _cid && m.mid == _mid)
            return &m;
    }
    return nullptr;
}

void CompxMouse::recall()
{
    QSettings s;
    s.beginGroup(QStringLiteral("compx-%1").arg(QString::fromLatin1(_node.physicalKey().toUtf8().toHex())));
    _cid = uint8_t(s.value(QStringLiteral("cid"), 0).toUInt());
    _mid = uint8_t(s.value(QStringLiteral("mid"), 0).toUInt());
    _firmware = s.value(QStringLiteral("firmware")).toString();
    _battery = s.value(QStringLiteral("battery"), -1).toInt();
    _model = findModel();
}

void CompxMouse::remember()
{
    QSettings s;
    s.beginGroup(QStringLiteral("compx-%1").arg(QString::fromLatin1(_node.physicalKey().toUtf8().toHex())));
    s.setValue(QStringLiteral("cid"), _cid);
    s.setValue(QStringLiteral("mid"), _mid);
    s.setValue(QStringLiteral("firmware"), _firmware);
    s.setValue(QStringLiteral("battery"), _battery);
}

bool CompxMouse::readAll()
{
    const auto id = transact(MouseCidMid);
    if (!id)
        return false;
    _cid = uint8_t(id->at(5));
    _mid = uint8_t(id->at(6));
    _model = findModel();
    if (const auto v = transact(MouseVersion))
        _firmware = QStringLiteral("%1.%2").arg(uint8_t(v->at(5)), 0, 16).arg(hex(uint8_t(v->at(6))));
    readBattery();

    const auto block0 = readEeprom(AddrBlock0, 10);
    const auto dpi = readEeprom(AddrDpi, 32);
    if (!block0 || !dpi)
        return false;
    _block0 = *block0;
    _dpi = *dpi;
    _lod = readEeprom(AddrLod, 2).value_or(QByteArray());
    _func = readEeprom(AddrFunction, 10).value_or(QByteArray());
    _sensor = readEeprom(AddrSensor, 6).value_or(QByteArray());
    _angle = readEeprom(AddrAngle, 4).value_or(QByteArray());
    _buttons = readEeprom(AddrButtons, 24).value_or(QByteArray());
    _ledEffect = readEeprom(AddrLedEffect, 8).value_or(QByteArray());
    _ledColors = readEeprom(AddrLedColors, 32).value_or(QByteArray());
    if (const auto far = transact(GetFarDistance))
        _farMode = uint8_t(far->at(5));
    remember();
    return true;
}

void CompxMouse::poll()
{
    const auto online = transact(MouseOnline);
    if (!online) {
        if (++_failures >= 5) {
            _timer.stop();
            Q_EMIT gone();
        }
        return;
    }
    _failures = 0;
    const bool now = uint8_t(online->at(5)) == 1;
    if (now && (!_online || !_loaded)) {
        _loaded = readAll();
        _pushPending = false;
    } else if (now && _pushPending) {
        _pushPending = false;
        readBlock0();
        readBattery();
    } else if (now && _sinceBattery.isValid() && _sinceBattery.elapsed() > BatteryIntervalMs) {
        readBattery();
    }
    if (!now)
        _loaded = false;
    _online = now;
    render();
}

QJsonObject CompxMouse::entryBase() const
{
    QJsonObject e;
    e[QStringLiteral("id")] = QStringLiteral("compx:") + _node.physicalKey();
    e[QStringLiteral("kind")] = QStringLiteral("mouse");
    e[QStringLiteral("vendor")] = _model ? QStringLiteral("MADLIONS") : QStringLiteral("ATK");
    e[QStringLiteral("name")] = _model ? QString::fromLatin1(_model->name)
                                       : (_cid ? QStringLiteral("Compx mouse %1:%2").arg(_cid).arg(_mid)
                                               : _node.name);
    e[QStringLiteral("transport")] = QStringLiteral("dongle");
    e[QStringLiteral("online")] = _online;
    e[QStringLiteral("untested")] = _cid != 0 && (!_model || _model->mid != 54);
    if (_battery >= 0) {
        e[QStringLiteral("battery")] = _battery;
        e[QStringLiteral("charging")] = _charging;
        if (_millivolts > 0)
            e[QStringLiteral("voltage")] = _millivolts;
    }
    if (!_firmware.isEmpty())
        e[QStringLiteral("firmware")] = _firmware;
    if (!_error.isEmpty())
        e[QStringLiteral("error")] = _error;
    return e;
}

void CompxMouse::render()
{
    QJsonObject e = entryBase();
    if (!_online || !_loaded || _block0.size() < 6 || _dpi.size() < 32) {
        publish({e});
        return;
    }
    const Codec codec = _model ? _model->codec : Codec::Paw3950Ultra;
    QJsonArray controls;

    const int count = std::clamp<int>(unpair(_block0, 2).value_or(1), 1, MaxStages);
    const int active = std::clamp<int>(unpair(_block0, 4).value_or(0), 0, count - 1);
    QJsonArray stages;
    for (int i = 0; i < count; ++i)
        stages.append(decodeDpiX(reinterpret_cast<const uint8_t *>(_dpi.constData()) + i * 4, codec));
    QJsonObject dpi{{QStringLiteral("type"), QStringLiteral("dpi")},
                    {QStringLiteral("key"), QStringLiteral("dpi")},
                    {QStringLiteral("section"), QStringLiteral("performance")},
                    {QStringLiteral("label"), QStringLiteral("DPI")},
                    {QStringLiteral("stages"), stages},
                    {QStringLiteral("active"), active},
                    {QStringLiteral("min"), dpiMin(codec)},
                    {QStringLiteral("max"), dpiMax(codec)},
                    {QStringLiteral("step"), 50},
                    {QStringLiteral("maxStages"), MaxStages}};
    if (_ledColors.size() >= count * 4) {
        QJsonArray colors;
        for (int i = 0; i < count; ++i) {
            const auto *c = reinterpret_cast<const uint8_t *>(_ledColors.constData()) + i * 4;
            colors.append(QStringLiteral("#%1%2%3").arg(hex(c[0]), hex(c[1]), hex(c[2])));
        }
        dpi[QStringLiteral("colors")] = colors;
    }
    controls.append(dpi);

    if (const auto rate = unpair(_block0, 0)) {
        QJsonArray options;
        for (int hz : {125, 250, 500, 1000, 2000, 4000, 8000})
            options.append(option(hz));
        controls.append(choice(QStringLiteral("pollingRate"), QStringLiteral("performance"), QStringLiteral("Polling rate"),
                               pollingRateFromCode(*rate), options, QStringLiteral("Hz")));
    }
    if (const auto lod = unpair(_lod, 0)) {
        QJsonArray options;
        if (codec != Codec::Paw3395)
            options.append(QJsonObject{{QStringLiteral("value"), 3}, {QStringLiteral("number"), 0.7}});
        options.append(QJsonObject{{QStringLiteral("value"), 1}, {QStringLiteral("number"), 1}});
        options.append(QJsonObject{{QStringLiteral("value"), 2}, {QStringLiteral("number"), 2}});
        controls.append(choice(QStringLiteral("liftOff"), QStringLiteral("sensor"), QStringLiteral("Lift-off distance"),
                               *lod, options, QStringLiteral("mm")));
    }
    if (const auto mode = unpair(_sensor, 4)) {
        QJsonArray options{option(0, QStringLiteral("Basic")), option(1, QStringLiteral("Competitive"))};
        if (codec != Codec::Paw3395 || *mode == 2)
            options.append(option(2, QStringLiteral("Competitive MAX")));
        controls.append(choice(QStringLiteral("sensorMode"), QStringLiteral("sensor"), QStringLiteral("Sensor mode"), *mode,
                               options));
    }
    if (const auto v = unpair(_func, 2))
        controls.append(toggle(QStringLiteral("motionSync"), QStringLiteral("sensor"), QStringLiteral("Motion sync"), *v));
    if (const auto v = unpair(_func, 6))
        controls.append(toggle(QStringLiteral("angleSnapping"), QStringLiteral("sensor"), QStringLiteral("Angle snapping"), *v));
    if (const auto v = unpair(_func, 8))
        controls.append(toggle(QStringLiteral("rippleControl"), QStringLiteral("sensor"), QStringLiteral("Ripple control"), *v));
    if (const auto v = unpair(_angle, 0)) {
        const int deg = *v > 127 ? int(*v) - 256 : int(*v);
        controls.append(slider(QStringLiteral("sensorAngle"), QStringLiteral("sensor"), QStringLiteral("Sensor angle"), deg,
                               -30, 30, 1, QStringLiteral("deg")));
    }
    if (const auto v = unpair(_func, 0)) {
        QJsonArray options;
        for (int ms : {0, 1, 2, 4, 8, 15, 20})
            options.append(option(ms));
        controls.append(choice(QStringLiteral("debounce"), QStringLiteral("buttons"), QStringLiteral("Debounce time"), *v,
                               options, QStringLiteral("ms")));
    }
    for (int i = 0; i < ButtonCount; ++i) {
        const QString current = buttonValue(_buttons, i);
        if (current.isEmpty())
            break;
        QJsonArray options;
        bool known = false;
        for (const ButtonAction &a : ButtonActions) {
            if (i == 0 && QLatin1String(a.value) == QLatin1String("0:0:0"))
                continue;
            options.append(option(QString::fromLatin1(a.value), QString::fromLatin1(a.label)));
            known = known || current == QLatin1String(a.value);
        }
        if (!known)
            options.append(option(current, QStringLiteral("Custom")));
        controls.append(choice(QStringLiteral("button.%1").arg(i), QStringLiteral("buttons"),
                               QString::fromLatin1(ButtonLabels[i]), current, options));
    }
    if (const auto v = unpair(_func, 4)) {
        QJsonArray options;
        for (int tenths : {3, 6, 12, 18, 30, 120, 150, 180}) {
            QJsonObject o{{QStringLiteral("value"), tenths}};
            if (tenths < 6) {
                o[QStringLiteral("number")] = tenths * 10;
                o[QStringLiteral("unit")] = QStringLiteral("s");
            } else {
                o[QStringLiteral("number")] = tenths / 6;
                o[QStringLiteral("unit")] = QStringLiteral("min");
            }
            options.append(o);
        }
        controls.append(choice(QStringLiteral("sleep"), QStringLiteral("power"), QStringLiteral("Sleep after"), *v, options));
    }
    if (_farMode >= 0)
        controls.append(toggle(QStringLiteral("farDistance"), QStringLiteral("power"), QStringLiteral("Long-distance mode"),
                               _farMode != 0));
    if (const auto enabled = unpair(_ledEffect, 6)) {
        controls.append(toggle(QStringLiteral("ledEnabled"), QStringLiteral("lighting"), QStringLiteral("DPI indicator"),
                               *enabled));
        if (const auto mode = unpair(_ledEffect, 0))
            controls.append(choice(QStringLiteral("ledMode"), QStringLiteral("lighting"), QStringLiteral("Effect"), *mode,
                                   {option(1, QStringLiteral("Steady")), option(2, QStringLiteral("Breathing"))}));
        if (const auto b = unpair(_ledEffect, 2))
            controls.append(choice(QStringLiteral("ledBrightness"), QStringLiteral("lighting"), QStringLiteral("Brightness"), *b,
                                   {option(16, QStringLiteral("Low")), option(128, QStringLiteral("Medium")),
                                    option(255, QStringLiteral("High"))}));
        if (const auto s = unpair(_ledEffect, 4))
            controls.append(choice(QStringLiteral("ledSpeed"), QStringLiteral("lighting"), QStringLiteral("Speed"), *s,
                                   {option(1, QStringLiteral("Slow")), option(3, QStringLiteral("Medium")),
                                    option(5, QStringLiteral("Fast"))}));
    }
    e[QStringLiteral("controls")] = controls;
    publish({e});
}

void CompxMouse::apply(const QString &, const QJsonObject &change)
{
    if (!_online || !_loaded) {
        _error = QStringLiteral("Mouse is asleep");
        render();
        return;
    }
    _error = applyChange(change);
    if (!_error.isEmpty())
        qCWarning(lcPd) << "compx apply:" << _error << QJsonDocument(change).toJson(QJsonDocument::Compact);
    _loaded = readAll();
    render();
}

QString CompxMouse::applyChange(const QJsonObject &change)
{
    const Codec codec = _model ? _model->codec : Codec::Paw3950Ultra;
    auto failed = [] { return QStringLiteral("Write failed"); };

    if (change.contains(QStringLiteral("dpi"))) {
        const QJsonObject d = change.value(QStringLiteral("dpi")).toObject();
        const QJsonArray stages = d.value(QStringLiteral("stages")).toArray();
        if (stages.isEmpty() || stages.size() > MaxStages)
            return QStringLiteral("Invalid DPI stages");
        QByteArray raw = _dpi;
        raw.resize(MaxStages * 4, '\0');
        for (int i = 0; i < stages.size(); ++i) {
            int v = stages[i].toInt();
            v = std::clamp(v, dpiMin(codec), dpiMax(codec));
            const int step = dpiStep(codec, v);
            v = (v + step / 2) / step * step;
            const auto enc = encodeDpi(v, v, codec);
            for (int k = 0; k < 4; ++k)
                raw[i * 4 + k] = char(enc[size_t(k)]);
        }
        if (raw != _dpi && !writeEeprom(AddrDpi, raw))
            return failed();
        QByteArray b0 = _block0;
        setPair(b0, 2, uint8_t(stages.size()));
        const int active = std::clamp(d.value(QStringLiteral("active")).toInt(unpair(_block0, 4).value_or(0)), 0,
                                      int(stages.size()) - 1);
        setPair(b0, 4, uint8_t(active));
        if (b0 != _block0 && !writeEeprom(AddrBlock0, b0))
            return failed();
    }
    if (change.contains(QStringLiteral("dpiActive"))) {
        const int count = unpair(_block0, 2).value_or(1);
        QByteArray b0 = _block0;
        setPair(b0, 4, uint8_t(std::clamp(change.value(QStringLiteral("dpiActive")).toInt(), 0, count - 1)));
        if (!writeEeprom(AddrBlock0, b0))
            return failed();
    }
    if (change.contains(QStringLiteral("pollingRate"))) {
        const uint8_t code = pollingRateCode(change.value(QStringLiteral("pollingRate")).toInt());
        if (!code)
            return QStringLiteral("Unsupported polling rate");
        QByteArray b0 = _block0;
        setPair(b0, 0, code);
        if (!writeEeprom(AddrBlock0, b0))
            return failed();
    }
    if (change.contains(QStringLiteral("liftOff"))) {
        const int v = change.value(QStringLiteral("liftOff")).toInt();
        if (v < 1 || v > 3 || (v == 3 && codec == Codec::Paw3395))
            return QStringLiteral("Unsupported lift-off distance");
        if (!writeEeprom(AddrLod, pair(uint8_t(v))))
            return failed();
    }

    QByteArray func = _func;
    QByteArray sensor = _sensor;
    auto setFunc = [&](const char *key, int offset, int min, int max) -> bool {
        const QString k = QString::fromLatin1(key);
        if (!change.contains(k))
            return true;
        const QJsonValue jv = change.value(k);
        const int v = jv.isBool() ? int(jv.toBool()) : jv.toInt();
        if (v < min || v > max)
            return false;
        setPair(func, offset, uint8_t(v));
        return true;
    };
    if (!setFunc("debounce", 0, 0, 20) || !setFunc("motionSync", 2, 0, 1) || !setFunc("angleSnapping", 6, 0, 1)
        || !setFunc("rippleControl", 8, 0, 1) || !setFunc("sleep", 4, 3, 180))
        return QStringLiteral("Invalid value");
    if (change.contains(QStringLiteral("sleep")) && sensor.size() >= 4) {
        setPair(sensor, 0, 1);
        setPair(sensor, 2, uint8_t(change.value(QStringLiteral("sleep")).toInt()));
    }
    if (change.contains(QStringLiteral("sensorMode")) && sensor.size() >= 6) {
        const int v = change.value(QStringLiteral("sensorMode")).toInt();
        if (v < 0 || v > 2)
            return QStringLiteral("Invalid value");
        setPair(sensor, 4, uint8_t(v));
    }
    if (func != _func && !writeEeprom(AddrFunction, func))
        return failed();
    if (sensor != _sensor && !writeEeprom(AddrSensor, sensor))
        return failed();

    if (change.contains(QStringLiteral("sensorAngle"))) {
        const int deg = std::clamp(change.value(QStringLiteral("sensorAngle")).toInt(), -30, 30);
        QByteArray a = pair(uint8_t(int8_t(deg)));
        a += pair(deg != 0 ? 1 : 0);
        if (!writeEeprom(AddrAngle, a))
            return failed();
    }
    if (change.contains(QStringLiteral("farDistance"))) {
        const auto r = transact(SetFarDistance, 0, MaxData,
                                QByteArray(1, char(change.value(QStringLiteral("farDistance")).toBool() ? 1 : 0)));
        if (!r || uint8_t(r->at(1)) != 0)
            return failed();
    }

    QByteArray buttons = _buttons;
    for (int i = 0; i < ButtonCount; ++i) {
        const QString k = QStringLiteral("button.%1").arg(i);
        if (!change.contains(k))
            continue;
        const auto b = parseButton(change.value(k).toString());
        if (!b || buttons.size() < (i + 1) * 4)
            return QStringLiteral("Invalid button action");
        buttons[i * 4] = char((*b)[0]);
        buttons[i * 4 + 1] = char((*b)[1]);
        buttons[i * 4 + 2] = char((*b)[2]);
        buttons[i * 4 + 3] = char(uint8_t(0x55 - ((*b)[0] + (*b)[1] + (*b)[2])));
    }
    if (buttons != _buttons) {
        bool leftClick = false;
        for (int i = 0; i < ButtonCount; ++i)
            leftClick = leftClick || buttonValue(buttons, i) == QLatin1String("1:1:0");
        if (!leftClick)
            return QStringLiteral("At least one button must keep the left click");
        for (int i = 0; i < ButtonCount; ++i) {
            if (buttons.mid(i * 4, 4) != _buttons.mid(i * 4, 4) && !writeEeprom(uint16_t(AddrButtons + i * 4), buttons.mid(i * 4, 4)))
                return failed();
        }
    }

    QByteArray led = _ledEffect;
    auto setLed = [&](const char *key, int offset) {
        const QString k = QString::fromLatin1(key);
        if (!change.contains(k))
            return;
        const QJsonValue jv = change.value(k);
        setPair(led, offset, uint8_t(jv.isBool() ? int(jv.toBool()) : jv.toInt()));
    };
    setLed("ledMode", 0);
    setLed("ledBrightness", 2);
    setLed("ledSpeed", 4);
    setLed("ledEnabled", 6);
    if (led != _ledEffect && !writeEeprom(AddrLedEffect, led))
        return failed();
    return {};
}
