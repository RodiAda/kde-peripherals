#include <cstdio>

#include "HidppProtocol.h"

using namespace hidpp;

static int failures = 0;

static void check(bool ok, const char *what)
{
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

static QByteArray bytes(std::initializer_list<int> v)
{
    QByteArray b;
    for (int x : v)
        b.append(char(x));
    return b;
}

int main()
{
    const QByteArray req = request(0x01, 0x05, 0x02, bytes({0x00}));
    check(req.size() == 20 && req[0] == 0x11 && req[1] == 0x01 && req[2] == 0x05 && uint8_t(req[3]) == 0x2A, "request header");

    QByteArray reply = bytes({0x11, 0x01, 0x05, 0x2A, 0x00, 0x03, 0x20});
    reply.resize(20, '\0');
    check(isReply(reply, 0x01, 0x05, 0x02), "reply match");
    check(!isReply(reply, 0x02, 0x05, 0x02), "reply other device");
    check(isError(bytes({0x10, 0x01, 0x8F, 0x05, 0x2A, 0x09, 0x00}), 0x01, 0x05, 0x02), "hidpp10 error");
    check(isError(bytes({0x11, 0xFF, 0xFF, 0x05, 0x2A, 0x02, 0x00}), 0xFF, 0x05, 0x02), "hidpp20 error");

    const auto list = parseDpiList(bytes({0x00, 0x01, 0x90, 0xE0, 0x32, 0x03, 0x20, 0x00, 0x00}));
    check(list.size() == 9 && list.first() == 400 && list.last() == 800 && list[1] == 450, "dpi list with step");
    const auto plain = parseDpiList(bytes({0x00, 0x01, 0x90, 0x03, 0x20, 0x06, 0x40, 0x00, 0x00}));
    check(plain == QList<int>({400, 800, 1600}), "dpi plain list");

    check(batteryPercentFromVoltage(4200) == 100 && batteryPercentFromVoltage(3400) == 0, "voltage bounds");
    const int mid = batteryPercentFromVoltage(3811);
    check(mid == 50, "voltage 3811");
    check(reportRatesFromMask(0x89) == QList<int>({125, 250, 1000}), "report rate mask");
    check(unifiedBatteryLevel(0x04) == 50 && unifiedBatteryLevel(0) == -1, "unified levels");

    if (failures == 0)
        std::printf("hidpp: all tests passed\n");
    return failures ? 1 : 0;
}
