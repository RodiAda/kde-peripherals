#pragma once

#include <QByteArray>
#include <QList>
#include <QString>

#include <cstdint>
#include <optional>

struct HidrawInfo {
    QString node;
    QString sysPath;
    QString name;
    QString phys;
    QString uniq;
    uint16_t bus = 0;
    uint16_t vendor = 0;
    uint16_t product = 0;
    int interface = -1;
    QByteArray descriptor;

    bool hasReportId(uint8_t id) const;
    bool hasUsagePage(uint16_t page) const;
    QString physicalKey() const;
};

QList<HidrawInfo> enumerateHidraw();

class Hidraw
{
public:
    Hidraw() = default;
    ~Hidraw();
    Hidraw(const Hidraw &) = delete;
    Hidraw &operator=(const Hidraw &) = delete;

    bool open(const QString &node);
    void close();
    bool isOpen() const { return _fd >= 0; }
    int fd() const { return _fd; }
    QString errorString() const { return _error; }

    bool write(const QByteArray &report);
    std::optional<QByteArray> read(int timeoutMs);
    bool setFeature(const QByteArray &report);
    std::optional<QByteArray> getFeature(uint8_t reportId, int size);

private:
    void fail(const char *what);

    int _fd = -1;
    QString _error;
};
