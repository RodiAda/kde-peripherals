#pragma once

#include <functional>
#include <memory>

#include "Device.h"

struct Driver {
    const char *name;
    std::function<bool(const HidrawInfo &)> matches;
    std::function<Device *(const HidrawInfo &)> create;
};

const QList<Driver> &drivers();
