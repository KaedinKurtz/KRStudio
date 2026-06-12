#include "FluidSystem.hpp"
#include "RenderingSystem.hpp"
#include "Shader.hpp"
#include "components.hpp"
#include "HardwareCaps.hpp"

#include "SdfBaker.hpp"

#include <QOpenGLFunctions_4_3_Core>
#include <QDebug>
#include <QDir>
#include <glm/gtc/quaternion.hpp>
#include <array>
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

    // Test hooks: select solver tier / render mode from the environment.
    const QString backendEnv = qEnvironmentVariable("KRS_FLUID_BACKEND").toLower();
    if (backendEnv == QLatin1String("dfsph")) m_requestedBackend = FluidBackend::DfsphCpu;
    else if (backendEnv == QLatin1String("pbf")) m_requestedBackend = FluidBackend::PbfGpu;
    const QString renderEnv = qEnvironmentVariable("KRS_FLUID_RENDER").toLower();
    if (renderEnv == QLatin1String("particles")) m_appearance.renderMode = FluidRenderMode::Particles;
    else if (renderEnv == QLatin1String("surface")) m_appearance.renderMode = FluidRenderMode::WaterSurface;
    const QStringList colorEnv = qEnvironmentVariable("KRS_FLUID_COLOR").split(',');
    if (colorEnv.size() == 3)
        m_appearance.color = { colorEnv[0].toFloat(), colorEnv[1].toFloat(), colorEnv[2].toFloat() };
    bool turbidityOk = false;
    const float turbidityEnv = qEnvironmentVariable("KRS_FLUID_TURBIDITY").toFloat(&turbidityOk);
    if (turbidityOk) m_appearance.turbidity = turbidityEnv;
    if (qEnvironmentVariableIsSet("KRS_FLUID_RECORD")) setRecording(true);
    bool foamOk = false;
    const float foamEnv = qEnvironmentVariable("KRS_FLUID_FOAM").toFloat(&foamOk);
    if (foamOk) m_appearance.foaminess = foamEnv;
    // Surface quality: High by default on CUDA-class GPUs, Low on iGPUs.
    // KRS_FLUID_QUALITY={0,1} forces a tier for testing.
    m_appearance.surfaceQuality = krs::hardwareCaps().cudaPhysics ? 1 : 0;
    bool qualityOk = false;
    const int qualityEnv = qEnvironmentVariable("KRS_FLUID_QUALITY").toInt(&qualityOk);
    if (qualityOk) m_appearance.surfaceQuality = qualityEnv != 0 ? 1 : 0;

    gl->glGenBuffers(1, &m_particleSSBO);
    gl->glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_particleSSBO);
    gl->glBufferData(GL_SHADER_STORAGE_BUFFER, sizeof(GpuParticle) * kMaxParticles, nullptr, GL_DYNAMIC_DRAW);

    m_h = 2.0f * m_params.particleRadius;
    m_builtRadius = m_params.particleRadius;
    rebuildGrid(gl);

    gl->glGenBuffers(1, &m_gridNextSSBO);
    gl->glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_gridNextSSBO);
    gl->glBufferData(GL_SHADER_STORAGE_BUFFER, sizeof(GLint) * kMaxParticles, nullptr, GL_DYNAMIC_DRAW);
    gl->glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

    gl->glGenBuffers(1, &m_colliderUBO);
    gl->glBindBuffer(GL_UNIFORM_BUFFER, m_colliderUBO);
    gl->glBufferData(GL_UNIFORM_BUFFER, sizeof(ColliderBlock), nullptr, GL_DYNAMIC_DRAW);
    gl->glBindBuffer(GL_UNIFORM_BUFFER, 0);

    // Two-way coupling impulse accumulator: one fixed-point ivec4 per
    // collider slot (32 boxes then 32 spheres).
    {
        std::vector<int> zeros(size_t(kMaxBoxes + kMaxSpheres) * 4, 0);
        gl->glGenBuffers(1, &m_impulseSSBO);
        gl->glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_impulseSSBO);
        gl->glBufferData(GL_SHADER_STORAGE_BUFFER, GLsizeiptr(zeros.size() * sizeof(int)),
                         zeros.data(), GL_DYNAMIC_DRAW);
        gl->glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
    }

    // Per-particle ellipsoid fits for the anisotropic surface splats
    // (quat + radii + smoothed centre, 3 vec4s).
    gl->glGenBuffers(1, &m_anisoSSBO);
    gl->glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_anisoSSBO);
    gl->glBufferData(GL_SHADER_STORAGE_BUFFER,
                     GLsizeiptr(sizeof(glm::vec4)) * 3 * kMaxParticles, nullptr, GL_DYNAMIC_DRAW);
    gl->glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

    // Surface normals (finalize writes, foam emission reads) and the
    // whitewater auto-normalisation maxima (3 fixed-point ints).
    gl->glGenBuffers(1, &m_normalsSSBO);
    gl->glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_normalsSSBO);
    gl->glBufferData(GL_SHADER_STORAGE_BUFFER,
                     GLsizeiptr(sizeof(glm::vec4)) * kMaxParticles, nullptr, GL_DYNAMIC_DRAW);
    gl->glGenBuffers(1, &m_foamNormSSBO);
    gl->glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_foamNormSSBO);
    const int normZeros[4] = { 0, 0, 0, 0 };
    gl->glBufferData(GL_SHADER_STORAGE_BUFFER, sizeof(normZeros), normZeros, GL_DYNAMIC_DRAW);
    gl->glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

    // Whitewater diffuse particles (zeroed: life 0 = dead slot).
    {
        std::vector<float> zeros(size_t(kMaxDiffuse) * 8, 0.0f);
        gl->glGenBuffers(1, &m_diffuseSSBO);
        gl->glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_diffuseSSBO);
        gl->glBufferData(GL_SHADER_STORAGE_BUFFER, GLsizeiptr(zeros.size() * sizeof(float)),
                         zeros.data(), GL_DYNAMIC_DRAW);
        const GLuint zero = 0;
        gl->glGenBuffers(1, &m_diffuseCounterSSBO);
        gl->glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_diffuseCounterSSBO);
        gl->glBufferData(GL_SHADER_STORAGE_BUFFER, sizeof(GLuint), &zero, GL_DYNAMIC_DRAW);
        gl->glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
    }

    m_initialized = true;
    qInfo() << "[Fluid] initialized: grid" << m_gridDim.x << "x" << m_gridDim.y << "x" << m_gridDim.z
            << "cells, capacity" << kMaxParticles << "particles";
}

void FluidSystem::shutdown(QOpenGLFunctions_4_3_Core* gl)
{
    if (!m_initialized) return;
    for (auto& s : m_sdfColliders)
        if (s.texture) gl->glDeleteTextures(1, &s.texture);
    m_sdfColliders.clear();
    gl->glDeleteBuffers(1, &m_particleSSBO);
    gl->glDeleteBuffers(1, &m_gridHeadSSBO);
    gl->glDeleteBuffers(1, &m_gridNextSSBO);
    gl->glDeleteBuffers(1, &m_colliderUBO);
    gl->glDeleteBuffers(1, &m_diffuseSSBO);
    gl->glDeleteBuffers(1, &m_diffuseCounterSSBO);
    gl->glDeleteBuffers(1, &m_impulseSSBO);
    gl->glDeleteBuffers(1, &m_anisoSSBO);
    gl->glDeleteBuffers(1, &m_normalsSSBO);
    gl->glDeleteBuffers(1, &m_foamNormSSBO);
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

void FluidSystem::bakeSdfColliders(QOpenGLFunctions_4_3_Core* gl, entt::registry& registry)
{
    // Release previous bakes
    for (auto& s : m_sdfColliders)
        if (s.texture) gl->glDeleteTextures(1, &s.texture);
    m_sdfColliders.clear();
    m_sdfsBaked = true;

    for (auto e : registry.view<SDFColliderComponent, RenderableMeshComponent, TransformComponent>()) {
        if (int(m_sdfColliders.size()) >= kMaxSdfColliders) {
            qWarning() << "[Fluid] SDF collider cap reached (" << kMaxSdfColliders << ")";
            break;
        }
        const auto& sdfc = registry.get<SDFColliderComponent>(e);
        const auto& mesh = registry.get<RenderableMeshComponent>(e);
        const auto& xf = registry.get<TransformComponent>(e);

        SdfBakeResult baked;
        if (!bakeMeshToSdf(mesh.vertices, mesh.indices, xf.getTransform(), sdfc.voxelSize, baked))
            continue;

        SdfCollider out;
        out.aabbMin = baked.aabbMin;
        out.aabbMax = baked.aabbMax;
        out.dims = baked.dims;
        out.cpuField = std::move(baked.field);

        gl->glGenTextures(1, &out.texture);
        gl->glBindTexture(GL_TEXTURE_3D, out.texture);
        gl->glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        gl->glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        gl->glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        gl->glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        gl->glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
        gl->glTexImage3D(GL_TEXTURE_3D, 0, GL_R32F, out.dims.x, out.dims.y, out.dims.z,
                         0, GL_RED, GL_FLOAT, out.cpuField.data());
        gl->glBindTexture(GL_TEXTURE_3D, 0);

        m_sdfColliders.push_back(std::move(out));
    }
}

void FluidSystem::seedVolumes(QOpenGLFunctions_4_3_Core* gl, entt::registry& registry)
{
    std::vector<glm::vec4> pos;
    std::vector<glm::vec4> vel;
    for (auto e : registry.view<FluidVolumeComponent, TransformComponent>()) {
        const auto& vol = registry.get<FluidVolumeComponent>(e);
        const auto& xf = registry.get<TransformComponent>(e);
        // Single resolution source of truth: the particle mass is computed
        // from the global rest spacing, so volumes MUST seed at it too —
        // mixed spacings give particles the wrong density and the column
        // compresses (caught by the fluid-column benchmark).
        const float s = m_params.particleRadius;
        Q_UNUSED(vol.particleSpacing);
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
    m_boxEntities.clear();
    m_sphereEntities.clear();

    for (auto e : registry.view<BoxCollider, TransformComponent>()) {
        if (nb >= kMaxBoxes) break;
        const auto& c = registry.get<BoxCollider>(e);
        const auto& xf = registry.get<TransformComponent>(e);
        block.boxes[nb].center = glm::vec4(xf.translation + glm::vec3(xf.rotation * c.offset), 0.0f);
        block.boxes[nb].halfExtents = glm::vec4(c.halfExtents * xf.scale, 0.0f);
        block.boxes[nb].rotation = glm::vec4(xf.rotation.x, xf.rotation.y, xf.rotation.z, xf.rotation.w);
        m_boxEntities.push_back(e);
        ++nb;
    }
    for (auto e : registry.view<SphereCollider, TransformComponent>()) {
        if (ns >= kMaxSpheres) break;
        const auto& c = registry.get<SphereCollider>(e);
        const auto& xf = registry.get<TransformComponent>(e);
        const float maxScale = std::max({ xf.scale.x, xf.scale.y, xf.scale.z });
        block.spheres[ns].centerRadius =
            glm::vec4(xf.translation + glm::vec3(xf.rotation * c.offset), c.radius * maxScale);
        m_sphereEntities.push_back(e);
        ++ns;
    }
    block.counts = glm::ivec4(nb, ns, 0, 0);

    gl->glBindBuffer(GL_UNIFORM_BUFFER, m_colliderUBO);
    gl->glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(ColliderBlock), &block);
    gl->glBindBuffer(GL_UNIFORM_BUFFER, 0);
}

const char* fluidBackendName(FluidBackend backend)
{
    switch (backend) {
    case FluidBackend::Auto:     return "Auto";
    case FluidBackend::PbfGpu:   return "PBF (GPU compute)";
    case FluidBackend::DfsphCpu: return "DFSPH (CPU reference, SPlisHSPlasH)";
    case FluidBackend::PhysxGpu: return "PBD (PhysX GPU)";
    }
    return "?";
}

FluidBackend FluidSystem::activeBackend() const
{
    if (m_requestedBackend == FluidBackend::Auto) {
        // Highest interactive tier the hardware offers. PhysX GPU PBD will
        // slot in here for CUDA machines once implemented; the GL-compute
        // PBF runs interactively on any GL 4.3 GPU.
        return FluidBackend::PbfGpu;
    }
    if (m_requestedBackend == m_externalTier && m_externalSolver
        && m_externalSolver->available())
        return m_externalTier;
    if (m_requestedBackend == FluidBackend::PbfGpu) return FluidBackend::PbfGpu;
    // Requested tier isn't available (no solver installed / no hardware).
    return FluidBackend::PbfGpu;
}

void FluidSystem::setExternalSolver(FluidBackend tier, std::unique_ptr<IFluidSolver> solver)
{
    m_externalTier = tier;
    m_externalSolver = std::move(solver);
}

void FluidSystem::dispatchAniso(RenderingSystem& renderer, QOpenGLFunctions_4_3_Core* gl,
                                bool rebuildGridFirst)
{
    m_anisoValid = false;
    if (m_appearance.surfaceQuality <= 0
        || m_appearance.renderMode != FluidRenderMode::WaterSurface
        || m_particleCount == 0)
        return;
    Shader* anisoShader = renderer.getShader("fluid_aniso");
    if (!anisoShader) return;

    const int groups = (m_particleCount + 255) / 256;
    auto bindUniforms = [&](Shader* s) {
        s->use(gl);
        s->setInt(gl, "u_particleCount", m_particleCount);
        s->setFloat(gl, "u_h", m_h);
        s->setVec3(gl, "u_domainMin", m_domainMin);
        s->setVec3(gl, "u_domainMax", m_domainMax);
        s->setInt(gl, "u_gridNx", m_gridDim.x);
        s->setInt(gl, "u_gridNy", m_gridDim.y);
        s->setInt(gl, "u_gridNz", m_gridDim.z);
    };

    gl->glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, m_particleSSBO);
    gl->glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, m_gridHeadSSBO);
    gl->glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, m_gridNextSSBO);
    gl->glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 6, m_anisoSSBO);

    if (rebuildGridFirst) {
        Shader* gridBuild = renderer.getShader("fluid_grid");
        if (!gridBuild) return;
        gl->glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_gridHeadSSBO);
        const GLint minusOne = -1;
        gl->glClearBufferData(GL_SHADER_STORAGE_BUFFER, GL_R32I, GL_RED_INTEGER, GL_INT, &minusOne);
        gl->glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
        gl->glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
        bindUniforms(gridBuild);
        gl->glDispatchCompute(groups, 1, 1);
        gl->glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
    }

    bindUniforms(anisoShader);
    gl->glDispatchCompute(groups, 1, 1);
    gl->glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_VERTEX_ATTRIB_ARRAY_BARRIER_BIT);
    m_anisoValid = true;
}

void FluidSystem::rebuildGrid(QOpenGLFunctions_4_3_Core* gl)
{
    if (m_gridHeadSSBO) gl->glDeleteBuffers(1, &m_gridHeadSSBO);
    const glm::vec3 extent = m_domainMax - m_domainMin;
    m_gridDim = glm::ivec3(glm::ceil(extent / m_h));
    const int cellCount = m_gridDim.x * m_gridDim.y * m_gridDim.z;
    gl->glGenBuffers(1, &m_gridHeadSSBO);
    gl->glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_gridHeadSSBO);
    gl->glBufferData(GL_SHADER_STORAGE_BUFFER, sizeof(GLint) * cellCount, nullptr, GL_DYNAMIC_DRAW);
    gl->glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
    qInfo() << "[Fluid] grid rebuilt: h =" << m_h << "->" << m_gridDim.x << "x" << m_gridDim.y
            << "x" << m_gridDim.z << "cells";
}

void FluidSystem::setRecording(bool on)
{
    if (on) {
        if (m_cache.directory().isEmpty())
            m_cache.setDirectory(QDir::currentPath() + QStringLiteral("/fluidcache"));
        m_cache.clear();
        m_recordFrame = 0;
        m_recordTime = 0.0;
        qInfo() << "[Fluid] recording bake to" << m_cache.directory();
    }
    else if (m_recording) {
        qInfo() << "[Fluid] bake stopped:" << m_recordFrame << "frames";
    }
    m_recording = on;
}

void FluidSystem::update(RenderingSystem& renderer, QOpenGLFunctions_4_3_Core* gl,
                         entt::registry& registry, float dt)
{
    // Scrub playback: replay a baked frame through the live particle SSBO
    // (engine context — works whether or not the simulation is playing).
    if (m_initialized && m_pendingScrubFrame >= 0) {
        FluidCache::Frame frame;
        if (m_cache.readFrame(m_pendingScrubFrame, frame)) {
            const int n = std::min(frame.particleCount(), int(kMaxParticles));
            std::vector<GpuParticle> upload;
            upload.resize(size_t(n));
            for (int i = 0; i < n; ++i) {
                const float* src = frame.data.data() + size_t(i) * 8;
                upload[size_t(i)].posLife = glm::vec4(src[0], src[1], src[2], src[3]);
                upload[size_t(i)].vel = glm::vec4(src[4], src[5], src[6], 0.0f);
                upload[size_t(i)].pred = glm::vec4(src[0], src[1], src[2], 0.0f);
            }
            gl->glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_particleSSBO);
            gl->glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0,
                                static_cast<GLsizeiptr>(sizeof(GpuParticle)) * n, upload.data());
            gl->glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
            m_particleCount = n;
            // The grid still reflects the last simulated frame — rebuild it
            // for the scrubbed positions before fitting splat ellipsoids.
            dispatchAniso(renderer, gl, true);
        }
        m_pendingScrubFrame = -1;
    }

    if (!m_initialized || !m_playing) return;

    // Live resolution change: h and the grid follow the particle radius.
    if (m_builtRadius != m_params.particleRadius) {
        m_h = 2.0f * m_params.particleRadius;
        m_builtRadius = m_params.particleRadius;
        rebuildGrid(gl);
    }

    if (activeBackend() == m_externalTier && m_externalSolver) {
        m_particleCount = m_externalSolver->update(renderer, gl, registry, dt);
        static int telemetryFrame = 0;
        if ((qEnvironmentVariableIsSet("KRS_AUTOPLAY") || qEnvironmentVariableIsSet("KRS_BENCH"))
            && (++telemetryFrame % 30) == 0)
            m_externalSolver->samplePositions(m_positionMirror);
        return;
    }

    if (!m_sdfsBaked) bakeSdfColliders(gl, registry);
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
    const float spacing = m_params.particleRadius; // rest spacing d_rest
    const float particleMass = m_params.restDensity * spacing * spacing * spacing;

    auto bindCommon = [&](Shader* s) {
        s->use(gl);
        s->setInt(gl, "u_particleCount", m_particleCount);
        s->setFloat(gl, "u_h", m_h);
        s->setFloat(gl, "u_restDensity", m_params.restDensity);
        s->setFloat(gl, "u_particleMass", particleMass);
        s->setFloat(gl, "u_particleRadius", m_params.particleRadius);
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
    integrate->setVec3(gl, "u_gravity", m_params.gravity);
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
    // SDF colliders: bind 3D distance textures + their world AABBs.
    auto bindSdfUniforms = [&](Shader* s) {
        const int n = std::min<int>(int(m_sdfColliders.size()), kMaxSdfColliders);
        s->setInt(gl, "u_sdfCount", n);
        for (int i = 0; i < n; ++i) {
            gl->glActiveTexture(GL_TEXTURE8 + i);
            gl->glBindTexture(GL_TEXTURE_3D, m_sdfColliders[i].texture);
            s->setInt(gl, ("u_sdf[" + std::to_string(i) + "]").c_str(), 8 + i);
            s->setVec3(gl, ("u_sdfMin[" + std::to_string(i) + "]").c_str(), m_sdfColliders[i].aabbMin);
            s->setVec3(gl, ("u_sdfMax[" + std::to_string(i) + "]").c_str(), m_sdfColliders[i].aabbMax);
        }
        gl->glActiveTexture(GL_TEXTURE0);
    };

    // Zero + bind the fluid->rigid impulse accumulator for this frame.
    {
        const GLint izero = 0;
        gl->glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_impulseSSBO);
        gl->glClearBufferData(GL_SHADER_STORAGE_BUFFER, GL_R32I, GL_RED_INTEGER, GL_INT, &izero);
        gl->glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 5, m_impulseSSBO);
    }

    for (int it = 0; it < m_params.solverIterations; ++it) {
        bindCommon(lambda);
        gl->glDispatchCompute(groups, 1, 1);
        gl->glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

        bindCommon(deltap);
        bindSdfUniforms(deltap);
        deltap->setFloat(gl, "u_dt", stepDt);
        gl->glDispatchCompute(groups, 1, 1);
        gl->glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
    }

    // 4) finalize: velocity from positions, XSPH viscosity + Akinci
    //    cohesion (surface tension), lifetime. Also writes per-particle
    //    surface normals (binding 7) for the whitewater crest potential.
    gl->glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 7, m_normalsSSBO);
    bindCommon(finalize);
    finalize->setFloat(gl, "u_dt", stepDt);
    // XSPH c from real kinematic viscosity: c ≈ 7.3·ν·dt/h² (research
    // brief), floored at 0.01 as the solver-noise filter; the legacy XSPH
    // slider adds on top for artistic damping.
    const float nu = m_params.dynamicViscosityPaS / m_params.restDensity;
    const float cMapped = 7.3f * nu * stepDt / (m_h * m_h);
    finalize->setFloat(gl, "u_viscosity",
                       glm::clamp(m_params.viscosity + std::max(0.01f, cMapped), 0.0f, 0.6f));
    // Akinci-style cohesion from sigma [N/m]. The published gamma=1 "water"
    // assumes unit-ish masses; with our SI particle masses the stable range
    // sits ~20x lower (gas explosion above it — found empirically).
    finalize->setFloat(gl, "u_cohesion", m_params.surfaceTensionNpm * 0.7f);
    gl->glDispatchCompute(groups, 1, 1);
    gl->glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_VERTEX_ATTRIB_ARRAY_BARRIER_BIT);

    // 4b) ellipsoid fits for the anisotropic surface splats (grid from this
    //     frame is still valid; positions just finalized).
    dispatchAniso(renderer, gl, false);

    // 5) whitewater: emit diffuse particles where air gets trapped / crests
    //    break, then advect them by local-fluid classification.
    Shader* foamEmit = renderer.getShader("fluid_foam_emit");
    Shader* foamUpdate = renderer.getShader("fluid_foam_update");
    if (foamEmit && foamUpdate && m_appearance.foaminess > 0.001f) {
        ++m_foamFrame;
        gl->glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, m_diffuseSSBO);
        gl->glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 4, m_diffuseCounterSSBO);
        gl->glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 7, m_normalsSSBO);

        // Auto-normalised taus (Bender 2019): tau_max rides a fast-rise /
        // slow-decay envelope of the raw per-frame potential maxima, so the
        // foam slider behaves identically across scenes and scales.
        {
            const GLint izero = 0;
            gl->glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_foamNormSSBO);
            gl->glClearBufferData(GL_SHADER_STORAGE_BUFFER, GL_R32I, GL_RED_INTEGER, GL_INT,
                                  &izero);
            gl->glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 8, m_foamNormSSBO);
        }
        const glm::vec3 floors(1.0f, 0.3f, 0.002f); // ta [m/s], wc, ke [J]
        const glm::vec3 tauMax = glm::max(m_foamPotentialEma, floors);

        bindCommon(foamEmit);
        foamEmit->setInt(gl, "u_maxDiffuse", kMaxDiffuse);
        foamEmit->setFloat(gl, "u_dt", stepDt);
        foamEmit->setFloat(gl, "u_foaminess", m_appearance.foaminess);
        foamEmit->setFloat(gl, "u_foamScale", m_appearance.foamScale);
        foamEmit->setVec2(gl, "u_tauTa", glm::vec2(0.1f * tauMax.x, tauMax.x));
        foamEmit->setVec2(gl, "u_tauWc", glm::vec2(0.1f * tauMax.y, tauMax.y));
        foamEmit->setVec2(gl, "u_tauKe", glm::vec2(0.1f * tauMax.z, tauMax.z));
        gl->glUniform1ui(gl->glGetUniformLocation(foamEmit->ID, "u_frame"), m_foamFrame);
        gl->glDispatchCompute(groups, 1, 1);
        gl->glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

        // Fold this frame's raw maxima into the envelope for next frame.
        {
            int raw[3] = { 0, 0, 0 };
            gl->glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_foamNormSSBO);
            gl->glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof(raw), raw);
            gl->glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
            const glm::vec3 rawMax(raw[0] / 1000.0f, raw[1] / 1000.0f, raw[2] / 1000.0f);
            m_foamPotentialEma = glm::max(rawMax, glm::mix(m_foamPotentialEma, rawMax, 0.02f));
        }

        bindCommon(foamUpdate);
        foamUpdate->setInt(gl, "u_maxDiffuse", kMaxDiffuse);
        foamUpdate->setFloat(gl, "u_dt", stepDt);
        foamUpdate->setVec3(gl, "u_gravity", m_params.gravity);
        foamUpdate->setFloat(gl, "u_foamDecay", m_appearance.foamDecay);
        gl->glDispatchCompute((kMaxDiffuse + 255) / 256, 1, 1);
        gl->glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_VERTEX_ATTRIB_ARRAY_BARRIER_BIT);
    }

    // 6) Fluid -> rigid coupling: read the per-collider impulse accumulator
    //    and hand net impulses to the rigid solver (same thread).
    if (m_impulseSink) {
        std::array<glm::ivec4, kMaxBoxes + kMaxSpheres> raw{};
        gl->glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_impulseSSBO);
        gl->glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof(raw), raw.data());
        gl->glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
        constexpr float kInvScale = 1.0f / 1.0e7f; // fixed-point decode
        // ("emit" is a Qt macro — don't name the lambda that.)
        auto sendImpulse = [&](int slot, entt::entity e) {
            const glm::ivec4& v = raw[size_t(slot)];
            if ((v.x | v.y | v.z) == 0) return;
            m_impulseSink(e, glm::vec3(v) * kInvScale);
        };
        for (size_t i = 0; i < m_boxEntities.size(); ++i) sendImpulse(int(i), m_boxEntities[i]);
        for (size_t i = 0; i < m_sphereEntities.size(); ++i)
            sendImpulse(kMaxBoxes + int(i), m_sphereEntities[i]);
    }

    // Bake recording: one cache frame per rendered frame while playing.
    if (m_recording && m_particleCount > 0) {
        std::vector<GpuParticle> sample;
        sample.resize(size_t(m_particleCount));
        gl->glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_particleSSBO);
        gl->glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0,
                               static_cast<GLsizeiptr>(sizeof(GpuParticle)) * m_particleCount,
                               sample.data());
        gl->glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
        m_recordScratch.resize(size_t(m_particleCount) * 8);
        for (int i = 0; i < m_particleCount; ++i) {
            float* dst = m_recordScratch.data() + size_t(i) * 8;
            dst[0] = sample[size_t(i)].posLife.x; dst[1] = sample[size_t(i)].posLife.y;
            dst[2] = sample[size_t(i)].posLife.z; dst[3] = sample[size_t(i)].posLife.w;
            dst[4] = sample[size_t(i)].vel.x; dst[5] = sample[size_t(i)].vel.y;
            dst[6] = sample[size_t(i)].vel.z; dst[7] = 0.0f;
        }
        m_recordTime += dt;
        m_cache.writeFrame(m_recordFrame++, m_recordTime, m_recordScratch);
    }

    // Telemetry mirror: refresh a CPU copy of particle positions for the
    // benchmark suite / autoplay diagnostics.
    static const bool s_telemetry = qEnvironmentVariableIsSet("KRS_AUTOPLAY")
                                 || qEnvironmentVariableIsSet("KRS_BENCH");
    static int s_frames = 0;
    if (s_telemetry && (++s_frames % 30) == 0) {
        // FULL population: sampling the first N was just the bottom seeded
        // slab — it reported "contained" while upper layers escaped.
        const int n = m_particleCount;
        std::vector<GpuParticle> sample(n);
        gl->glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_particleSSBO);
        gl->glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof(GpuParticle) * n, sample.data());
        gl->glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
        m_positionMirror.resize(n);
        for (int i = 0; i < n; ++i) m_positionMirror[i] = sample[i].posLife;
        // Dispersal telemetry: p95 height + horizontal extent — separates
        // "dense pool" from "exploded into gas" without screenshots.
        std::vector<float> ys;
        float maxR = 0.0f;
        ys.reserve(size_t(n));
        for (int i = 0; i < n; ++i) {
            if (sample[size_t(i)].posLife.w <= 0.0f) continue;
            ys.push_back(sample[size_t(i)].posLife.y);
            maxR = std::max(maxR, std::max(std::abs(sample[size_t(i)].posLife.x),
                                           std::abs(sample[size_t(i)].posLife.z)));
        }
        if (!ys.empty()) {
            std::sort(ys.begin(), ys.end());
            int firstDead = -1, lastDead = -1, deadCount = 0;
            float deadW = 0.0f;
            glm::vec3 deadPos(0.0f);
            for (int i = 0; i < n; ++i) {
                if (sample[size_t(i)].posLife.w > 0.0f) continue;
                if (firstDead < 0) {
                    firstDead = i;
                    deadW = sample[size_t(i)].posLife.w;
                    deadPos = glm::vec3(sample[size_t(i)].posLife);
                }
                lastDead = i;
                ++deadCount;
            }
            qInfo() << "[Fluid][autoplay] p95Y" << ys[size_t(ys.size() * 0.95)]
                    << "maxXZ" << maxR << "live" << ys.size() << "dead" << deadCount
                    << "range" << firstDead << ".." << lastDead << "w" << deadW
                    << "at" << deadPos.x << deadPos.y << deadPos.z;
        }

        if (qEnvironmentVariableIsSet("KRS_AUTOPLAY") && (s_frames % 120) == 0) {
            int inside = 0, outside = 0, sdfPenetrating = 0;
            float maxYIn = 0.0f; bool nan = false;

            auto sdfDistance = [this](const glm::vec3& p) {
                float best = 1e9f;
                for (const auto& s : m_sdfColliders) {
                    if (glm::any(glm::lessThan(p, s.aabbMin)) ||
                        glm::any(glm::greaterThan(p, s.aabbMax))) continue;
                    const glm::vec3 uvw = (p - s.aabbMin) / (s.aabbMax - s.aabbMin);
                    const glm::ivec3 c = glm::clamp(glm::ivec3(uvw * glm::vec3(s.dims - glm::ivec3(1)) + 0.5f),
                                                    glm::ivec3(0), s.dims - glm::ivec3(1));
                    best = std::min(best, s.cpuField[(size_t(c.z) * s.dims.y + c.y) * s.dims.x + c.x]);
                }
                return best;
            };

            for (const auto& q : m_positionMirror) {
                if (q.w <= 0.0f) continue;
                const glm::vec3 pp(q);
                if (glm::any(glm::isnan(pp))) { nan = true; continue; }
                const bool inGlass = std::abs(pp.x + 2.0f) < 0.62f && std::abs(pp.z) < 0.62f;
                if (inGlass) { ++inside; maxYIn = std::max(maxYIn, pp.y); }
                else ++outside;
                if (!m_sdfColliders.empty() && sdfDistance(pp) < -2.0f * m_params.particleRadius)
                    ++sdfPenetrating;
            }
            qInfo() << "[Fluid][autoplay] t=" << s_frames << "inGlass" << inside
                    << "out" << outside << "maxY(in)" << maxYIn
                    << "sdfPenetrating" << sdfPenetrating << (nan ? "NAN" : "");
        }
    }
}
