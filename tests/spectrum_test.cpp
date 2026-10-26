#include <cmath>
#include <cstdio>
#include <vector>

#include "Spectrum.h"

using namespace spectrum;

static int failures = 0;

static void check(bool ok, const char *what)
{
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

static int loudestBand(float hz)
{
    std::vector<float> s(2048);
    for (size_t i = 0; i < s.size(); ++i)
        s[i] = std::sin(2.0f * float(M_PI) * hz * float(i) / 48000.0f);
    const auto bands = bandMagnitudes(s.data(), int(s.size()), 48000, Columns, 40.0f, 16000.0f);
    int best = 0;
    for (int i = 1; i < bands.size(); ++i)
        if (bands[i] > bands[best])
            best = i;
    return best;
}

int main()
{
    check(loudestBand(60.0f) <= 2, "low tone in left columns");
    check(loudestBand(10000.0f) >= 17, "high tone in right columns");
    check(loudestBand(1000.0f) > loudestBand(200.0f), "bands increase with frequency");

    std::vector<float> silence(2048, 0.0f);
    const auto quiet = bandMagnitudes(silence.data(), 2048, 48000, Columns, 40.0f, 16000.0f);
    Analyzer analyzer;
    const auto levels = analyzer.process(quiet);
    check(levels.size() == Columns && levels[0] == 0.0f, "silence gives zero levels");

    QVector<float> full(Columns, 1.0f);
    const QByteArray bars = frame(full, Style::Bars, 1.0f, 0);
    check(bars.size() == Slots * 3, "frame size");
    check(uint8_t(bars[5 * 3 + 1]) == 255 && uint8_t(bars[5 * 3]) < 20, "bottom row is green");
    check(uint8_t(bars[0]) == 255 && uint8_t(bars[1]) == 0, "top row is red");

    QVector<float> half(Columns, 0.5f);
    const QByteArray mid = frame(half, Style::Bars, 1.0f, 0);
    check(mid[0] == 0 && mid[1] == 0 && mid[2] == 0, "top row dark at half level");
    check(uint8_t(mid[5 * 3 + 1]) > 0, "bottom row lit at half level");

    const QByteArray dark = frame(full, Style::Rainbow, 0.0f, 0);
    check(dark == QByteArray(Slots * 3, '\0'), "zero brightness is dark");

    check(sceneForMode(2) == Scene::Ripples && sceneForMode(6) == Scene::Sparkle && sceneForMode(18) == Scene::Pulse
              && sceneForMode(1) == Scene::Wave,
          "scene mapping");

    Palette red;
    red.rainbow = false;
    red.green = 0.0f;
    red.blue = 0.0f;
    SceneRenderer pulse;
    const QByteArray quietPulse = pulse.render(QVector<float>(Columns, 0.0f), Scene::Pulse, red, 1.0f, 0.05f);
    const QByteArray loudPulse = pulse.render(QVector<float>(Columns, 1.0f), Scene::Pulse, red, 1.0f, 0.05f);
    check(quietPulse.size() == Slots * 3 && loudPulse.size() == Slots * 3, "scene frame size");
    check(uint8_t(loudPulse[0]) > uint8_t(quietPulse[0]) && loudPulse[1] == 0 && loudPulse[2] == 0, "pulse follows loudness in red");

    SceneRenderer dimPulse;
    const QByteArray quietDim = dimPulse.render(QVector<float>(Columns, 0.0f), Scene::Pulse, red, 1.0f, 0.05f, Reaction{1.0f, true, true});
    const QByteArray loudDim = dimPulse.render(QVector<float>(Columns, 1.0f), Scene::Pulse, red, 1.0f, 0.05f, Reaction{1.0f, true, true});
    check(uint8_t(quietDim[0]) > uint8_t(loudDim[0]), "inverted pulse dims with loudness");

    SceneRenderer quietThenLoud;
    QVector<float> soft(Columns, 0.05f);
    QByteArray softFrame;
    for (int i = 0; i < 40; ++i)
        softFrame = quietThenLoud.render(soft, Scene::Pulse, red, 1.0f, 0.05f);
    check(uint8_t(softFrame[0]) > 128, "auto gain lifts quiet music to full range");

    SceneRenderer ripples;
    QVector<float> quietLevels(Columns, 0.0f);
    for (int i = 0; i < 20; ++i)
        ripples.render(quietLevels, Scene::Ripples, red, 1.0f, 0.05f);
    const QByteArray idle = ripples.render(quietLevels, Scene::Ripples, red, 1.0f, 0.05f);
    QVector<float> beat(Columns, 0.0f);
    for (int i = 0; i < 4; ++i)
        beat[i] = 1.0f;
    const QByteArray hit = ripples.render(beat, Scene::Ripples, red, 1.0f, 0.05f);
    int brighter = 0;
    for (int slot = 0; slot < Slots; ++slot)
        brighter += uint8_t(hit[slot * 3]) > uint8_t(idle[slot * 3]) + 40 ? 1 : 0;
    check(brighter >= 1, "bass beat spawns a ripple");

    if (failures == 0)
        std::printf("spectrum: all tests passed\n");
    return failures ? 1 : 0;
}
