#pragma once

#include <QString>
#include <cstdint>
#include <vector>

/**
 * @brief Houdini-style fluid sim cache: one compact binary file per frame
 * (cache/<name>/frame_00000.krfc), recorded live while the simulation
 * plays and scrubbed back through the same particle SSBO the renderer
 * already consumes — baked playback looks identical to the live sim.
 *
 * Frame layout: header {magic 'KRFC', version, frameIndex, simTime,
 * particleCount} followed by particleCount * {vec4 posLife, vec4 vel}.
 */
class FluidCache
{
public:
    struct Frame {
        double simTime = 0.0;
        std::vector<float> data; // 8 floats per particle (posLife, vel)
        int particleCount() const { return int(data.size() / 8); }
    };

    void setDirectory(const QString& dir);
    const QString& directory() const { return m_dir; }

    /// Number of frames present on disk (scanned by setDirectory/refresh).
    int frameCount() const { return m_frameCount; }
    void refresh();

    bool writeFrame(int index, double simTime, const std::vector<float>& interleaved);
    bool readFrame(int index, Frame& out) const;
    void clear(); // delete all frames in the directory

private:
    QString framePath(int index) const;
    QString m_dir;
    int m_frameCount = 0;
};
