#include "RkKeyboard.h"

#include <QColor>
#include <QSettings>
#include <QThread>

#include "Log.h"

using namespace rk;

bool RkKeyboard::matches(const HidrawInfo &info)
{
    return info.vendor == 0x258A && info.product == 0x0089 && info.bus == 0x03 && info.hasReportId(ReportId);
}

void RkKeyboard::start()
{
    if (!_dev.open(_node.node)) {
        qCWarning(lcPd) << "rk:" << _node.node << _dev.errorString();
        Q_EMIT gone();
        return;
    }
    load();
    render();
}

void RkKeyboard::load()
{
    QSettings s;
    s.beginGroup(QStringLiteral("rk-%1").arg(_node.product, 4, 16, QLatin1Char('0')));
    _known = s.contains(QStringLiteral("mode"));
    _lighting.mode = s.value(QStringLiteral("mode"), _lighting.mode).toInt();
    _lighting.speed = s.value(QStringLiteral("speed"), _lighting.speed).toInt();
    _lighting.brightness = s.value(QStringLiteral("brightness"), _lighting.brightness).toInt();
    const QColor c(s.value(QStringLiteral("color"), QStringLiteral("#ffffff")).toString());
    _lighting.red = uint8_t(c.red());
    _lighting.green = uint8_t(c.green());
    _lighting.blue = uint8_t(c.blue());
    _lighting.random = s.value(QStringLiteral("random"), false).toBool();
    _lighting.sleep = s.value(QStringLiteral("sleep"), _lighting.sleep).toInt();
}

void RkKeyboard::save()
{
    QSettings s;
    s.beginGroup(QStringLiteral("rk-%1").arg(_node.product, 4, 16, QLatin1Char('0')));
    s.setValue(QStringLiteral("mode"), _lighting.mode);
    s.setValue(QStringLiteral("speed"), _lighting.speed);
    s.setValue(QStringLiteral("brightness"), _lighting.brightness);
    s.setValue(QStringLiteral("color"), QColor(_lighting.red, _lighting.green, _lighting.blue).name());
    s.setValue(QStringLiteral("random"), _lighting.random);
    s.setValue(QStringLiteral("sleep"), _lighting.sleep);
    _known = true;
}

bool RkKeyboard::send(const QByteArray &payload)
{
    for (const QByteArray &report : frame(payload)) {
        if (!_dev.setFeature(report)) {
            qCWarning(lcPd) << "rk:" << _dev.errorString();
            return false;
        }
        QThread::msleep(8);
    }
    QThread::msleep(50);
    return true;
}

void RkKeyboard::render()
{
    QJsonObject e;
    e[QStringLiteral("id")] = QStringLiteral("rk:") + _node.physicalKey();
    e[QStringLiteral("kind")] = QStringLiteral("keyboard");
    e[QStringLiteral("vendor")] = QStringLiteral("Royal Kludge");
    e[QStringLiteral("name")] = QStringLiteral("RK96 RGB");
    e[QStringLiteral("transport")] = QStringLiteral("usb");
    e[QStringLiteral("online")] = true;
    e[QStringLiteral("untested")] = false;
    e[QStringLiteral("writeOnly")] = true;
    e[QStringLiteral("settingsKnown")] = _known;
    if (!_error.isEmpty())
        e[QStringLiteral("error")] = _error;

    QJsonArray modes;
    for (int m = 1; m <= ModeCount; ++m)
        modes.append(option(m, QString::fromLatin1(modeInfo(m).name)));
    const ModeInfo &info = modeInfo(_lighting.mode);
    QJsonArray controls;
    controls.append(choice(QStringLiteral("mode"), QStringLiteral("lighting"), QStringLiteral("Effect"), _lighting.mode, modes));
    controls.append(slider(QStringLiteral("brightness"), QStringLiteral("lighting"), QStringLiteral("Brightness"),
                           _lighting.brightness, 0, 5, 1));
    if (info.speed)
        controls.append(slider(QStringLiteral("speed"), QStringLiteral("lighting"), QStringLiteral("Speed"), _lighting.speed,
                               1, 5, 1));
    if (info.random)
        controls.append(toggle(QStringLiteral("random"), QStringLiteral("lighting"), QStringLiteral("Rainbow colors"),
                               _lighting.random));
    if (info.color && !_lighting.random)
        controls.append(color(QStringLiteral("color"), QStringLiteral("lighting"), QStringLiteral("Color"),
                              QColor(_lighting.red, _lighting.green, _lighting.blue).name()));
    QJsonArray sleep;
    const int minutes[] = {5, 10, 20, 30};
    for (int i = 0; i < 4; ++i)
        sleep.append(QJsonObject{{QStringLiteral("value"), i + 1}, {QStringLiteral("number"), minutes[i]},
                                 {QStringLiteral("unit"), QStringLiteral("min")}});
    sleep.append(option(5, QStringLiteral("Never")));
    controls.append(choice(QStringLiteral("sleep"), QStringLiteral("power"), QStringLiteral("Lighting off after"),
                           _lighting.sleep, sleep));
    e[QStringLiteral("controls")] = controls;
    publish({e});
}

void RkKeyboard::apply(const QString &, const QJsonObject &change)
{
    Lighting next = _lighting;
    if (change.contains(QStringLiteral("mode")))
        next.mode = std::clamp(change.value(QStringLiteral("mode")).toInt(), 1, ModeCount);
    if (change.contains(QStringLiteral("brightness")))
        next.brightness = std::clamp(change.value(QStringLiteral("brightness")).toInt(), 0, 5);
    if (change.contains(QStringLiteral("speed")))
        next.speed = std::clamp(change.value(QStringLiteral("speed")).toInt(), 1, 5);
    if (change.contains(QStringLiteral("random")))
        next.random = change.value(QStringLiteral("random")).toBool();
    if (change.contains(QStringLiteral("sleep")))
        next.sleep = std::clamp(change.value(QStringLiteral("sleep")).toInt(), 1, 5);
    if (change.contains(QStringLiteral("color"))) {
        const QColor c(change.value(QStringLiteral("color")).toString());
        if (c.isValid()) {
            next.red = uint8_t(c.red());
            next.green = uint8_t(c.green());
            next.blue = uint8_t(c.blue());
        }
    }
    if (send(lightingPayload(next))) {
        _lighting = next;
        _error.clear();
        save();
    } else {
        _error = QStringLiteral("Write failed");
    }
    render();
}
