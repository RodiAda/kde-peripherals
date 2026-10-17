#include "Device.h"

Device::Device(const HidrawInfo &node, QObject *parent)
    : QObject(parent)
    , _node(node)
    , _key(node.physicalKey() + QLatin1Char('|') + node.node)
{
}

void Device::publish(const QJsonArray &entries)
{
    if (entries == _last)
        return;
    _last = entries;
    Q_EMIT entriesChanged(entries);
}

QJsonObject option(const QJsonValue &value, const QString &label)
{
    QJsonObject o{{QStringLiteral("value"), value}};
    if (!label.isEmpty())
        o[QStringLiteral("label")] = label;
    return o;
}

static QJsonObject control(const QString &type, const QString &key, const QString &section, const QString &label)
{
    return {{QStringLiteral("type"), type},
            {QStringLiteral("key"), key},
            {QStringLiteral("section"), section},
            {QStringLiteral("label"), label}};
}

QJsonObject choice(const QString &key, const QString &section, const QString &label, const QJsonValue &value,
                   const QJsonArray &options, const QString &unit)
{
    QJsonObject c = control(QStringLiteral("choice"), key, section, label);
    c[QStringLiteral("value")] = value;
    c[QStringLiteral("options")] = options;
    if (!unit.isEmpty())
        c[QStringLiteral("unit")] = unit;
    return c;
}

QJsonObject toggle(const QString &key, const QString &section, const QString &label, bool value)
{
    QJsonObject c = control(QStringLiteral("toggle"), key, section, label);
    c[QStringLiteral("value")] = value;
    return c;
}

QJsonObject slider(const QString &key, const QString &section, const QString &label, int value, int min, int max,
                   int step, const QString &unit)
{
    QJsonObject c = control(QStringLiteral("slider"), key, section, label);
    c[QStringLiteral("value")] = value;
    c[QStringLiteral("min")] = min;
    c[QStringLiteral("max")] = max;
    c[QStringLiteral("step")] = step;
    if (!unit.isEmpty())
        c[QStringLiteral("unit")] = unit;
    return c;
}

QJsonObject color(const QString &key, const QString &section, const QString &label, const QString &value)
{
    QJsonObject c = control(QStringLiteral("color"), key, section, label);
    c[QStringLiteral("value")] = value;
    return c;
}

QJsonObject actionButton(const QString &key, const QString &section, const QString &label)
{
    return control(QStringLiteral("action"), key, section, label);
}
