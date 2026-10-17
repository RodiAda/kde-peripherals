#pragma once

#include <QByteArray>

#include <array>
#include <cstdint>
#include <optional>

namespace compx {

constexpr uint8_t ReportId = 0x08;
constexpr int PayloadSize = 16;
constexpr int MaxData = 10;

enum Command : uint8_t {
    MouseOnline = 0x03,
    Battery = 0x04,
    SetEeprom = 0x07,
    GetEeprom = 0x08,
    StatusPush = 0x0A,
    MouseCidMid = 0x10,
    MouseVersion = 0x12,
    SetFarDistance = 0x16,
    GetFarDistance = 0x17,
    DongleVersion = 0x1D,
};

enum class Codec { Paw3395, Paw3950, Paw3950Ultra };

QByteArray request(uint8_t command, uint16_t address = 0, uint8_t length = 0, const QByteArray &data = {});
bool checksumValid(const QByteArray &report);

constexpr uint8_t pairCrc(uint8_t v) { return uint8_t(0x55 - v); }
QByteArray pair(uint8_t v);
std::optional<uint8_t> unpair(const QByteArray &block, int offset);

std::array<uint8_t, 4> encodeDpi(int x, int y, Codec codec);
int decodeDpiX(const uint8_t *stage, Codec codec);
int dpiMin(Codec codec);
int dpiMax(Codec codec);
int dpiStep(Codec codec, int dpi);

int pollingRateFromCode(uint8_t code);
uint8_t pollingRateCode(int hz);

}
