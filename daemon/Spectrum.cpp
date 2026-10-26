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

void hsvf(float h, float &r, float &g, float &b)
{
    uint8_t rr, gg, bb;
    hsv(h, 1.0f, 1.0f, rr, gg, bb);
    r = float(rr) / 255.0f;
    g = float(gg) / 255.0f;
    b = float(bb) / 255.0f;
}

uint8_t encode(float v, float brightness)
{
    return uint8_t(std::lround(std::pow(std::clamp(v, 0.0f, 1.0f), 2.2f) * brightness * 255.0f));
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

QByteArray frame(const QVector<float> &levels, Style style, float brightness, int tick, float glow, bool inverted)
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
                    (inverted ? 1.0f - 0.88f * average : std::max(average, glow)) * brightness, r, g, b);
            } else {
                float fill = std::clamp(height - float(fromBottom), 0.0f, 1.0f);
                if (inverted)
                    fill = 1.0f - 0.88f * fill;
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


Scene sceneForMode(int mode)
{
    switch (mode) {
    case 2:
    case 20:
    case 21:
        return Scene::Ripples;
    case 6:
        return Scene::Sparkle;
    case 8:
    case 14:
    case 17:
    case 18:
        return Scene::Pulse;
    }
    return Scene::Wave;
}

float SceneRenderer::random01()
{
    _seed = _seed * 1664525u + 1013904223u;
    return float(_seed >> 8) / float(1u << 24);
}

void SceneRenderer::nextColour(const Palette &palette, float &r, float &g, float &b)
{
    if (palette.rainbow) {
        _hue = std::fmod(_hue + 0.13f, 1.0f);
        hsvf(_hue, r, g, b);
    } else {
        r = palette.red;
        g = palette.green;
        b = palette.blue;
    }
}

float Envelope::update(float x)
{
    _peak = std::max({x, _peak * 0.996f, 0.02f});
    const float normalized = std::clamp(x / _peak, 0.0f, 1.0f);
    _value += (normalized - _value) * (normalized > _value ? 0.6f : 0.2f);
    return _value;
}

QByteArray SceneRenderer::render(const QVector<float> &levels, Scene scene, const Palette &palette, float brightness,
                                 float dt, const Reaction &reaction)
{
    const bool inverted = reaction.inverted;
    const float depth = std::clamp(reaction.depth, 0.0f, 1.0f);
    auto react = [depth](float v) { return 0.5f + (v - 0.5f) * depth; };
    auto shade = [inverted, &react](float v) { return react(inverted ? 1.0f - 0.88f * v : v); };
    QByteArray rgb(Slots * 3, '\0');
    brightness = std::clamp(brightness, 0.0f, 1.0f);
    float loud = 0.0f;
    for (float l : levels)
        loud += l;
    loud = levels.isEmpty() ? 0.0f : loud / float(levels.size());
    float bass = 0.0f;
    for (int i = 0; i < std::min<int>(4, int(levels.size())); ++i)
        bass += levels[i] / 4.0f;
    float treble = 0.0f;
    for (int i = std::max<int>(0, int(levels.size()) - 7); i < levels.size(); ++i)
        treble += levels[i] / 7.0f;
    loud = _loudEnvelope.update(loud);
    bass = _bassEnvelope.update(bass);
    treble = _trebleEnvelope.update(treble);
    const float tempo = !reaction.tempo ? 1.0f : inverted ? 1.6f - 1.3f * loud : 0.35f + 1.65f * loud;
    const float adt = dt * tempo;

    auto put = [&](int slot, float r, float g, float b) {
        rgb[slot * 3] = char(encode(r, brightness));
        rgb[slot * 3 + 1] = char(encode(g, brightness));
        rgb[slot * 3 + 2] = char(encode(b, brightness));
    };
    const float baseR = palette.rainbow ? 1.0f : palette.red;
    const float baseG = palette.rainbow ? 1.0f : palette.green;
    const float baseB = palette.rainbow ? 1.0f : palette.blue;

    switch (scene) {
    case Scene::Wave: {
        _phase = std::fmod(_phase + adt * (0.25f + 0.9f * bass), 1.0f);
        for (int col = 0; col < Columns; ++col) {
            const float level = col < levels.size() ? levels[col] : 0.0f;
            const float intensity = shade(0.25f + 0.75f * std::pow(level, 0.7f));
            float r = baseR, g = baseG, b = baseB;
            if (palette.rainbow) {
                hsvf(float(col) / float(Columns) * 0.8f + _phase, r, g, b);
            } else {
                const float wave = 0.55f + 0.45f * std::sin(2.0f * float(M_PI) * (float(col) / 10.0f - _phase * 2.0f));
                r *= wave;
                g *= wave;
                b *= wave;
            }
            for (int row = 0; row < Rows; ++row)
                put(col * Rows + row, r * intensity, g * intensity, b * intensity);
        }
        break;
    }
    case Scene::Ripples: {
        _bassAverage = _bassAverage * 0.92f + bass * 0.08f;
        _sinceBeat += dt;
        if (bass > 0.3f && bass > _bassAverage * 1.3f && _sinceBeat > 0.15f && _ripples.size() < 8) {
            _sinceBeat = 0.0f;
            Ripple rp{int(random01() * Columns) % Columns, int(random01() * Rows) % Rows, 0.0f, 1.0f, 1.0f, 1.0f};
            nextColour(palette, rp.r, rp.g, rp.b);
            _ripples.append(rp);
        }
        for (Ripple &rp : _ripples)
            rp.age += adt;
        _ripples.erase(std::remove_if(_ripples.begin(), _ripples.end(), [](const Ripple &rp) { return rp.age > 2.5f; }),
                       _ripples.end());
        for (int slot = 0; slot < Slots; ++slot) {
            const int col = slot / Rows, row = slot % Rows;
            float level = 0.0f, cr = baseR, cg = baseG, cb = baseB;
            for (const Ripple &rp : _ripples) {
                const float dist = std::hypot(float(col - rp.col), 1.1f * float(row - rp.row));
                float contrib = std::exp(-std::pow((dist - 16.0f * rp.age) / 1.3f, 2.0f));
                contrib = std::min(1.0f, 1.6f * contrib * std::exp(-rp.age / 1.1f));
                if (col == rp.col && row == rp.row)
                    contrib = std::max(contrib, std::exp(-rp.age / 0.55f));
                if (contrib > level) {
                    level = contrib;
                    cr = rp.r, cg = rp.g, cb = rp.b;
                }
            }
            const float mix = std::pow(std::min(level, 1.0f), 0.6f);
            if (inverted) {
                const float dim = react(1.0f - 0.88f * mix);
                put(slot, baseR * dim, baseG * dim, baseB * dim);
            } else {
                const float base = 0.3f + 0.15f * loud;
                put(slot, baseR * base + (cr - baseR * base) * mix, baseG * base + (cg - baseG * base) * mix,
                    baseB * base + (cb - baseB * base) * mix);
            }
        }
        break;
    }
    case Scene::Sparkle: {
        const float decay = std::exp(-adt / 0.35f);
        for (float &s : _spark)
            s *= decay;
        const int count = int((treble * 6.0f + loud * 3.0f) * std::max(tempo, 0.3f));
        for (int i = 0; i < count; ++i) {
            const int slot = int(random01() * Slots) % Slots;
            _spark[slot] = 1.0f;
            _sparkHue[slot] = random01();
        }
        for (int slot = 0; slot < Slots; ++slot) {
            float r = baseR, g = baseG, b = baseB;
            if (palette.rainbow)
                hsvf(_sparkHue[slot], r, g, b);
            if (inverted) {
                const float dim = react(1.0f - 0.88f * _spark[slot]);
                put(slot, baseR * dim, baseG * dim, baseB * dim);
                continue;
            }
            const float v = 0.18f + 0.82f * _spark[slot];
            const float base = 0.18f;
            put(slot, palette.rainbow && _spark[slot] < 0.05f ? base : r * v, palette.rainbow && _spark[slot] < 0.05f ? base : g * v,
                palette.rainbow && _spark[slot] < 0.05f ? base : b * v);
        }
        break;
    }
    case Scene::Pulse: {
        _phase = std::fmod(_phase + adt * 0.05f, 1.0f);
        const float v = react(inverted ? 1.0f - 0.88f * std::pow(loud, 0.8f) : 0.12f + 0.88f * std::pow(loud, 0.8f));
        for (int slot = 0; slot < Slots; ++slot) {
            float r = baseR, g = baseG, b = baseB;
            if (palette.rainbow)
                hsvf(_phase + float(slot / Rows) / float(Columns * 4), r, g, b);
            put(slot, r * v, g * v, b * v);
        }
        break;
    }
    }
    return rgb;
}

}
