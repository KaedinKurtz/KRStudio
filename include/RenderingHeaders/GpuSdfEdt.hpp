#pragma once
// ===========================================================================
// GPU Euclidean SDF via Jump Flooding (live-fluid avoidance SDF). Splat live
// particles -> a vec4 seed grid (nearest particle world-pos per cell) -> log2(N)
// JFA passes -> distance = |cellPos - seed| - radius (<0 inside), gradient =
// normalize(cellPos - seed). Particle-count-independent (O(N^3 log N)). Replaces
// the slow O(cells*particles) hand-rolled field. Runs on the engine GL-4.3
// compute context (no CUDA -- the AMD-compatible portable path; nvblox is the
// intended NVIDIA backend, un-buildable/un-runnable on this CUDA-less machine).
// ===========================================================================
#include <QtGui/qopengl.h>
#include <glm/glm.hpp>
#include <vector>

class RenderingSystem;
class QOpenGLFunctions_4_3_Core;

class GpuSdfEdt {
public:
    void shutdown(QOpenGLFunctions_4_3_Core* gl);

    // Build the SDF from a live particle SSBO (generic float-stride layout:
    // fluid=stride 12/pos 0/alive 3; MPM=stride 48/pos 0/alive 43). Dispatches the
    // seed + JFA passes; the result seed grid is left in resultBuffer(). Returns
    // false if the shaders are missing.
    bool build(RenderingSystem& renderer, QOpenGLFunctions_4_3_Core* gl,
               GLuint particleBuffer, int count, int stride, int posOff, int aliveOff,
               const glm::vec3& origin, const glm::vec3& extent, int N, float radius);

    GLuint resultBuffer() const { return m_result; }
    int gridN() const { return m_N; }

    // read the final seed grid back to the CPU (vec4 per cell: xyz=nearest pos, w=valid).
    void readback(QOpenGLFunctions_4_3_Core* gl, std::vector<glm::vec4>& out) const;

    // CPU SDF queries from a read-back seed grid (the gate's analytic comparison).
    static float distanceAt(const std::vector<glm::vec4>& seeds, const glm::vec3& origin,
                            const glm::vec3& extent, int N, float radius, const glm::vec3& point);
    static glm::vec3 gradientAt(const std::vector<glm::vec4>& seeds, const glm::vec3& origin,
                                const glm::vec3& extent, int N, float radius, const glm::vec3& point);

private:
    void allocate(QOpenGLFunctions_4_3_Core* gl, int N);
    GLuint m_seedA = 0, m_seedB = 0, m_result = 0;
    int m_N = 0;
};
