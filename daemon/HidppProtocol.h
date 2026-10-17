#pragma once

#include <QByteArray>
#include <QList>

#include <cstdint>

namespace hidpp {

constexpr uint8_t ShortReport = 0x10;
constexpr uint8_t LongReport = 0x11;
constexpr int LongSize = 20;
constexpr uint8_t SoftwareId = 0x0A;
constexpr uint8_t DirectIndex = 0xFF;

enum Feature : uint16_t {
    Root = 0x0000,
    FirmwareVersion = 0x0003,
    DeviceName = 0x0005,
    BatteryStatus = 0x1000,
    BatteryVoltage = 0x1001,
    UnifiedBattery = 0x1004,
    ChangeHost = 0x1814,
    SmartShift = 0x2110,
    SmartShiftEnhanced = 0x2111,
    HiresWheel = 0x2121,
    AdjustableDpi = 0x2201,
    FnInversion = 0x40A0,
    NewFnInversion = 0x40A2,
    ReportRate = 0x8060,
};

QByteArray request(uint8_t device, uint8_t featureIndex, uint8_t function, const QByteArray &params = {});
bool isReply(const QByteArray &report, uint8_t device, uint8_t featureIndex, uint8_t function);
bool isError(const QByteArray &report, uint8_t device, uint8_t featureIndex, uint8_t function);

QList<int> parseDpiList(const QByteArray &params);
int batteryPercentFromVoltage(int millivolts);
int unifiedBatteryLevel(uint8_t levelFlags);
QList<int> reportRatesFromMask(uint8_t mask);

}
