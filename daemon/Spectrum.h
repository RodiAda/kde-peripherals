#pragma once

#include <QByteArray>
#include <QVector>

namespace spectrum {

constexpr int Columns = 21;
constexpr int Rows = 6;
constexpr int Slots = Columns * Rows;

enum class Style { Bars, Rainbow, Pulse };

QVector<float> bandMagnitudes(const float *samples, int count, int sampleRate, int bands, float minHz, float maxHz);

class Analyzer
{
public:
    QVector<float> process(const QVector<float> &magnitudes);

private:
    QVector<float> _levels;
    float _reference = 1e-3f;
};

QByteArray frame(const QVector<float> &levels, Style style, float brightness, int tick, float glow = 0.0f,
                 bool inverted = false);

enum class Scene { Wave, Ripples, Sparkle, Pulse };

struct Palette {
    bool rainbow = true;
    float red = 1.0f;
    float green = 1.0f;
    float blue = 1.0f;
};

Scene sceneForMode(int mode);

struct Reaction {
    float depth = 1.0f;
    bool tempo = true;
    bool inverted = false;
};

class Envelope
{
public:
    float update(float x);

private:
    float _peak = 0.02f;
    float _value = 0.0f;
};

class SceneRenderer
{
public:
    QByteArray render(const QVector<float> &levels, Scene scene, const Palette &palette, float brightness, float dt,
                      const Reaction &reaction = {});

private:
    struct Ripple {
        int col;
        int row;
        float age;
        float r, g, b;
    };

    float random01();
    void nextColour(const Palette &palette, float &r, float &g, float &b);

    Envelope _loudEnvelope;
    Envelope _bassEnvelope;
    Envelope _trebleEnvelope;
    float _phase = 0.0f;
    float _hue = 0.0f;
    float _bassAverage = 0.0f;
    float _sinceBeat = 1.0f;
    uint32_t _seed = 0x12345678u;
    QVector<Ripple> _ripples;
    QVector<float> _spark = QVector<float>(Slots, 0.0f);
    QVector<float> _sparkHue = QVector<float>(Slots, 0.0f);
};

}
