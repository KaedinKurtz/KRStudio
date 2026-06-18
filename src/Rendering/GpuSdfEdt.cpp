#include "GpuSdfEdt.hpp"
#include "RenderingSystem.hpp"
#include "Shader.hpp"

#include <QOpenGLFunctions_4_3_Core>
#include <glm/gtc/type_ptr.hpp>
#include <algorithm>
#include <utility>

void GpuSdfEdt::shutdown(QOpenGLFunctions_4_3_Core* gl) {
    if (!gl) return;
    if (m_seedA) gl->glDeleteBuffers(1, &m_seedA);
    if (m_seedB) gl->glDeleteBuffers(1, &m_seedB);
    m_seedA = m_seedB = m_result = 0; m_N = 0;
}

void GpuSdfEdt::allocate(QOpenGLFunctions_4_3_Core* gl, int N) {
    if (m_seedA) gl->glDeleteBuffers(1, &m_seedA);
    if (m_seedB) gl->glDeleteBuffers(1, &m_seedB);
    const GLsizeiptr bytes = GLsizeiptr(sizeof(glm::vec4)) * N * N * N;
    gl->glGenBuffers(1, &m_seedA);
    gl->glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_seedA);
    gl->glBufferData(GL_SHADER_STORAGE_BUFFER, bytes, nullptr, GL_DYNAMIC_DRAW);
    gl->glGenBuffers(1, &m_seedB);
    gl->glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_seedB);
    gl->glBufferData(GL_SHADER_STORAGE_BUFFER, bytes, nullptr, GL_DYNAMIC_DRAW);
    gl->glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
    m_N = N;
}

bool GpuSdfEdt::build(RenderingSystem& renderer, QOpenGLFunctions_4_3_Core* gl,
                      GLuint particleBuffer, int count, int stride, int posOff, int aliveOff,
                      const glm::vec3& origin, const glm::vec3& extent, int N, float radius) {
    Shader* seed = renderer.getShader("edt_seed");
    Shader* jfa = renderer.getShader("edt_jfa");
    if (!seed || !jfa || !gl) return false;
    if (N != m_N) allocate(gl, N);
    const int cells = N * N * N;

    // clear the seed grid to invalid (w = 0).
    const float zero[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    gl->glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_seedA);
    gl->glClearBufferData(GL_SHADER_STORAGE_BUFFER, GL_RGBA32F, GL_RGBA, GL_FLOAT, zero);
    gl->glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

    // seed pass: particles (binding 0) -> seedA (binding 1).
    seed->use(gl);
    seed->setInt(gl, "u_count", count);
    seed->setInt(gl, "u_stride", stride);
    seed->setInt(gl, "u_posOff", posOff);
    seed->setInt(gl, "u_aliveOff", aliveOff);
    seed->setInt(gl, "u_N", N);
    seed->setVec3(gl, "u_origin", origin);
    seed->setVec3(gl, "u_extent", extent);
    gl->glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, particleBuffer);
    gl->glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, m_seedA);
    gl->glDispatchCompute(GLuint((count + 63) / 64), 1, 1);
    gl->glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

    // JFA passes (ping-pong): seedIn (binding 1) -> seedOut (binding 2).
    GLuint cur = m_seedA, other = m_seedB;
    jfa->use(gl);
    jfa->setInt(gl, "u_N", N);
    jfa->setVec3(gl, "u_origin", origin);
    jfa->setVec3(gl, "u_extent", extent);
    for (int step = N / 2; step >= 1; step /= 2) {
        jfa->setInt(gl, "u_step", step);
        gl->glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, cur);
        gl->glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, other);
        gl->glDispatchCompute(GLuint((cells + 63) / 64), 1, 1);
        gl->glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
        std::swap(cur, other);
    }
    m_result = cur;
    (void)radius;

    gl->glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, 0);
    gl->glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, 0);
    gl->glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, 0);
    return true;
}

void GpuSdfEdt::readback(QOpenGLFunctions_4_3_Core* gl, std::vector<glm::vec4>& out) const {
    if (!gl || !m_result) return;
    const int cells = m_N * m_N * m_N;
    out.resize(size_t(cells));
    gl->glMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT | GL_SHADER_STORAGE_BARRIER_BIT);
    gl->glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_result);
    gl->glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, GLsizeiptr(sizeof(glm::vec4)) * cells, out.data());
    gl->glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
}

float GpuSdfEdt::distanceAt(const std::vector<glm::vec4>& seeds, const glm::vec3& origin,
                            const glm::vec3& extent, int N, float radius, const glm::vec3& point) {
    const glm::vec3 uvw = (point - origin) / extent;
    glm::ivec3 c = glm::ivec3(glm::floor(uvw * float(N)));
    c = glm::clamp(c, glm::ivec3(0), glm::ivec3(N - 1));
    const int cell = (c.z * N + c.y) * N + c.x;
    const glm::vec4 s = seeds[size_t(cell)];
    if (s.w < 0.5f) return 1.0e9f;
    return glm::length(point - glm::vec3(s)) - radius;
}

glm::vec3 GpuSdfEdt::gradientAt(const std::vector<glm::vec4>& seeds, const glm::vec3& origin,
                                const glm::vec3& extent, int N, float radius, const glm::vec3& point) {
    const glm::vec3 uvw = (point - origin) / extent;
    glm::ivec3 c = glm::ivec3(glm::floor(uvw * float(N)));
    c = glm::clamp(c, glm::ivec3(0), glm::ivec3(N - 1));
    const int cell = (c.z * N + c.y) * N + c.x;
    const glm::vec4 s = seeds[size_t(cell)];
    (void)radius;
    if (s.w < 0.5f) return glm::vec3(0.0f);
    const glm::vec3 d = point - glm::vec3(s);
    const float len = glm::length(d);
    return len > 1e-6f ? d / len : glm::vec3(0.0f);
}
