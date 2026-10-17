#pragma once

#include <QTimer>

#include "Device.h"

class GamepadSource : public Device
{
    Q_OBJECT

public:
    using Device::Device;

public Q_SLOTS:
    void start() override;
    void apply(const QString &id, const QJsonObject &change) override;

private:
    void refresh();
    QJsonArray fromPowerSupplies();
    QJsonArray fromBluez(const QJsonArray &known);

    QTimer _timer{this};
    QHash<QString, QString> _bluezPaths;
};
