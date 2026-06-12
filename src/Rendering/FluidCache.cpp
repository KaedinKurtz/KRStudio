#include "FluidCache.hpp"

#include <QDir>
#include <QFile>
#include <QDebug>

namespace {
struct FrameHeader {
    char magic[4] = { 'K', 'R', 'F', 'C' };
    uint32_t version = 1;
    uint32_t frameIndex = 0;
    double simTime = 0.0;
    uint32_t particleCount = 0;
    uint32_t reserved = 0;
};
} // namespace

void FluidCache::setDirectory(const QString& dir)
{
    m_dir = dir;
    QDir().mkpath(dir);
    refresh();
}

QString FluidCache::framePath(int index) const
{
    return QStringLiteral("%1/frame_%2.krfc").arg(m_dir).arg(index, 5, 10, QLatin1Char('0'));
}

void FluidCache::refresh()
{
    if (m_dir.isEmpty()) { m_frameCount = 0; return; }
    const QStringList frames =
        QDir(m_dir).entryList({ QStringLiteral("frame_*.krfc") }, QDir::Files, QDir::Name);
    m_frameCount = frames.size();
}

bool FluidCache::writeFrame(int index, double simTime, const std::vector<float>& interleaved)
{
    if (m_dir.isEmpty()) return false;
    QFile f(framePath(index));
    if (!f.open(QIODevice::WriteOnly)) return false;
    FrameHeader h;
    h.frameIndex = uint32_t(index);
    h.simTime = simTime;
    h.particleCount = uint32_t(interleaved.size() / 8);
    f.write(reinterpret_cast<const char*>(&h), sizeof(h));
    f.write(reinterpret_cast<const char*>(interleaved.data()),
            qint64(interleaved.size() * sizeof(float)));
    m_frameCount = std::max(m_frameCount, index + 1);
    return true;
}

bool FluidCache::readFrame(int index, Frame& out) const
{
    QFile f(framePath(index));
    if (!f.open(QIODevice::ReadOnly)) return false;
    FrameHeader h;
    if (f.read(reinterpret_cast<char*>(&h), sizeof(h)) != sizeof(h)) return false;
    if (memcmp(h.magic, "KRFC", 4) != 0 || h.version != 1) {
        qWarning() << "[FluidCache] bad frame file:" << framePath(index);
        return false;
    }
    out.simTime = h.simTime;
    out.data.resize(size_t(h.particleCount) * 8);
    const qint64 bytes = qint64(out.data.size() * sizeof(float));
    return f.read(reinterpret_cast<char*>(out.data.data()), bytes) == bytes;
}

void FluidCache::clear()
{
    if (m_dir.isEmpty()) return;
    QDir d(m_dir);
    for (const QString& name : d.entryList({ QStringLiteral("frame_*.krfc") }, QDir::Files))
        d.remove(name);
    m_frameCount = 0;
}
