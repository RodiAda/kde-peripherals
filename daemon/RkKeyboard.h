#pragma once

#include "Device.h"
#include "RkProtocol.h"

class RkKeyboard : public Device
{
    Q_OBJECT

public:
    using Device::Device;

    static bool matches(const HidrawInfo &info);

public Q_SLOTS:
    void start() override;
    void apply(const QString &id, const QJsonObject &change) override;

private:
    void load();
    void save();
    bool send(const QByteArray &payload);
    void render();

    Hidraw _dev;
    rk::Lighting _lighting;
    bool _known = false;
    QString _error;
};
