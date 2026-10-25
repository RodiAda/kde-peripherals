#include "RkProtocol.h"

namespace rk {

const ModeInfo &modeInfo(int mode)
{
    static const ModeInfo modes[ModeCount] = {
        {"Neon Stream", true, true, true},
        {"Ripples Shining", true, true, true},
        {"Rotating Windmill", true, false, false},
        {"Sine Wave", true, true, true},
        {"Rainbow Roulette", true, false, false},
        {"Stars Twinkle", true, true, true},
        {"Layer Upon Layer", true, true, true},
        {"Rich and Honored", true, true, true},
        {"Marquee Effect", true, true, true},
        {"Rotating Storm", true, true, true},
        {"Serpentine Horse Race", true, true, true},
        {"Retro Snake", true, true, true},
        {"Diagonal Transformation", true, true, true},
        {"Custom", true, true, true},
        {"Ambilight", true, true, true},
        {"Streamer", true, false, false},
        {"Steady", false, true, true},
        {"Breathing", true, true, true},
        {"Neon", true, false, false},
        {"Shadow Disappear", true, true, true},
        {"Flash Away", true, true, true},
    };
    return modes[std::clamp(mode, 1, ModeCount) - 1];
}

QList<QByteArray> frame(const QByteArray &payload)
{
    QList<QByteArray> reports;
    const int total = (int(payload.size()) + ChunkSize - 1) / ChunkSize;
    for (int i = 0; i < total; ++i) {
        QByteArray r(ReportSize, '\0');
        r[0] = char(ReportId);
        r[1] = char(total);
        r[2] = char(i + 1);
        const QByteArray chunk = payload.mid(i * ChunkSize, ChunkSize);
        for (int k = 0; k < chunk.size(); ++k)
            r[3 + k] = chunk[k];
        reports.append(r);
    }
    return reports;
}

QByteArray lightingPayload(const Lighting &l)
{
    QByteArray p(60, '\0');
    p[0] = 0x02;
    p[1] = 0x29;
    p[2] = char(std::clamp(l.mode, 1, ModeCount));
    p[3] = 0x01;
    p[4] = char(std::clamp(l.speed, 1, l.speedLevels == 9 ? 9 : 5));
    p[5] = char(std::clamp(l.brightness, 0, 5));
    const bool useColor = !l.random && modeInfo(l.mode).color;
    p[6] = char(useColor ? l.red : 0);
    p[7] = char(useColor ? l.green : 0);
    p[8] = char(useColor ? l.blue : 0);
    p[9] = char(l.random && modeInfo(l.mode).random ? 1 : 0);
    p[10] = char(std::clamp(l.sleep, 1, 5));
    return p;
}

}
