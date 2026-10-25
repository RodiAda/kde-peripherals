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

QByteArray frame(const QVector<float> &levels, Style style, float brightness, int tick, float glow = 0.0f);

}
