#include "RkKeyboard.h"

#include <QColor>
#include <QSettings>
#include <QThread>

#include "Log.h"

using namespace rk;

namespace {

constexpr int SampleRate = 48000;
constexpr int WindowSamples = 2048;
constexpr int FrameIntervalMs = 50;
constexpr float IdleGlow = 0.08f;

}

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
    connect(&_frameTimer, &QTimer::timeout, this, &RkKeyboard::musicFrame);
    _frameTimer.setInterval(FrameIntervalMs);
    if (_music)
        startMusic();
    render();
}

void RkKeyboard::startMusic()
{
    if (_audio)
        return;
    _samples = QVector<float>(WindowSamples, 0.0f);
    _audio = new QProcess(this);
    connect(_audio, &QProcess::readyReadStandardOutput, this, &RkKeyboard::readAudio);
    connect(_audio, &QProcess::finished, this, [this] {
        if (!_music)
            return;
        _error = QStringLiteral("Audio capture failed");
        qCWarning(lcPd) << "rk: pw-cat exited:" << _audio->readAllStandardError();
        stopMusic();
        render();
    });
    _audio->start(QStringLiteral("pw-cat"),
                  {QStringLiteral("--record"), QStringLiteral("--raw"), QStringLiteral("--format"), QStringLiteral("f32"),
                   QStringLiteral("--rate"), QString::number(SampleRate), QStringLiteral("--channels"), QStringLiteral("1"),
                   QStringLiteral("--latency"), QStringLiteral("20ms"), QStringLiteral("-P"),
                   QStringLiteral("{ stream.capture.sink=true node.name=peripheralsd-music media.name=\"Keyboard lighting\" application.id=org.kde.plasma-pa application.name=peripheralsd }"),
                   QStringLiteral("-")});
    _sinceAudio.start();
    _frameTimer.start();
}

void RkKeyboard::stopMusic()
{
    _frameTimer.stop();
    if (_audio) {
        _audio->disconnect(this);
        _audio->kill();
        _audio->waitForFinished(1000);
        _audio->deleteLater();
        _audio = nullptr;
    }
}

void RkKeyboard::readAudio()
{
    const QByteArray data = _audio->readAllStandardOutput();
    const int count = int(data.size() / sizeof(float));
    if (count <= 0)
        return;
    const auto *values = reinterpret_cast<const float *>(data.constData());
    if (count >= WindowSamples) {
        _samples = QVector<float>(values + count - WindowSamples, values + count);
    } else {
        _samples.remove(0, count);
        _samples.append(QVector<float>(values, values + count));
    }
    _sinceAudio.start();
}

void RkKeyboard::musicFrame()
{
    if (_sinceAudio.elapsed() > 200)
        _samples.fill(0.0f);
    const auto magnitudes = spectrum::bandMagnitudes(_samples.constData(), int(_samples.size()), SampleRate,
                                                     spectrum::Columns, 40.0f, 16000.0f);
    const auto levels = _analyzer.process(magnitudes);
    const auto style = static_cast<spectrum::Style>(std::clamp(_musicStyle, 0, 2));
    const QByteArray rgb = spectrum::frame(levels, style, float(_lighting.brightness) / 5.0f, _tick++, IdleGlow);
    QByteArray payload;
    payload.append(char(0x06));
    payload.append(char(0x00));
    payload.append(rgb);
    if (!send(payload, 0, 0)) {
        _error = QStringLiteral("Write failed");
        stopMusic();
        render();
    }
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
    _lighting.speedLevels = s.value(QStringLiteral("speedLevels"), 5).toInt() == 9 ? 9 : 5;
    _music = s.value(QStringLiteral("music"), false).toBool();
    _musicStyle = s.value(QStringLiteral("musicStyle"), 0).toInt();
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
    s.setValue(QStringLiteral("music"), _music);
    s.setValue(QStringLiteral("musicStyle"), _musicStyle);
    _known = true;
}

bool RkKeyboard::send(const QByteArray &payload, int gapMs, int tailMs)
{
    for (const QByteArray &report : rk::frame(payload)) {
        if (!_dev.setFeature(report)) {
            qCWarning(lcPd) << "rk:" << _dev.errorString();
            return false;
        }
        if (gapMs > 0)
            QThread::msleep(gapMs);
    }
    if (tailMs > 0)
        QThread::msleep(tailMs);
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
                               1, _lighting.speedLevels, 1));
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
    controls.append(choice(QStringLiteral("sleep"), QStringLiteral("lighting"), QStringLiteral("Lighting off after"),
                           _lighting.sleep, sleep));
    controls.append(toggle(QStringLiteral("music"), QStringLiteral("music"), QStringLiteral("Music visualization"), _music));
    if (_music)
        controls.append(choice(QStringLiteral("musicStyle"), QStringLiteral("music"), QStringLiteral("Style"), _musicStyle,
                               {option(0, QStringLiteral("Spectrum")), option(1, QStringLiteral("Rainbow")),
                                option(2, QStringLiteral("Pulse"))}));
    e[QStringLiteral("controls")] = controls;
    publish({e});
}

void RkKeyboard::apply(const QString &, const QJsonObject &input)
{
    QJsonObject change = input;
    if (change.contains(QStringLiteral("musicStyle"))) {
        _musicStyle = std::clamp(change.take(QStringLiteral("musicStyle")).toInt(), 0, 2);
        save();
    }
    if (change.contains(QStringLiteral("music"))) {
        _music = change.take(QStringLiteral("music")).toBool();
        _error.clear();
        save();
        if (_music)
            startMusic();
        else
            stopMusic();
    }
    if (change.isEmpty()) {
        render();
        return;
    }
    Lighting next = _lighting;
    if (change.contains(QStringLiteral("mode")))
        next.mode = std::clamp(change.value(QStringLiteral("mode")).toInt(), 1, ModeCount);
    if (change.contains(QStringLiteral("brightness")))
        next.brightness = std::clamp(change.value(QStringLiteral("brightness")).toInt(), 0, 5);
    if (change.contains(QStringLiteral("speed")))
        next.speed = std::clamp(change.value(QStringLiteral("speed")).toInt(), 1, _lighting.speedLevels);
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
