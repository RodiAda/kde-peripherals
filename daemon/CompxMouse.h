#pragma once

#include <QElapsedTimer>
#include <QSocketNotifier>
#include <QTimer>

#include <memory>

#include "CompxProtocol.h"
#include "Device.h"

class CompxMouse : public Device
{
    Q_OBJECT

public:
    using Device::Device;

    static bool matches(const HidrawInfo &info);

public Q_SLOTS:
    void start() override;
    void apply(const QString &id, const QJsonObject &change) override;

private:
    struct Model {
        uint8_t cid;
        uint8_t mid;
        const char *name;
        compx::Codec codec;
    };

    std::optional<QByteArray> transact(uint8_t command, uint16_t address = 0, uint8_t length = 0,
                                       const QByteArray &data = {});
    std::optional<QByteArray> readEeprom(uint16_t address, uint8_t length);
    bool writeEeprom(uint16_t address, const QByteArray &data);
    void drain();
    void poll();
    bool readAll();
    void readBattery();
    void readBlock0();
    void handlePush(const QByteArray &report);
    void render();
    void remember();
    void recall();
    const Model *findModel() const;
    QJsonObject entryBase() const;
    QString applyChange(const QJsonObject &change);

    Hidraw _dev;
    std::unique_ptr<QSocketNotifier> _notifier;
    QTimer _timer{this};
    QElapsedTimer _sinceBattery;
    bool _online = false;
    bool _loaded = false;
    bool _pushPending = false;
    int _failures = 0;
    const Model *_model = nullptr;
    uint8_t _cid = 0;
    uint8_t _mid = 0;
    QString _firmware;
    int _battery = -1;
    bool _charging = false;
    int _millivolts = 0;
    QByteArray _block0;
    QByteArray _dpi;
    QByteArray _lod;
    QByteArray _func;
    QByteArray _sensor;
    QByteArray _angle;
    QByteArray _buttons;
    QByteArray _ledEffect;
    QByteArray _ledColors;
    int _farMode = -1;
    QString _error;
};
