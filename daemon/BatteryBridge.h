#pragma once

#include <QHash>
#include <QJsonArray>
#include <QObject>
#include <QString>

class BatteryBridge : public QObject
{
    Q_OBJECT

public:
    using QObject::QObject;

    void update(const QJsonArray &devices);

private:
    struct State {
        QString name;
        QString kind;
        int percent = -1;
        bool charging = false;
    };

    void call(const QString &method, const QVariantList &args);

    QHash<QString, State> _sent;
};
