#pragma once

#include <QByteArray>
#include <QList>

#include <cstdint>

namespace rk {

constexpr uint8_t ReportId = 0x0A;
constexpr int ReportSize = 65;
constexpr int ChunkSize = 62;

struct Lighting {
    int mode = 17;
    int speed = 3;
    int brightness = 5;
    uint8_t red = 255;
    uint8_t green = 255;
    uint8_t blue = 255;
    bool random = false;
    int sleep = 5;
};

struct ModeInfo {
    const char *name;
    bool speed;
    bool random;
    bool color;
};

constexpr int ModeCount = 21;
const ModeInfo &modeInfo(int mode);

QList<QByteArray> frame(const QByteArray &payload);
QByteArray lightingPayload(const Lighting &l);

}
