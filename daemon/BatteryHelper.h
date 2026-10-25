#pragma once

#include <QDBusContext>
#include <QHash>
#include <QObject>
#include <QSocketNotifier>
#include <QTimer>

#include <memory>

class BatteryHelper : public QObject, protected QDBusContext
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "io.github.rodiada.PeripheralsBattery")

public:
    static constexpr auto Service = "io.github.rodiada.PeripheralsBattery";
    static constexpr auto Path = "/io/github/rodiada/PeripheralsBattery";

    explicit BatteryHelper(QObject *parent = nullptr);
    ~BatteryHelper() override;

    static QByteArray descriptor(bool keyboard);
    static QString sanitize(const QString &text, int maxLength);

public Q_SLOTS:
    void Set(const QString &id, const QString &name, const QString &kind, int percent, bool charging);
    void Remove(const QString &id);

private Q_SLOTS:
    void onNameOwnerChanged(const QString &name, const QString &oldOwner, const QString &newOwner);

private:
    struct Battery {
        int fd = -1;
        QString owner;
        QString name;
        bool keyboard = false;
        uint8_t percent = 0;
        bool charging = false;
        std::unique_ptr<QSocketNotifier> notifier;
    };

    QString key(const QString &owner, const QString &id) const;
    bool create(Battery &battery, const QString &uniq);
    void sendLevel(Battery &battery);
    void service(Battery &battery);
    void destroy(const QString &key);
    void updateIdle();

    QHash<QString, std::shared_ptr<Battery>> _batteries;
    QTimer _idle;
};
