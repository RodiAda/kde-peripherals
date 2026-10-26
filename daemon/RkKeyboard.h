#pragma once

#include <QElapsedTimer>
#include <QProcess>
#include <QTimer>

#include "Device.h"
#include "RkProtocol.h"
#include "Spectrum.h"

class RkKeyboard : public Device
{
    Q_OBJECT

public:
    using Device::Device;

    static bool matches(const HidrawInfo &info);

public Q_SLOTS:
    void start() override;
    void apply(const QString &id, const QJsonObject &change) override;

private:
    void load();
    void save();
    bool send(const QByteArray &payload, int gapMs = 8, int tailMs = 50);
    void render();
    void startMusic();
    void stopMusic();
    void readAudio();
    void musicFrame();

    Hidraw _dev;
    rk::Lighting _lighting;
    bool _known = false;
    QString _error;
    bool _music = false;
    int _musicStyle = 0;
    bool _musicInvert = false;
    int _musicDepth = 100;
    bool _musicTempo = true;
    QProcess *_audio = nullptr;
    QTimer _frameTimer{this};
    QElapsedTimer _sinceAudio;
    QVector<float> _samples;
    spectrum::Analyzer _analyzer;
    spectrum::SceneRenderer _scenes;
    int _tick = 0;
};
