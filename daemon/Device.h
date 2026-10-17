#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QString>

#include "Hidraw.h"

class Device : public QObject
{
    Q_OBJECT

public:
    explicit Device(const HidrawInfo &node, QObject *parent = nullptr);

    QString key() const { return _key; }
    QString node() const { return _node.node; }
    const HidrawInfo &info() const { return _node; }

public Q_SLOTS:
    virtual void start() = 0;
    virtual void apply(const QString &id, const QJsonObject &change) = 0;

Q_SIGNALS:
    void entriesChanged(const QJsonArray &entries);
    void gone();

protected:
    void publish(const QJsonArray &entries);

    HidrawInfo _node;
    QString _key;
    QJsonArray _last;
};

QJsonObject option(const QJsonValue &value, const QString &label = {});
QJsonObject choice(const QString &key, const QString &section, const QString &label, const QJsonValue &value,
                   const QJsonArray &options, const QString &unit = {});
QJsonObject toggle(const QString &key, const QString &section, const QString &label, bool value);
QJsonObject slider(const QString &key, const QString &section, const QString &label, int value, int min, int max,
                   int step, const QString &unit = {});
QJsonObject color(const QString &key, const QString &section, const QString &label, const QString &value);
QJsonObject actionButton(const QString &key, const QString &section, const QString &label);
