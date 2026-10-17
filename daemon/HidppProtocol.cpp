#include "HidppProtocol.h"

namespace hidpp {

QByteArray request(uint8_t device, uint8_t featureIndex, uint8_t function, const QByteArray &params)
{
    QByteArray r(LongSize, '\0');
    r[0] = char(LongReport);
    r[1] = char(device);
    r[2] = char(featureIndex);
    r[3] = char((function << 4) | SoftwareId);
    for (int i = 0; i < params.size() && 4 + i < LongSize; ++i)
        r[4 + i] = params[i];
    return r;
}

bool isReply(const QByteArray &report, uint8_t device, uint8_t featureIndex, uint8_t function)
{
    if (report.size() < 4)
        return false;
    const uint8_t id = uint8_t(report[0]);
    if (id != ShortReport && id != LongReport && id != 0x12)
        return false;
    return uint8_t(report[1]) == device && uint8_t(report[2]) == featureIndex
        && uint8_t(report[3]) == ((function << 4) | SoftwareId);
}

bool isError(const QByteArray &report, uint8_t device, uint8_t featureIndex, uint8_t function)
{
    if (report.size() < 6 || uint8_t(report[1]) != device)
        return false;
    const uint8_t sub = uint8_t(report[2]);
    if (sub != 0xFF && sub != 0x8F)
        return false;
    return uint8_t(report[3]) == featureIndex && uint8_t(report[4]) == ((function << 4) | SoftwareId);
}

QList<int> parseDpiList(const QByteArray &params)
{
    QList<int> out;
    int step = 0;
    for (int i = 1; i + 1 < params.size(); i += 2) {
        const int v = (uint8_t(params[i]) << 8) | uint8_t(params[i + 1]);
        if (v == 0)
            break;
        if ((v >> 13) == 0b111) {
            step = v & 0x1FFF;
            continue;
        }
        if (step > 0 && !out.isEmpty()) {
            for (int d = out.last() + step; d < v; d += step)
                out.append(d);
            step = 0;
        }
        out.append(v);
    }
    return out;
}

int batteryPercentFromVoltage(int mv)
{
    static const int curve[][2] = {{4186, 100}, {4067, 90}, {3989, 80}, {3922, 70}, {3859, 60}, {3811, 50},
                                   {3778, 40},  {3751, 30}, {3717, 20}, {3671, 10}, {3646, 5},  {3579, 2},
                                   {3500, 0}};
    if (mv >= curve[0][0])
        return 100;
    for (size_t i = 1; i < std::size(curve); ++i) {
        if (mv >= curve[i][0]) {
            const int v0 = curve[i][0], p0 = curve[i][1], v1 = curve[i - 1][0], p1 = curve[i - 1][1];
            return p0 + (mv - v0) * (p1 - p0) / (v1 - v0);
        }
    }
    return 0;
}

int unifiedBatteryLevel(uint8_t flags)
{
    if (flags & 0x08)
        return 100;
    if (flags & 0x04)
        return 50;
    if (flags & 0x02)
        return 20;
    if (flags & 0x01)
        return 5;
    return -1;
}

QList<int> reportRatesFromMask(uint8_t mask)
{
    QList<int> hz;
    for (int ms = 8; ms >= 1; --ms) {
        if (mask & (1 << (ms - 1)))
            hz.append(1000 / ms);
    }
    return hz;
}

}
