#include <cstdio>

#include "RkProtocol.h"

using namespace rk;

static int failures = 0;

static void check(bool ok, const char *what)
{
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

int main()
{
    Lighting white;
    white.mode = 17;
    white.speed = 3;
    white.brightness = 5;
    white.sleep = 5;
    const auto reports = frame(lightingPayload(white));
    check(reports.size() == 1 && reports[0].size() == 65, "single report");
    const QByteArray expect = QByteArray::fromHex("0a01010229110103 05ffffff0005");
    check(reports[0].left(expect.size()) == expect, "steady white bytes");
    bool zeros = true;
    for (int i = expect.size(); i < 65; ++i)
        zeros = zeros && reports[0][i] == 0;
    check(zeros, "zero padding");

    Lighting rainbow;
    rainbow.mode = 1;
    rainbow.random = true;
    rainbow.sleep = 4;
    const QByteArray r = frame(lightingPayload(rainbow))[0];
    check(r.left(14) == QByteArray::fromHex("0a0101022901010305000000 0104"), "neon stream rainbow");

    Lighting noColor;
    noColor.mode = 3;
    noColor.red = 10;
    check(frame(lightingPayload(noColor))[0][9] == 0, "mode without color sends zero");

    const auto multi = frame(QByteArray(381, '\x11'));
    check(multi.size() == 7 && multi[0][1] == 7 && multi[6][2] == 7 && multi[6][11] == 0x11 && multi[6][12] == 0,
          "seven packet framing");

    Lighting fast;
    fast.speed = 9;
    check(uint8_t(frame(lightingPayload(fast))[0][7]) == 5, "stock firmware clamps speed to 5");
    fast.speedLevels = 9;
    check(uint8_t(frame(lightingPayload(fast))[0][7]) == 9, "patched firmware accepts speed 9");

    if (failures == 0)
        std::printf("rk: all tests passed\n");
    return failures ? 1 : 0;
}
