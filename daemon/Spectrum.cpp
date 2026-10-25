#include "Spectrum.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <vector>

namespace spectrum {

namespace {

void fft(std::vector<std::complex<float>> &a)
{
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j)
            std::swap(a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1) {
        const float angle = -2.0f * float(M_PI) / float(len);
        const std::complex<float> step(std::cos(angle), std::sin(angle));
        for (size_t i = 0; i < n; i += len) {
            std::complex<float> w(1.0f, 0.0f);
            for (size_t k = 0; k < len / 2; ++k) {
                const std::complex<float> u = a[i + k];
                const std::complex<float> v = a[i + k + len / 2] * w;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
                w *= step;
            }
        }
    }
}

void hsv(float h, float s, float v, uint8_t &r, uint8_t &g, uint8_t &b)
{
    h = std::fmod(std::max(h, 0.0f), 1.0f) * 6.0f;
    const int i = int(h);
    const float f = h - float(i);
    const float p = v * (1 - s), q = v * (1 - s * f), t = v * (1 - s * (1 - f));
    float rf, gf, bf;
    switch (i) {
    case 0: rf = v, gf = t, bf = p; break;
    case 1: rf = q, gf = v, bf = p; break;
    case 2: rf = p, gf = v, bf = t; break;
    case 3: rf = p, gf = q, bf = v; break;
    case 4: rf = t, gf = p, bf = v; break;
    default: rf = v, gf = p, bf = q; break;
    }
    r = uint8_t(std::clamp(rf, 0.0f, 1.0f) * 255.0f);
    g = uint8_t(std::clamp(gf, 0.0f, 1.0f) * 255.0f);
    b = uint8_t(std::clamp(bf, 0.0f, 1.0f) * 255.0f);
}

}

QVector<float> bandMagnitudes(const float *samples, int count, int sampleRate, int bands, float minHz, float maxHz)
{
    size_t n = 1;
    while (n * 2 <= size_t(count))
        n *= 2;
    std::vector<std::complex<float>> buf(n);
    const float *start = samples + (count - int(n));
    for (size_t i = 0; i < n; ++i) {
        const float window = 0.5f - 0.5f * std::cos(2.0f * float(M_PI) * float(i) / float(n - 1));
        buf[i] = std::complex<float>(start[i] * window, 0.0f);
    }
    fft(buf);
    QVector<float> out(bands, 0.0f);
    const float binHz = float(sampleRate) / float(n);
    for (int b = 0; b < bands; ++b) {
        const float lo = minHz * std::pow(maxHz / minHz, float(b) / float(bands));
        const float hi = minHz * std::pow(maxHz / minHz, float(b + 1) / float(bands));
        const int first = std::max(1, int(lo / binHz));
        const int last = std::max(first, std::min(int(n / 2) - 1, int(hi / binHz)));
        float peak = 0.0f;
        for (int k = first; k <= last; ++k)
            peak = std::max(peak, std::abs(buf[size_t(k)]) / float(n));
        out[b] = peak;
    }
    return out;
}

QVector<float> Analyzer::process(const QVector<float> &magnitudes)
{
    if (_levels.size() != magnitudes.size())
        _levels = QVector<float>(magnitudes.size(), 0.0f);
    const float loudest = magnitudes.isEmpty() ? 0.0f : *std::max_element(magnitudes.begin(), magnitudes.end());
    _reference = std::max({loudest, _reference * 0.995f, 1e-3f});
    for (int i = 0; i < magnitudes.size(); ++i) {
        const float target = std::clamp(magnitudes[i] / _reference, 0.0f, 1.0f);
        _levels[i] = target > _levels[i] ? target : _levels[i] * 0.85f + target * 0.15f;
    }
    return _levels;
}

QByteArray frame(const QVector<float> &levels, Style style, float brightness, int tick, float glow)
{
    QByteArray rgb(Slots * 3, '\0');
    brightness = std::clamp(brightness, 0.0f, 1.0f);
    float average = 0.0f;
    for (float l : levels)
        average += l;
    average = levels.isEmpty() ? 0.0f : average / float(levels.size());
    for (int col = 0; col < Columns; ++col) {
        const float level = col < levels.size() ? levels[col] : 0.0f;
        const float height = level * float(Rows);
        for (int row = 0; row < Rows; ++row) {
            const int fromBottom = Rows - 1 - row;
            uint8_t r = 0, g = 0, b = 0;
            if (style == Style::Pulse) {
                hsv(float(tick % 600) / 600.0f + float(col) / float(Columns * 3), 1.0f,
                    std::max(average, glow) * brightness, r, g, b);
            } else {
                const float fill = std::clamp(height - float(fromBottom), 0.0f, 1.0f);
                const float hue = style == Style::Rainbow ? float(col) / float(Columns)
                                                          : 0.33f * (1.0f - float(fromBottom) / float(Rows - 1));
                hsv(hue, 1.0f, std::max(fill, glow) * brightness, r, g, b);
            }
            const int slot = col * Rows + row;
            rgb[slot * 3] = char(r);
            rgb[slot * 3 + 1] = char(g);
            rgb[slot * 3 + 2] = char(b);
        }
    }
    return rgb;
}

}
