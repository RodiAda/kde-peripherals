#include <cstdio>

#include "Hidraw.h"

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
    HidrawInfo child;
    child.phys = QStringLiteral("usb-0000:0a:00.3-2/input2:3");
    check(child.receiverIndex() == 3, "receiver index");
    check(child.physicalKey() == QStringLiteral("usb-0000:0a:00.3-2:3"), "receiver child key");

    HidrawInfo plain;
    plain.phys = QStringLiteral("usb-0000:0a:00.3-2.4/input1");
    check(plain.receiverIndex() == -1, "no receiver index");
    check(plain.physicalKey() == QStringLiteral("usb-0000:0a:00.3-2.4"), "plain key");

    HidrawInfo bt;
    bt.phys = QStringLiteral("0c:58:7b:94:32:a3");
    bt.uniq = QStringLiteral("D8:1C:88:A2:E2:E0");
    check(bt.physicalKey() == QStringLiteral("d8:1c:88:a2:e2:e0"), "bluetooth key");

    HidrawInfo hidpp;
    hidpp.descriptor = QByteArray::fromHex("0643ff0a0202a1018511");
    check(hidpp.hasReportId(0x11) && hidpp.hasUsagePage(0xFF43) && !hidpp.hasReportId(0x10), "descriptor parsing");

    if (failures == 0)
        std::printf("hidraw: all tests passed\n");
    return failures ? 1 : 0;
}
