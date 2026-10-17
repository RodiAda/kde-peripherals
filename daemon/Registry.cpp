#include "Registry.h"

#include "CompxMouse.h"
#include "LogitechDevice.h"
#include "RkKeyboard.h"

const QList<Driver> &drivers()
{
    static const QList<Driver> list{
        {"compx", &CompxMouse::matches, [](const HidrawInfo &i) -> Device * { return new CompxMouse(i); }},
        {"rk", &RkKeyboard::matches, [](const HidrawInfo &i) -> Device * { return new RkKeyboard(i); }},
        {"logitech", &LogitechDevice::matches, [](const HidrawInfo &i) -> Device * { return new LogitechDevice(i); }},
    };
    return list;
}
