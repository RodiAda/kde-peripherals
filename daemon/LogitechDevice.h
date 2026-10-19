#pragma once

#include <QElapsedTimer>
#include <QHash>
#include <QMap>
#include <QSocketNotifier>
#include <QTimer>

#include <memory>
#include <optional>

#include "Device.h"
#include "HidppProtocol.h"

class LogitechDevice : public Device
{
    Q_OBJECT

public:
    using Device::Device;

    static bool matches(const HidrawInfo &info);

public Q_SLOTS:
    void start() override;
    void apply(const QString &id, const QJsonObject &change) override;

private:
    struct Reply {
        bool ok = false;
        uint8_t error = 0;
        QByteArray params;
    };

    struct Slot {
        uint8_t index = 0;
        bool online = false;
        bool loaded = false;
        QHash<uint16_t, uint8_t> features;
        QString name;
        int type = -1;
        QString firmware;
        int battery = -1;
        bool charging = false;
        QList<int> dpiList;
        int dpi = 0;
        QList<int> rates;
        int rate = 0;
        int wheelMode = 0;
        int autoDisengage = 0;
        int autoDisengageDefault = 0;
        int hiresFlags = -1;
        int thumbInvert = -1;
        int hosts = 0;
        int currentHost = -1;
        int fnInversion = -1;
        QList<uint16_t> buttons;
        QHash<uint16_t, QString> actions;
        QList<uint16_t> pressed;
        QHash<QString, QString> gestures;
        bool gestureActive = false;
        int gestureDx = 0;
        int gestureDy = 0;
        QString error;
    };

    Reply transact(uint8_t device, uint8_t featureIndex, uint8_t function, const QByteArray &params = {});
    std::optional<uint8_t> featureIndex(Slot &slot, uint16_t feature);
    Reply call(Slot &slot, uint16_t feature, uint8_t function, const QByteArray &params = {});
    void probe(uint8_t index);
    void load(Slot &slot);
    void readBattery(Slot &slot);
    void readState(Slot &slot);
    void handleNotification(const QByteArray &report);
    void loadButtons(Slot &slot);
    void applyDiverts(Slot &slot);
    void handleButtons(Slot &slot, const QByteArray &report);
    void handleRawXY(Slot &slot, const QByteArray &report);
    void finishGesture(Slot &slot);
    void runAction(const QString &id);
    QString settingsGroup(const Slot &slot) const;
    void tick();
    void render();
    QString applyChange(Slot &slot, const QJsonObject &change);
    QString entryId(const Slot &slot) const;

    Hidraw _dev;
    std::unique_ptr<QSocketNotifier> _notifier;
    QTimer _timer{this};
    QElapsedTimer _sinceBattery;
    bool _receiver = false;
    uint8_t _directIndex = hidpp::DirectIndex;
    QMap<uint8_t, Slot> _slots;
    QList<uint8_t> _reprobe;
    int _failures = 0;
};
