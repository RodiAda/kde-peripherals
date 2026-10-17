#include <QCoreApplication>

#include <cstdio>

#include "CompxProtocol.h"

using namespace compx;

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
    check(request(Battery) == bytes({0x08, 0x04, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x49}), "battery request");
    check(request(GetEeprom, 0x00A9, 10).at(16) == char(0x92), "get eeprom checksum");
    check(request(SetEeprom, 0x000C, 8, bytes({0x0f, 0x0f, 0x00, 0x37, 0x1f, 0x1f, 0x00, 0x17})).at(16) == char(0x88),
          "dpi write checksum");
    check(checksumValid(bytes({0x08, 0x04, 0x00, 0x00, 0x00, 0x02, 0x1e, 0x00, 0x0e, 0x84, 0, 0, 0, 0, 0, 0, 0x97})),
          "reply checksum");

    auto eq = [](std::array<uint8_t, 4> a, std::initializer_list<int> b) {
        int i = 0;
        for (int x : b)
            if (a[size_t(i++)] != uint8_t(x))
                return false;
        return true;
    };
    check(eq(encodeDpi(800, 800, Codec::Paw3395), {0x0f, 0x0f, 0x00, 0x37}), "800 dpi");
    check(eq(encodeDpi(1600, 1600, Codec::Paw3395), {0x1f, 0x1f, 0x00, 0x17}), "1600 dpi");
    check(eq(encodeDpi(26000, 26000, Codec::Paw3395), {0x07, 0x07, 0x88, 0xbf}), "26000 dpi");
    check(eq(encodeDpi(30000, 30000, Codec::Paw3395), {0x57, 0x57, 0x88, 0x1f}), "30000 dpi");
    for (int dpi : {100, 400, 800, 1600, 3200, 12800, 26000, 30000}) {
        const auto e = encodeDpi(dpi, dpi, Codec::Paw3395);
        check(decodeDpiX(e.data(), Codec::Paw3395) == dpi, "paw3395 roundtrip");
    }
    for (int dpi : {50, 1000, 30000, 32000, 36000}) {
        const auto e = encodeDpi(dpi, dpi, Codec::Paw3950);
        check(decodeDpiX(e.data(), Codec::Paw3950) == dpi, "paw3950 roundtrip");
    }
    for (int dpi : {10, 800, 10000, 10050, 26000, 30000, 36000}) {
        const auto e = encodeDpi(dpi, dpi, Codec::Paw3950Ultra);
        check(decodeDpiX(e.data(), Codec::Paw3950Ultra) == dpi, "paw3950ultra roundtrip");
    }
    check(pollingRateFromCode(0x20) == 4000 && pollingRateCode(8000) == 0x40, "polling codes");
    check(unpair(bytes({0x20, 0x35}), 0) == 0x20 && !unpair(bytes({0x20, 0x36}), 0), "pair crc");

    if (failures == 0)
        std::printf("compx: all tests passed\n");
    return failures ? 1 : 0;
}
