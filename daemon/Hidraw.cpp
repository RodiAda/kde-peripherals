#include "Hidraw.h"

#include <QDir>
#include <QFile>
#include <QRegularExpression>

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <linux/hidraw.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <unistd.h>

namespace {

QByteArray readFile(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

template<typename Fn>
void walkDescriptor(const QByteArray &d, Fn fn)
{
    int i = 0;
    while (i < d.size()) {
        const uint8_t b = uint8_t(d[i]);
        if (b == 0xFE) {
            if (i + 1 >= d.size())
                return;
            i += 3 + uint8_t(d[i + 1]);
            continue;
        }
        static constexpr int sizes[] = {0, 1, 2, 4};
        const int size = sizes[b & 3];
        uint32_t value = 0;
        for (int k = 0; k < size && i + 1 + k < d.size(); ++k)
            value |= uint32_t(uint8_t(d[i + 1 + k])) << (8 * k);
        fn(uint8_t(b & 0xFC), value);
        i += 1 + size;
    }
}

}

bool HidrawInfo::hasReportId(uint8_t id) const
{
    bool found = false;
    walkDescriptor(descriptor, [&](uint8_t tag, uint32_t value) {
        if (tag == 0x84 && value == id)
            found = true;
    });
    return found;
}

bool HidrawInfo::hasUsagePage(uint16_t page) const
{
    bool found = false;
    walkDescriptor(descriptor, [&](uint8_t tag, uint32_t value) {
        if (tag == 0x04 && value == page)
            found = true;
    });
    return found;
}

QString HidrawInfo::physicalKey() const
{
    if (!uniq.isEmpty())
        return uniq.toLower();
    QString p = phys;
    p.remove(QRegularExpression(QStringLiteral("/input\\d+$")));
    return p;
}

QList<HidrawInfo> enumerateHidraw()
{
    QList<HidrawInfo> result;
    const QDir dir(QStringLiteral("/sys/class/hidraw"));
    for (const QString &entry : dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
        HidrawInfo info;
        info.node = QStringLiteral("/dev/") + entry;
        info.sysPath = QFileInfo(dir.filePath(entry) + QStringLiteral("/device")).canonicalFilePath();
        const QByteArray uevent = readFile(info.sysPath + QStringLiteral("/uevent"));
        for (const QByteArray &line : uevent.split('\n')) {
            const int eq = line.indexOf('=');
            if (eq < 0)
                continue;
            const QByteArray key = line.left(eq);
            const QString value = QString::fromUtf8(line.mid(eq + 1));
            if (key == "HID_NAME")
                info.name = value;
            else if (key == "HID_PHYS")
                info.phys = value;
            else if (key == "HID_UNIQ")
                info.uniq = value;
            else if (key == "HID_ID") {
                const QStringList parts = value.split(QLatin1Char(':'));
                if (parts.size() == 3) {
                    info.bus = uint16_t(parts[0].toUInt(nullptr, 16));
                    info.vendor = uint16_t(parts[1].toUInt(nullptr, 16));
                    info.product = uint16_t(parts[2].toUInt(nullptr, 16));
                }
            }
        }
        const auto m = QRegularExpression(QStringLiteral("/input(\\d+)$")).match(info.phys);
        if (m.hasMatch())
            info.interface = m.captured(1).toInt();
        info.descriptor = readFile(info.sysPath + QStringLiteral("/report_descriptor"));
        result.append(info);
    }
    return result;
}

Hidraw::~Hidraw()
{
    close();
}

bool Hidraw::open(const QString &node)
{
    close();
    _fd = ::open(QFile::encodeName(node).constData(), O_RDWR | O_NONBLOCK | O_CLOEXEC);
    if (_fd < 0) {
        fail("open");
        return false;
    }
    return true;
}

void Hidraw::close()
{
    if (_fd >= 0)
        ::close(_fd);
    _fd = -1;
}

void Hidraw::fail(const char *what)
{
    _error = QStringLiteral("%1: %2").arg(QLatin1String(what), QString::fromLocal8Bit(std::strerror(errno)));
}

bool Hidraw::write(const QByteArray &report)
{
    const ssize_t n = ::write(_fd, report.constData(), size_t(report.size()));
    if (n != report.size()) {
        fail("write");
        return false;
    }
    return true;
}

std::optional<QByteArray> Hidraw::read(int timeoutMs)
{
    pollfd p{_fd, POLLIN, 0};
    const int r = ::poll(&p, 1, timeoutMs);
    if (r <= 0)
        return std::nullopt;
    if (p.revents & (POLLERR | POLLHUP | POLLNVAL)) {
        _error = QStringLiteral("device gone");
        return std::nullopt;
    }
    QByteArray buf(4096, Qt::Uninitialized);
    const ssize_t n = ::read(_fd, buf.data(), size_t(buf.size()));
    if (n <= 0) {
        fail("read");
        return std::nullopt;
    }
    buf.truncate(int(n));
    return buf;
}

bool Hidraw::setFeature(const QByteArray &report)
{
    QByteArray buf = report;
    if (::ioctl(_fd, HIDIOCSFEATURE(buf.size()), buf.data()) < 0) {
        fail("set feature");
        return false;
    }
    return true;
}

std::optional<QByteArray> Hidraw::getFeature(uint8_t reportId, int size)
{
    QByteArray buf(size, '\0');
    buf[0] = char(reportId);
    const int n = ::ioctl(_fd, HIDIOCGFEATURE(buf.size()), buf.data());
    if (n < 0) {
        fail("get feature");
        return std::nullopt;
    }
    buf.truncate(n);
    return buf;
}
