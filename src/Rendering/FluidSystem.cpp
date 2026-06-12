#include "FluidSystem.hpp"
#include "RenderingSystem.hpp"
#include "Shader.hpp"
#include "components.hpp"

#include <QOpenGLFunctions_4_3_Core>
#include <QDebug>
#include <glm/gtc/quaternion.hpp>
#include <random>

namespace {
struct GpuParticle {
    glm::vec4 posLife; // xyz position, w remaining life (<=0: inert)
    glm::vec4 vel;     // xyz velocity
    glm::vec4 pred;    // xyz predicted position, w lambda
};

// std140 collider block — keep in sync with fluid_*.glsl
struct GpuBox {
    glm::vec4 center;      // xyz
    glm::vec4 halfExtents; // xyz
    glm::vec4 rotation;    // quaternion xyzw
};
struct GpuSphere {
    glm::vec4 centerRadius; // xyz center, w radius
};
struct ColliderBlock {
    GpuBox boxes[FluidSystem::kMaxBoxes];
    GpuSphere spheres[FluidSystem::kMaxSpheres];
    glm::ivec4 counts; // x boxes, y spheres
};
} // namespace

void FluidSystem::initialize(RenderingSystem& renderer, QOpenGLFunctions_4_3_Core* gl)
{
    Q_UNUSED(renderer);
    if (m_initialized) return;

    const glm::vec3 extent = m_domainMax - m_domainMin;
    m_gridDim = glm::ivec3(glm::ceil(extent / m_h));
    const int cellCount = m_gridDim.x * m_gridDim.y * m_gridDim.z;

    gl->glGenBuffers(1, &m_particleSSBO);
    gl->glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_particleSSBO);
    gl->glBufferData(GL_SHADER_STORAGE_BUFFER, sizeof(GpuParticle) * kMaxParticles, nullptr, GL_DYNAMIC_DRAW);

    gl->glGenBuffers(1, &m_gridHeadSSBO);
    gl->glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_gridHeadSSBO);
    gl->glBufferData(GL_SHADER_STORAGE_BUFFER, sizeof(GLint) * cellCount, nullptr, GL_DYNAMIC_DRAW);

    gl->glGenBuffers(1, &m_gridNextSSBO);
    gl->glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_gridNextSSBO);
    gl->glBufferData(GL_SHADER_STORAGE_BUFFER, sizeof(GLint) * kMaxParticles, nullptr, GL_DYNAMIC_DRAW);
    gl->glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

    gl->glGenBuffers(1, &m_colliderUBO);
    gl->glBindBuffer(GL_UNIFORM_BUFFER, m_colliderUBO);
    gl->glBufferData(GL_UNIFORM_BUFFER, sizeof(ColliderBlock), nullptr, GL_DYNAMIC_DRAW);
    gl->glBindBuffer(GL_UNIFORM_BUFFER, 0);

    m_initialized = true;
    qInfo() << "[Fluid] initialized: grid" << m_gridDim.x << "x" << m_gridDim.y << "x" << m_gridDim.z
            << "cells, capacity" << kMaxParticles << "particles";
}

void FluidSystem::shutdown(QOpenGLFunctions_4_3_Core* gl)
{
    if (!m_initialized) return;
    gl->glDeleteBuffers(1, &m_particleSSBO);
    gl->glDeleteBuffers(1, &m_gridHeadSSBO);
    gl->glDeleteBuffers(1, &m_gridNextSSBO);
    gl->glDeleteBuffers(1, &m_colliderUBO);
    m_initialized = false;
}

void FluidSystem::appendParticles(QOpenGLFunctions_4_3_Core* gl,
                                  const std::vector<glm::vec4>& posLife,
                                  const std::vector<glm::vec4>& vel)
{
    const int n = std::min<int>(int(posLife.size()), kMaxParticles - m_particleCount);
    if (n <= 0) return;

    std::vector<GpuParticle> staging(n);
    for (int i = 0; i < n; ++i) {
        staging[i].posLife = posLife[i];
        staging[i].vel = vel[i];
        staging[i].pred = glm::vec4(glm::vec3(posLife[i]), 0.0f);
    }
    gl->glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_particleSSBO);
    gl->glBufferSubData(GL_SHADER_STORAGE_BUFFER, sizeof(GpuParticle) * m_particleCount,
                        sizeof(GpuParticle) * n, staging.data());
    gl->glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
    m_particleCount += n;
}

void FluidSystem::seedVolumes(QOpenGLFunctions_4_3_Core* gl, entt::registry& registry)
{
    std::vector<glm::vec4> pos;
    std::vector<glm::vec4> vel;
    for (auto e : registry.view<FluidVolumeComponent, TransformComponent>()) {
        const auto& vol = registry.get<FluidVolumeComponent>(e);
        const auto& xf = registry.get<TransformComponent>(e);
        const float s = std::max(0.01f, vol.particleSpacing);
        const glm::vec3 he = vol.halfExtents;
        for (float x = -he.x; x <= he.x; x += s)
            for (float y = -he.y; y <= he.y; y += s)
                for (float z = -he.z; z <= he.z; z += s) {
                    const glm::vec3 p = xf.translation + glm::vec3(xf.rotation * glm::vec3(x, y, z));
                    pos.emplace_back(p, 1e9f); // volume water is immortal
                    vel.emplace_back(0.0f);
                }
    }
    if (!pos.empty()) {
        appendParticles(gl, pos, vel);
        qInfo() << "[Fluid] seeded" << pos.size() << "particles from volumes (total" << m_particleCount << ")";
    }
    m_volumesSeeded = true;
}

void FluidSystem::emitFromEmitters(QOpenGLFunctions_4_3_Core* gl, entt::registry& registry, float dt)
{
    static std::mt19937 rng{ 1234u };
    std::uniform_real_distribution<float> uni(0.0f, 1.0f);

    std::vector<glm::vec4> pos;
    std::vector<glm::vec4> vel;

    for (auto e : registry.view<FluidEmitterComponent, TransformComponent>()) {
        auto& em = registry.get<FluidEmitterComponent>(e);
        if (!em.enabled) continue;
        const auto& xf = registry.get<TransformComponent>(e);

        em.emitAccumulator += em.ratePerSecond * dt;
        int n = int(em.emitAccumulator);
        em.emitAccumulator -= float(n);

        const glm::vec3 dir = glm::normalize(glm::vec3(xf.rotation * em.direction));
        // Orthonormal basis around the emission direction
        const glm::vec3 tmp = std::abs(dir.y) < 0.99f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
        const glm::vec3 u = glm::normalize(glm::cross(dir, tmp));
        const glm::vec3 v = glm::cross(dir, u);
        const float spreadRad = glm::radians(em.spreadDegrees);

        for (int i = 0; i < n; ++i) {
            const float ang = uni(rng) * 6.2831853f;
            const float rad = std::sqrt(uni(rng)) * em.emitterRadius;
            const glm::vec3 p = xf.translation + (u * std::cos(ang) + v * std::sin(ang)) * rad;

            const float ca = uni(rng) * 6.2831853f;
            const float cr = uni(rng) * spreadRad;
            const glm::vec3 d = glm::normalize(
                dir * std::cos(cr) + (u * std::cos(ca) + v * std::sin(ca)) * std::sin(cr));

            pos.emplace_back(p, em.particleLifetime > 0.0f ? em.particleLifetime : 1e9f);
            vel.emplace_back(d * em.initialSpeed, 0.0f);
        }
    }
    if (!pos.empty()) appendParticles(gl, pos, vel);
}

void FluidSystem::uploadColliders(QOpenGLFunctions_4_3_Core* gl, entt::registry& registry)
{
    static ColliderBlock block; // large; avoid stack churn
    int nb = 0, ns = 0;

    for (auto e : registry.view<BoxCollider, TransformComponent>()) {
        if (nb >= kMaxBoxes) break;
        const auto& c = registry.get<BoxCollider>(e);
        const auto& xf = registry.get<TransformComponent>(e);
        block.boxes[nb].center = glm::vec4(xf.translation + glm::vec3(xf.rotation * c.offset), 0.0f);
        block.boxes[nb].halfExtents = glm::vec4(c.halfExtents * xf.scale, 0.0f);
        block.boxes[nb].rotation = glm::vec4(xf.rotation.x, xf.rotation.y, xf.rotation.z, xf.rotation.w);
        ++nb;
    }
    for (auto e : registry.view<SphereCollider, TransformComponent>()) {
        if (ns >= kMaxSpheres) break;
        const auto& c = registry.get<SphereCollider>(e);
        const auto& xf = registry.get<TransformComponent>(e);
        const float maxScale = std::max({ xf.scale.x, xf.scale.y, xf.scale.z });
        block.spheres[ns].centerRadius =
            glm::vec4(xf.translation + glm::vec3(xf.rotation * c.offset), c.radius * maxScale);
        ++ns;
    }
    block.counts = glm::ivec4(nb, ns, 0, 0);

    gl->glBindBuffer(GL_UNIFORM_BUFFER, m_colliderUBO);
    gl->glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(ColliderBlock), &block);
    gl->glBindBuffer(GL_UNIFORM_BUFFER, 0);
}

void FluidSystem::update(RenderingSystem& renderer, QOpenGLFunctions_4_3_Core* gl,
                         entt::registry& registry, float dt)
{
    if (!m_initialized || !m_playing) return;

    if (!m_volumesSeeded) seedVolumes(gl, registry);
    emitFromEmitters(gl, registry, dt);
    if (m_particleCount == 0) return;

    uploadColliders(gl, registry);

    Shader* integrate = renderer.getShader("fluid_integrate");
    Shader* gridBuild = renderer.getShader("fluid_grid");
    Shader* lambda = renderer.getShader("fluid_lambda");
    Shader* deltap = renderer.getShader("fluid_deltap");
    Shader* finalize = renderer.getShader("fluid_finalize");
    if (!integrate || !gridBuild || !lambda || !deltap || !finalize) return;

    const float stepDt = glm::clamp(dt, 1.0f / 240.0f, 1.0f / 120.0f);
    const int groups = (m_particleCount + 255) / 256;
    const int cellCount = m_gridDim.x * m_gridDim.y * m_gridDim.z;

    // Particle mass chosen so a lattice at seeding spacing reaches rest density.
    const float spacing = 0.05f;
    const float particleMass = m_restDensity * spacing * spacing * spacing;

    auto bindCommon = [&](Shader* s) {
        s->use(gl);
        s->setInt(gl, "u_particleCount", m_particleCount);
        s->setFloat(gl, "u_h", m_h);
        s->setFloat(gl, "u_restDensity", m_restDensity);
        s->setFloat(gl, "u_particleMass", particleMass);
        s->setFloat(gl, "u_particleRadius", m_particleRadius);
        s->setVec3(gl, "u_domainMin", m_domainMin);
        s->setVec3(gl, "u_domainMax", m_domainMax);
        // grid dims as ivec3 (Shader helper lacks ivec3 — use 3 ints)
        s->setInt(gl, "u_gridNx", m_gridDim.x);
        s->setInt(gl, "u_gridNy", m_gridDim.y);
        s->setInt(gl, "u_gridNz", m_gridDim.z);
    };

    gl->glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, m_particleSSBO);
    gl->glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, m_gridHeadSSBO);
    gl->glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, m_gridNextSSBO);
    gl->glBindBufferBase(GL_UNIFORM_BUFFER, 3, m_colliderUBO);

    // 1) integrate: gravity + predict
    bindCommon(integrate);
    integrate->setFloat(gl, "u_dt", stepDt);
    gl->glDispatchCompute(groups, 1, 1);
    gl->glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

    // 2) clear grid heads to -1, then insert particles
    gl->glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_gridHeadSSBO);
    const GLint minusOne = -1;
    gl->glClearBufferData(GL_SHADER_STORAGE_BUFFER, GL_R32I, GL_RED_INTEGER, GL_INT, &minusOne);
    gl->glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
    gl->glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

    bindCommon(gridBuild);
    gl->glDispatchCompute(groups, 1, 1);
    gl->glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
    Q_UNUSED(cellCount);

    // 3) solver iterations: lambda then position correction (+collide)
    for (int it = 0; it < m_solverIterations; ++it) {
        bindCommon(lambda);
        gl->glDispatchCompute(groups, 1, 1);
        gl->glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

        bindCommon(deltap);
        gl->glDispatchCompute(groups, 1, 1);
        gl->glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
    }

    // 4) finalize: velocity from positions, XSPH viscosity, lifetime
    bindCommon(finalize);
    finalize->setFloat(gl, "u_dt", stepDt);
    gl->glDispatchCompute(groups, 1, 1);
    gl->glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_VERTEX_ATTRIB_ARRAY_BARRIER_BIT);

    // Diagnostic readback under the autoplay test hook: containment timeline
    // around the demo glass at (-2, 0, 0) proves the water stays inside.
    static int s_frames = 0;
    if (qEnvironmentVariableIsSet("KRS_AUTOPLAY") && (++s_frames % 120) == 0) {
        const int n = std::min(m_particleCount, 8192);
        std::vector<GpuParticle> sample(n);
        gl->glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_particleSSBO);
        gl->glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof(GpuParticle) * n, sample.data());
        gl->glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
        int inside = 0, outside = 0; float maxYIn = 0.0f; bool nan = false;
        for (const auto& q : sample) {
            if (q.posLife.w <= 0.0f) continue;
            const glm::vec3 pp(q.posLife);
            if (glm::any(glm::isnan(pp))) { nan = true; continue; }
            const bool inGlass = std::abs(pp.x + 2.0f) < 0.62f && std::abs(pp.z) < 0.62f;
            if (inGlass) { ++inside; maxYIn = std::max(maxYIn, pp.y); }
            else ++outside;
        }
        qInfo() << "[Fluid][autoplay] t=" << s_frames << "inGlass" << inside
                << "out" << outside << "maxY(in)" << maxYIn << (nan ? "NAN" : "");
    }
}
