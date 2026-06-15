#include "MpmSystem.hpp"
#include "RenderingSystem.hpp"
#include "SmokeSystem.hpp"
#include "Shader.hpp"
#include "components.hpp"
#include "HardwareCaps.hpp"

#include <QOpenGLFunctions_4_3_Core>
#include <QDebug>
#include <QString>
#include <algorithm>
#include <cmath>
#include <random>
#include <string>

// Particle SSBO layout (std430), 11 vec4 per particle — keep in sync with
// the MLS-MPM compute shaders:
//   v0 pos.xyz, mass        v1 vel.xyz, V0
//   v2..v4 C rows (APIC)    v5..v7 F rows (deformation gradient)
//   v8 Jp, temperature, heatCapacity, meltTemp
//   v9 mu, lambda, frictionAlpha, materialType
//   v10 color.rgb, alive
namespace {
constexpr int kStride = MpmSystem::kFloatsPerParticle; // 48 floats (12 vec4)
constexpr int kLocal = 64;
}

void MpmSystem::initialize(RenderingSystem& renderer, QOpenGLFunctions_4_3_Core* gl)
{
    Q_UNUSED(renderer);
    if (m_initialized) return;
    m_N = krs::hardwareCaps().cudaPhysics ? 96 : 64;
    bool ok = false;
    const int envN = qEnvironmentVariable("KRS_MPM_GRID").toInt(&ok);
    if (ok && envN >= 32 && envN <= 160) m_N = (envN / 4) * 4;
    // Thermodynamic field overrides (M4). Ambient temperature + Newton
    // exchange rate let a hot environment melt solids; default = inert ambient.
    if (qEnvironmentVariableIsSet("KRS_MPM_AMBIENT"))
        m_ambientT = qEnvironmentVariable("KRS_MPM_AMBIENT").toFloat();
    if (qEnvironmentVariableIsSet("KRS_MPM_HEATX"))
        m_heatExchange = qEnvironmentVariable("KRS_MPM_HEATX").toFloat();
    if (qEnvironmentVariableIsSet("KRS_MPM_COND_SCALE"))
        m_conductionScale = qEnvironmentVariable("KRS_MPM_COND_SCALE").toFloat();
    // Initial visualization mode (1 Thermal, 2 VonMises, 3 Strain) for headless grabs.
    const int viz = qEnvironmentVariable("KRS_MPM_VIZ").toInt();
    if (viz >= 1 && viz <= 3) { m_appearance.mode = VizMode(viz); m_calibratePending = true; }
    allocate(gl);
    m_initialized = true;
    qInfo() << "[MPM] initialized grid" << m_N << "^3, capacity" << kMaxParticles;
}

void MpmSystem::allocate(QOpenGLFunctions_4_3_Core* gl)
{
    gl->glGenBuffers(1, &m_particleSSBO);
    gl->glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_particleSSBO);
    gl->glBufferData(GL_SHADER_STORAGE_BUFFER,
                     GLsizeiptr(sizeof(float)) * kStride * kMaxParticles, nullptr, GL_DYNAMIC_DRAW);

    const int cells = m_N * m_N * m_N;
    gl->glGenBuffers(1, &m_gridIntSSBO);
    gl->glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_gridIntSSBO);
    gl->glBufferData(GL_SHADER_STORAGE_BUFFER, GLsizeiptr(sizeof(GLint)) * cells * 4, nullptr,
                     GL_DYNAMIC_DRAW);
    gl->glGenBuffers(1, &m_gridVelSSBO);
    gl->glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_gridVelSSBO);
    gl->glBufferData(GL_SHADER_STORAGE_BUFFER, GLsizeiptr(sizeof(float)) * cells * 4, nullptr,
                     GL_DYNAMIC_DRAW);
    // Thermal grid: fixed-point (energy, m*c_p, m*c_p*k) scatter + temperature
    // ping-pong + per-node thermal mass C and conductivity k for Fourier conduction.
    gl->glGenBuffers(1, &m_gridThermSSBO);
    gl->glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_gridThermSSBO);
    gl->glBufferData(GL_SHADER_STORAGE_BUFFER, GLsizeiptr(sizeof(GLint)) * cells * 3, nullptr,
                     GL_DYNAMIC_DRAW);
    gl->glGenBuffers(1, &m_gridTempA);
    gl->glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_gridTempA);
    gl->glBufferData(GL_SHADER_STORAGE_BUFFER, GLsizeiptr(sizeof(float)) * cells, nullptr,
                     GL_DYNAMIC_DRAW);
    gl->glGenBuffers(1, &m_gridTempB);
    gl->glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_gridTempB);
    gl->glBufferData(GL_SHADER_STORAGE_BUFFER, GLsizeiptr(sizeof(float)) * cells, nullptr,
                     GL_DYNAMIC_DRAW);
    gl->glGenBuffers(1, &m_gridC);
    gl->glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_gridC);
    gl->glBufferData(GL_SHADER_STORAGE_BUFFER, GLsizeiptr(sizeof(float)) * cells, nullptr,
                     GL_DYNAMIC_DRAW);
    gl->glGenBuffers(1, &m_gridK);
    gl->glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_gridK);
    gl->glBufferData(GL_SHADER_STORAGE_BUFFER, GLsizeiptr(sizeof(float)) * cells, nullptr,
                     GL_DYNAMIC_DRAW);
    // Per-source thermal-mass accumulator: scatter sums sum(m*c_p) of particles in
    // each heat source's radius so the gather can inject a mass-weighted dT (Watts).
    gl->glGenBuffers(1, &m_heatAccumSSBO);
    gl->glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_heatAccumSSBO);
    gl->glBufferData(GL_SHADER_STORAGE_BUFFER, GLsizeiptr(sizeof(GLint)) * kMaxHeatSources, nullptr,
                     GL_DYNAMIC_DRAW);
    gl->glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
}

void MpmSystem::shutdown(QOpenGLFunctions_4_3_Core* gl)
{
    if (!m_initialized) return;
    gl->glDeleteBuffers(1, &m_particleSSBO);
    gl->glDeleteBuffers(1, &m_gridIntSSBO);
    gl->glDeleteBuffers(1, &m_gridVelSSBO);
    gl->glDeleteBuffers(1, &m_gridThermSSBO);
    gl->glDeleteBuffers(1, &m_gridTempA);
    gl->glDeleteBuffers(1, &m_gridTempB);
    gl->glDeleteBuffers(1, &m_gridC);
    gl->glDeleteBuffers(1, &m_gridK);
    gl->glDeleteBuffers(1, &m_heatAccumSSBO);
    m_initialized = false;
}

void MpmSystem::computeDomain(entt::registry& registry)
{
    glm::vec3 lo(1e9f), hi(-1e9f);
    int n = 0;
    for (auto e : registry.view<MpmBodyComponent, TransformComponent>()) {
        const auto& b = registry.get<MpmBodyComponent>(e);
        const auto& xf = registry.get<TransformComponent>(e);
        const glm::vec3 he = b.halfExtents * xf.scale;
        lo = glm::min(lo, xf.translation - he);
        hi = glm::max(hi, xf.translation + he);
        ++n;
    }
    if (n == 0) return;
    // Cubic domain sized to hold the bodies plus generous room to spread/fall.
    const glm::vec3 span = hi - lo;
    const glm::vec3 center = 0.5f * (lo + hi);
    float ext = std::max({ span.x, span.y, span.z, hi.y }) + 2.4f;
    m_size = glm::vec3(ext);
    // Anchor the grid floor (2-cell BC band) at the world ground plane y=0, so
    // material rests on the visible floor rather than floating in the grid.
    const float dx = ext / float(m_N);
    m_origin = glm::vec3(center.x - 0.5f * ext, -2.0f * dx, center.z - 0.5f * ext);
}

void MpmSystem::seedBodies(QOpenGLFunctions_4_3_Core* gl, entt::registry& registry)
{
    computeDomain(registry);
    m_seedScratch.clear();
    int count = 0;

    auto pushParticle = [&](const glm::vec3& p, const MpmBodyComponent& b) {
        if (count >= kMaxParticles) return;
        const float vol = b.particleSpacing * b.particleSpacing * b.particleSpacing;
        const float mass = b.density * vol;
        const float mu = b.youngsModulus / (2.0f * (1.0f + b.poissonRatio));
        const float lambda = b.youngsModulus * b.poissonRatio
                           / ((1.0f + b.poissonRatio) * (1.0f - 2.0f * b.poissonRatio));
        const float sphi = std::sin(glm::radians(b.frictionDegrees));
        const float alpha = std::sqrt(2.0f / 3.0f) * (2.0f * sphi) / (3.0f - sphi);
        const float meltT = b.meltTemperature;

        float v[kStride] = { 0 };
        v[0] = p.x; v[1] = p.y; v[2] = p.z; v[3] = mass;             // pos, mass
        v[4] = b.initialVelocity.x; v[5] = b.initialVelocity.y;
        v[6] = b.initialVelocity.z; v[7] = vol;                     // vel, V0
        // C columns (8..19) = 0
        // F columns (20..31) = identity
        v[20] = 1.0f; v[25] = 1.0f; v[30] = 1.0f;
        v[32] = 1.0f;                                               // Jp (fluid: J)
        v[33] = b.temperature; v[34] = b.heatCapacity; v[35] = meltT; // temp, Cp (J/kg.K), meltT
        // matl: solids store (mu, lambda, frictionAlpha); fluid stores
        // (bulkK, gamma) so the shader can pick an EOS instead of Lame.
        if (b.material == MpmMaterial::Fluid) {
            v[36] = b.youngsModulus; // bulk modulus K (set softly for explicit stability)
            v[37] = 7.0f;            // Tait gamma
            v[38] = b.viscosity;     // dynamic viscosity (Pa·s)
        } else {
            v[36] = mu; v[37] = lambda; v[38] = alpha;
        }
        v[39] = float(int(b.material));
        v[40] = b.color.r; v[41] = b.color.g; v[42] = b.color.b; v[43] = 1.0f; // color, alive
        v[44] = b.thermalConductivity;                              // therm2.x = k (W/m.K)
        m_seedScratch.insert(m_seedScratch.end(), v, v + kStride);
        ++count;
    };

    std::mt19937 rng{ 9281u };
    std::uniform_real_distribution<float> jit(-0.25f, 0.25f);
    float minSpacing = 0.04f;
    for (auto e : registry.view<MpmBodyComponent>())
        minSpacing = std::min(minSpacing, std::max(registry.get<MpmBodyComponent>(e).particleSpacing, 0.01f));
    // Render splat radius = particle spacing dx (Phase 5 Task 1): splats overlap so
    // the body reads as a coherent SURFACE (not a sparse spring-net), and this same
    // radius is the floor-contact offset so the visual matches the collision.
    m_renderRadius = minSpacing;
    for (auto e : registry.view<MpmBodyComponent, TransformComponent>()) {
        const auto& b = registry.get<MpmBodyComponent>(e);
        const auto& xf = registry.get<TransformComponent>(e);
        const glm::vec3 he = b.halfExtents * xf.scale;
        const float s = std::max(b.particleSpacing, 0.01f);
        const glm::vec3 lo = xf.translation - he;
        const glm::ivec3 res = glm::max(glm::ivec3(glm::ceil((he * 2.0f) / s)), glm::ivec3(1));
        for (int ix = 0; ix < res.x; ++ix)
            for (int iy = 0; iy < res.y; ++iy)
                for (int iz = 0; iz < res.z; ++iz) {
                    glm::vec3 p = lo + (glm::vec3(ix, iy, iz) + 0.5f) * s;
                    p += glm::vec3(jit(rng), jit(rng), jit(rng)) * s; // break the lattice
                    pushParticle(p, b);
                }
    }

    // Max elastic wave speed c = sqrt(stiffness/density) across all bodies,
    // for the explicit-MPM CFL substep count.
    m_maxWaveSpeed = 1.0f;
    bool anyMeltable = false;
    for (auto e : registry.view<MpmBodyComponent>()) {
        const auto& b = registry.get<MpmBodyComponent>(e);
        // Fluid uses a Tait EOS whose effective bulk stiffness near J=1 is
        // K*gamma (dp/dJ = -K*gamma), so the sound speed is sqrt(K*gamma/rho).
        const float stiff = (b.material == MpmMaterial::Fluid)
                                ? b.youngsModulus * 7.0f : b.youngsModulus;
        const float c = std::sqrt(std::max(stiff, 1.0f) / std::max(b.density, 1.0f));
        m_maxWaveSpeed = std::max(m_maxWaveSpeed, c);
        // A meltable solid becomes fluid mid-sim; its post-melt Tait stiffness
        // governs the CFL once it phase-changes, so account for it up front.
        if (b.meltTemperature < 1.0e8f) {
            anyMeltable = true;
            const float cf = std::sqrt(m_fluidMeltK * 7.0f / std::max(b.density, 1.0f));
            m_maxWaveSpeed = std::max(m_maxWaveSpeed, cf);
        }
    }
    // If the scene contains a meltable body and the user hasn't configured a
    // heat source, switch on a warm ambient so phase change is actually driven.
    if (anyMeltable && m_heatExchange <= 0.0f) {
        m_heatExchange = 2.5f;
        if (std::abs(m_ambientT - 20.0f) < 0.5f) m_ambientT = 45.0f;
    }

    m_particleCount = count;
    if (count > 0) {
        gl->glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_particleSSBO);
        gl->glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0,
                            GLsizeiptr(sizeof(float)) * m_seedScratch.size(), m_seedScratch.data());
        gl->glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
    }
    qInfo() << "[MPM] seeded" << count << "particles; domain origin" << m_origin.x << m_origin.y
            << m_origin.z << "size" << m_size.x << "waveSpeed" << m_maxWaveSpeed;
}

void MpmSystem::update(RenderingSystem& renderer, QOpenGLFunctions_4_3_Core* gl,
                       entt::registry& registry, float dt)
{
    if (!m_initialized) return;
    // Seed MPM bodies on the first update REGARDLESS of play state, so the
    // particles render (and the Visualize dropdown can recolour them) immediately
    // at boot while paused -- not only after Play. setSimulationPlaying() is
    // event-driven (fires on state change, never per-frame), so this runs once.
    if (!m_seeded) {
        seedBodies(gl, registry);
        m_seeded = true;
    }
    // Paused mode-switch: recalibrate the colour range against the static state.
    if (m_calibratePending && !m_playing && m_particleCount > 0) { autoCalibrate(gl, false); m_calibratePending = false; }
    if (!m_playing) return;
    if (m_particleCount <= 0) return;
    // Dynamic normalization: recalibrate the active viz range on mode change and
    // periodically (~0.75 s) so the gradient tracks the live, evolving field.
    if (m_appearance.mode != VizMode::Default && m_appearance.autoRange
        && (m_calibratePending || (m_vizFrame % 45 == 0))) {
        autoCalibrate(gl, /*smooth=*/!m_calibratePending);   // mode-switch snaps; periodic EMA-smooths
        m_calibratePending = false;
    }
    ++m_vizFrame;

    Shader* p2g = renderer.getShader("mpm_p2g");
    Shader* grid = renderer.getShader("mpm_grid");
    Shader* g2p = renderer.getShader("mpm_g2p");
    if (!p2g || !grid || !g2p) return;

    const float dx = m_size.x / float(m_N);
    const float invDx = float(m_N) / m_size.x;

    // Adaptive CFL: a node should not see material move more than ~0.4 cells
    // per substep. dt < CFL * dx / c, c = max wave speed.
    const float frameDt = glm::clamp(dt, 1.0e-4f, 1.0f / 30.0f);
    const float cflDt = 0.35f * dx / std::max(m_maxWaveSpeed, 1.0f);
    int substeps = int(std::ceil(frameDt / std::max(cflDt, 1.0e-5f)));
    substeps = std::clamp(substeps, 1, 50);
    const float sdt = frameDt / float(substeps);

    for (int s = 0; s < substeps; ++s)
        runSubstep(gl, p2g, grid, g2p, sdt, m_gravity, dx, invDx);

    gl->glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, 0);
    gl->glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, 0);
    gl->glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, 0);

    // Heat diffusion + phase change, once per rendered frame (heat evolves far
    // slower than momentum, so a single thermal step per frame is plenty).
    collectHeatSources(registry);
    runThermalStep(renderer, gl, frameDt);
}

void MpmSystem::collectHeatSources(entt::registry& registry)
{
    m_heatCount = 0;
    for (auto e : registry.view<HeatSourceComponent, TransformComponent>()) {
        const auto& hs = registry.get<HeatSourceComponent>(e);
        if (!hs.active) continue;
        const auto& xf = registry.get<TransformComponent>(e);
        if (m_heatCount < kMaxHeatSources) {
            m_heatSrc[m_heatCount] = glm::vec4(xf.translation, hs.temperature); // pos + nominal T
            m_heatRadius[m_heatCount] = hs.radius;
            m_heatPower[m_heatCount] = hs.power;                                // W (Neumann)
            ++m_heatCount;
        }
        // A hot rigid body glows: drive its emissive from the source temperature
        // (reuses the existing emissive G-buffer path — no shader-variant churn).
        if (auto* mat = registry.try_get<MaterialComponent>(e)) {
            const float t = glm::clamp((hs.temperature - 20.0f) / 200.0f, 0.0f, 1.0f);
            mat->emissiveColor = glm::mix(glm::vec3(0.02f), glm::vec3(1.0f, 0.35f, 0.08f), t);
            mat->emissiveStrength = t * 3.0f;
        }
    }
}

void MpmSystem::runThermalStep(RenderingSystem& renderer, QOpenGLFunctions_4_3_Core* gl,
                               float dtFrame)
{
    if (m_particleCount <= 0) return;
    Shader* scat = renderer.getShader("mpm_heat_scatter");
    Shader* norm = renderer.getShader("mpm_heat_normalize");
    Shader* diff = renderer.getShader("mpm_heat_diffuse");
    Shader* gath = renderer.getShader("mpm_heat_gather");
    if (!scat || !norm || !diff || !gath) return;

    const float dx = m_size.x / float(m_N);
    const float invDx = float(m_N) / m_size.x;
    const int cells = m_N * m_N * m_N;
    const int pGroups = (m_particleCount + kLocal - 1) / kLocal;
    const int cGroups = (cells + kLocal - 1) / kLocal;
    const GLint zero = 0;
    // Fourier conduction coefficient u_coef = S*dt*dx; the per-cell stability clamp
    // (u_betaMax) inside the diffuse shader bounds the explicit sweep for any S.
    const float uCoef = m_conductionScale * dtFrame * dx;
    const float uBetaMax = 0.5f;

    gl->glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_gridThermSSBO);
    gl->glClearBufferData(GL_SHADER_STORAGE_BUFFER, GL_R32I, GL_RED_INTEGER, GL_INT, &zero);
    gl->glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
    gl->glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_heatAccumSSBO);   // per-source sum(m*c_p)
    gl->glClearBufferData(GL_SHADER_STORAGE_BUFFER, GL_R32I, GL_RED_INTEGER, GL_INT, &zero);
    gl->glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

    gl->glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, m_particleSSBO);
    gl->glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, m_gridThermSSBO);
    gl->glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 4, m_gridTempA);
    gl->glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 5, m_gridTempB);
    gl->glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 6, m_heatAccumSSBO);
    gl->glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 7, m_gridC);
    gl->glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 8, m_gridK);

    scat->use(gl);
    scat->setInt(gl, "u_count", m_particleCount);
    scat->setInt(gl, "u_N", m_N);
    scat->setVec3(gl, "u_origin", m_origin);
    scat->setFloat(gl, "u_invDx", invDx);
    // Heat sources also given to scatter so it can sum sum(m*c_p) per source.
    scat->setInt(gl, "u_heatCount", m_heatCount);
    for (int h = 0; h < m_heatCount; ++h) {
        scat->setVec4(gl, ("u_heatSrc[" + std::to_string(h) + "]").c_str(), m_heatSrc[h]);
        scat->setFloat(gl, ("u_heatRadius[" + std::to_string(h) + "]").c_str(), m_heatRadius[h]);
    }
    gl->glDispatchCompute(pGroups, 1, 1);
    gl->glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

    norm->use(gl);
    norm->setInt(gl, "u_N", m_N);
    gl->glDispatchCompute(cGroups, 1, 1);
    gl->glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

    diff->use(gl);
    diff->setInt(gl, "u_N", m_N);
    diff->setFloat(gl, "u_coef", uCoef);
    diff->setFloat(gl, "u_betaMax", uBetaMax);
    gl->glDispatchCompute(cGroups, 1, 1);
    gl->glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

    gath->use(gl);
    gath->setInt(gl, "u_count", m_particleCount);
    gath->setInt(gl, "u_N", m_N);
    gath->setVec3(gl, "u_origin", m_origin);
    gath->setFloat(gl, "u_invDx", invDx);
    gath->setFloat(gl, "u_ambientT", m_ambientT);
    gath->setFloat(gl, "u_heatExchange", m_heatExchange);
    gath->setFloat(gl, "u_dtFrame", dtFrame);
    gath->setFloat(gl, "u_fluidK", m_fluidMeltK);
    // Heat sources (HeatSourceComponent), collected in update(). Volumetric power
    // (Watts) is injected as dT = power*dt / sum(m*c_p) over particles in radius.
    gath->setInt(gl, "u_heatCount", m_heatCount);
    for (int h = 0; h < m_heatCount; ++h) {
        gath->setVec4(gl, ("u_heatSrc[" + std::to_string(h) + "]").c_str(), m_heatSrc[h]);
        gath->setFloat(gl, ("u_heatRadius[" + std::to_string(h) + "]").c_str(), m_heatRadius[h]);
        gath->setFloat(gl, ("u_heatPower[" + std::to_string(h) + "]").c_str(), m_heatPower[h]);
    }
    // Flame/smoke grid coupling: sample the smoke temperature field where it
    // overlaps the MPM domain so a flame scorches the material.
    SmokeSystem* smoke = renderer.getSmokeSystem();
    int smokeOn = (smoke && smoke->scalarsTexture()) ? 1 : 0;
    if (smokeOn) {
        gl->glActiveTexture(GL_TEXTURE0 + 8);
        gl->glBindTexture(GL_TEXTURE_3D, smoke->scalarsTexture());
        gath->setInt(gl, "u_smokeScalars", 8);
        gath->setVec3(gl, "u_smokeOrigin", smoke->origin());
        gath->setVec3(gl, "u_smokeSize", smoke->extent());
        gath->setFloat(gl, "u_smokeTempC", 600.0f);   // flame (g=1) ~ 600 C
        gath->setFloat(gl, "u_smokeRate", 5.0f);
        gl->glMemoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT); // smoke wrote it earlier this frame
    }
    gath->setInt(gl, "u_smokeOn", smokeOn);
    gl->glDispatchCompute(pGroups, 1, 1);
    gl->glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

    gl->glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, 0);
    gl->glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, 0);
    gl->glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 4, 0);
    gl->glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 5, 0);
    gl->glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 6, 0);
    gl->glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 7, 0);
    gl->glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 8, 0);
}

void MpmSystem::runSubstep(QOpenGLFunctions_4_3_Core* gl, Shader* p2g, Shader* grid,
                           Shader* g2p, float sdt, const glm::vec3& gravity,
                           float dx, float invDx)
{
    const int cells = m_N * m_N * m_N;
    const int pGroups = (m_particleCount + kLocal - 1) / kLocal;
    const int cGroups = (cells + kLocal - 1) / kLocal;
    const GLint zero = 0;

    // Clear the fixed-point grid accumulator (mass + momentum).
    gl->glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_gridIntSSBO);
    gl->glClearBufferData(GL_SHADER_STORAGE_BUFFER, GL_R32I, GL_RED_INTEGER, GL_INT, &zero);
    gl->glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

    gl->glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, m_particleSSBO);
    gl->glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, m_gridIntSSBO);
    gl->glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, m_gridVelSSBO);

    // --- P2G ---
    p2g->use(gl);
    p2g->setInt(gl, "u_count", m_particleCount);
    p2g->setInt(gl, "u_N", m_N);
    p2g->setVec3(gl, "u_origin", m_origin);
    p2g->setFloat(gl, "u_dx", dx);
    p2g->setFloat(gl, "u_invDx", invDx);
    p2g->setFloat(gl, "u_dt", sdt);
    gl->glDispatchCompute(pGroups, 1, 1);
    gl->glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

    // --- grid update ---
    grid->use(gl);
    grid->setInt(gl, "u_N", m_N);
    grid->setFloat(gl, "u_dt", sdt);
    grid->setVec3(gl, "u_gravity", gravity);
    grid->setInt(gl, "u_bound", 2);
    grid->setFloat(gl, "u_floorFriction", m_floorFriction);
    gl->glDispatchCompute(cGroups, 1, 1);
    gl->glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

    // --- G2P ---
    g2p->use(gl);
    g2p->setInt(gl, "u_count", m_particleCount);
    g2p->setInt(gl, "u_N", m_N);
    g2p->setVec3(gl, "u_origin", m_origin);
    g2p->setFloat(gl, "u_dx", dx);
    g2p->setFloat(gl, "u_invDx", invDx);
    g2p->setFloat(gl, "u_dt", sdt);
    g2p->setFloat(gl, "u_thetaC", 0.025f);
    g2p->setFloat(gl, "u_thetaS", 0.0075f);
    // Floor plane = origin.y + bound*dx (the no-penetration band top, world y=0 in
    // the live domain); offset particle centres by the effective radius so surfaces rest on it.
    g2p->setFloat(gl, "u_floorY", m_origin.y + 2.0f * dx);
    g2p->setFloat(gl, "u_radius", m_renderRadius);
    gl->glDispatchCompute(pGroups, 1, 1);
    gl->glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
}

bool MpmSystem::runSelfTests(RenderingSystem& renderer, QOpenGLFunctions_4_3_Core* gl)
{
    Shader* p2g = renderer.getShader("mpm_p2g");
    Shader* grid = renderer.getShader("mpm_grid");
    Shader* g2p = renderer.getShader("mpm_g2p");
    if (!p2g || !grid || !g2p) { qWarning() << "[MPM selftest] shaders missing"; return false; }

    const int savedN = m_N;
    m_N = std::min(m_N, 64);
    m_origin = glm::vec3(-1.5f);
    m_size = glm::vec3(3.0f);

    auto seedBlock = [&](int material, glm::vec3 center, float half, float spacing,
                         float density, float E, float nu, glm::vec3 v0,
                         float T0, float meltT, float tempSpanX,
                         float conductivity = 50.0f, bool append = false) {
        if (!append) { m_seedScratch.clear(); m_particleCount = 0; }
        int count = 0;
        const float vol = spacing * spacing * spacing;
        const float mass = density * vol;
        const float mu = E / (2.0f * (1.0f + nu));
        const float lambda = E * nu / ((1.0f + nu) * (1.0f - 2.0f * nu));
        const float sphi = std::sin(glm::radians(35.0f));
        const float alpha = std::sqrt(2.0f / 3.0f) * (2.0f * sphi) / (3.0f - sphi);
        const int n = std::max(1, int(std::round(2.0f * half / spacing)));
        for (int ix = 0; ix < n; ++ix)
            for (int iy = 0; iy < n; ++iy)
                for (int iz = 0; iz < n; ++iz) {
                    glm::vec3 pp = center - glm::vec3(half) + (glm::vec3(ix, iy, iz) + 0.5f) * spacing;
                    const float nx = (2.0f * half > 1e-6f) ? (pp.x - (center.x - half)) / (2.0f * half) : 0.0f;
                    const float T = T0 + tempSpanX * nx;
                    float v[kStride] = { 0 };
                    v[0] = pp.x; v[1] = pp.y; v[2] = pp.z; v[3] = mass;
                    v[4] = v0.x; v[5] = v0.y; v[6] = v0.z; v[7] = vol;
                    v[20] = 1.0f; v[25] = 1.0f; v[30] = 1.0f; v[32] = 1.0f;
                    v[33] = T; v[34] = 900.0f; v[35] = meltT;
                    if (material == 0) { v[36] = E; v[37] = 7.0f; v[38] = 0.0f; }
                    else { v[36] = mu; v[37] = lambda; v[38] = alpha; }
                    v[39] = float(material);
                    v[40] = 0.6f; v[41] = 0.7f; v[42] = 0.9f; v[43] = 1.0f;
                    v[44] = conductivity;                       // therm2.x = k (W/m.K)
                    m_seedScratch.insert(m_seedScratch.end(), v, v + kStride);
                    ++count;
                }
        m_particleCount += count;
        gl->glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_particleSSBO);
        gl->glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0,
                            GLsizeiptr(sizeof(float)) * m_seedScratch.size(), m_seedScratch.data());
        gl->glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
        const float stiff = (material == 0) ? E * 7.0f : E; // fluid: K*gamma
        m_maxWaveSpeed = std::max(append ? m_maxWaveSpeed : 1.0f, std::sqrt(stiff / density));
    };

    auto runFor = [&](float seconds, glm::vec3 gravity) {
        const float dx = m_size.x / float(m_N);
        const float invDx = float(m_N) / m_size.x;
        const float cflDt = 0.35f * dx / std::max(m_maxWaveSpeed, 1.0f);
        int sub = std::clamp(int(std::ceil(seconds / std::max(cflDt, 1.0e-5f))), 1, 6000);
        const float sdt = seconds / float(sub);
        for (int s = 0; s < sub; ++s) runSubstep(gl, p2g, grid, g2p, sdt, gravity, dx, invDx);
        gl->glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, 0);
        gl->glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, 0);
        gl->glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, 0);
    };

    bool allPass = true;
    auto check = [&](const char* name, bool cond, const std::string& detail) {
        qInfo().noquote() << QString("[MPM selftest] %1  %2  (%3)")
                                 .arg(cond ? "PASS" : "FAIL", -5).arg(name).arg(QString::fromStdString(detail));
        allPass = allPass && cond;
    };
    auto fmt = [](const char* k, double v) { return std::string(k) + "=" + std::to_string(v); };

    // Test 1 — free-fall kinematics (validates APIC momentum transfer + integrator).
    {
        seedBlock(0, glm::vec3(0, 0, 0), 0.2f, 0.05f, 1000.0f, 5.0e4f, 0.0f, glm::vec3(0), 20.0f, 1.0e9f, 0.0f);
        Diag d0 = sample(gl);
        const float T = 0.12f;
        runFor(T, glm::vec3(0, -9.81f, 0));
        Diag d1 = sample(gl);
        const float expV = 9.81f * T;
        const float expDrop = 0.5f * 9.81f * T * T;
        const float vErr = std::abs(float(d1.comVelocity.y) + expV) / expV;
        const float dropErr = std::abs(float(d0.comPosition.y - d1.comPosition.y) - expDrop) / expDrop;
        const float massErr = std::abs(d1.totalMass - d0.totalMass) / std::max(d0.totalMass, 1e-9);
        check("free-fall velocity = g*t", vErr < 0.05f, fmt("relErr", vErr));
        check("free-fall drop = 0.5*g*t^2", dropErr < 0.10f, fmt("relErr", dropErr));
        check("mass conserved (fall)", massErr < 1e-3f, fmt("relErr", massErr));
    }
    // Test 2 — weakly-compressible fluid settles in the box without blowing up.
    {
        seedBlock(0, glm::vec3(0, -0.6f, 0), 0.3f, 0.06f, 1000.0f, 5.0e4f, 0.0f, glm::vec3(0), 20.0f, 1.0e9f, 0.0f);
        Diag d0 = sample(gl);
        runFor(1.0f, glm::vec3(0, -9.81f, 0));
        Diag d1 = sample(gl);
        const bool finite = std::isfinite(d1.maxSpeed) && std::isfinite(float(d1.totalMass));
        const float massErr = std::abs(d1.totalMass - d0.totalMass) / std::max(d0.totalMass, 1e-9);
        check("fluid rest stable (bounded)", finite && d1.maxSpeed < 25.0f, fmt("maxSpeed", d1.maxSpeed));
        check("fluid rest mass conserved", massErr < 1e-3f, fmt("relErr", massErr));
    }
    // Test 3 — elastic block (Neo-Hookean) stays bounded and conserves mass.
    {
        seedBlock(1, glm::vec3(0, -0.4f, 0), 0.25f, 0.05f, 1000.0f, 1.0e5f, 0.3f, glm::vec3(0), 20.0f, 1.0e9f, 0.0f);
        Diag d0 = sample(gl);
        runFor(1.0f, glm::vec3(0, -9.81f, 0));
        Diag d1 = sample(gl);
        const float massErr = std::abs(d1.totalMass - d0.totalMass) / std::max(d0.totalMass, 1e-9);
        check("elastic stable (bounded)", std::isfinite(d1.maxSpeed) && d1.maxSpeed < 25.0f,
              fmt("maxSpeed", d1.maxSpeed));
        check("elastic mass conserved", massErr < 1e-3f, fmt("relErr", massErr));
    }

    // Test 4 — sand column (Drucker-Prager) collapses, stays bounded, and
    // spreads wider than it started (granular flow, not a rigid drop).
    {
        seedBlock(2, glm::vec3(0, -0.5f, 0), 0.22f, 0.045f, 1600.0f, 6.0e5f, 0.3f, glm::vec3(0), 20.0f, 1.0e9f, 0.0f);
        Diag d0 = sample(gl);
        // Measure initial horizontal spread (std-dev proxy via re-reading buffer).
        runFor(1.2f, glm::vec3(0, -9.81f, 0));
        Diag d1 = sample(gl);
        const float massErr = std::abs(d1.totalMass - d0.totalMass) / std::max(d0.totalMass, 1e-9);
        check("sand stable (bounded)", std::isfinite(d1.maxSpeed) && d1.maxSpeed < 25.0f,
              fmt("maxSpeed", d1.maxSpeed));
        check("sand mass conserved", massErr < 1e-3f, fmt("relErr", massErr));
        check("sand settles (COM drops)", d1.comPosition.y < d0.comPosition.y - 0.02,
              fmt("dropY", d0.comPosition.y - d1.comPosition.y));
    }

    // Test 5 — pure heat diffusion. A fluid block with a 20->80C gradient along
    // x, no gravity stepping and no ambient exchange: total heat is conserved
    // while the spread collapses toward the mean (Fourier diffusion).
    {
        const float savedAmb = m_ambientT, savedHx = m_heatExchange;
        m_ambientT = 50.0f; m_heatExchange = 0.0f;
        seedBlock(0, glm::vec3(0, -0.3f, 0), 0.3f, 0.05f, 1000.0f, 5.0e4f, 0.0f,
                  glm::vec3(0), 20.0f, 1.0e9f, 60.0f);
        Diag d0 = sample(gl);
        for (int f = 0; f < 200; ++f) runThermalStep(renderer, gl, 1.0f / 60.0f);
        Diag d1 = sample(gl);
        const float spread0 = d0.tempMax - d0.tempMin;
        const float spread1 = d1.tempMax - d1.tempMin;
        const float meanErr = std::abs(float(d1.tempMean - d0.tempMean));
        check("heat diffuses (spread shrinks)", spread1 < 0.8f * spread0,
              fmt("spread0", spread0) + " " + fmt("spread1", spread1));
        check("heat conserved (mean stable)", meanErr < 6.0f, fmt("meanErr", meanErr));
        m_ambientT = savedAmb; m_heatExchange = savedHx;
    }
    // Test 6 — phase change. An elastic block (melt point 30C, starts at 15C)
    // sits in a 95C ambient with Newton exchange: it heats past its melt point
    // and the in-solver constitutive switch converts it to fluid.
    {
        const float savedAmb = m_ambientT, savedHx = m_heatExchange;
        m_ambientT = 95.0f; m_heatExchange = 5.0f;
        seedBlock(1, glm::vec3(0, -0.4f, 0), 0.22f, 0.05f, 1000.0f, 8.0e4f, 0.3f,
                  glm::vec3(0), 15.0f, 30.0f, 0.0f);
        m_maxWaveSpeed = std::max(m_maxWaveSpeed, std::sqrt(m_fluidMeltK * 7.0f / 1000.0f));
        Diag d0 = sample(gl);
        for (int f = 0; f < 120; ++f) {
            runFor(1.0f / 60.0f, glm::vec3(0, -9.81f, 0));
            runThermalStep(renderer, gl, 1.0f / 60.0f);
        }
        Diag d1 = sample(gl);
        check("phase change: starts solid (no fluid)", d0.fluidCount == 0,
              fmt("fluidCount0", d0.fluidCount));
        check("phase change: heated solid melts to fluid", d1.fluidCount > d1.live / 2,
              fmt("melted", d1.fluidCount) + " / " + fmt("live", d1.live));
        m_ambientT = savedAmb; m_heatExchange = savedHx;
    }
    // Test 7 — HeatSourceComponent injection + dissipation (Phase 3). A hot
    // source warms a solid body; removing it lets the heat dissipate to ambient.
    {
        const float savedAmb = m_ambientT, savedHx = m_heatExchange;
        m_ambientT = 20.0f; m_heatExchange = 0.5f;
        seedBlock(1, glm::vec3(0, -0.4f, 0), 0.22f, 0.05f, 1000.0f, 8.0e4f, 0.3f,
                  glm::vec3(0), 20.0f, 1.0e9f, 0.0f);
        // Volumetric heat-generation source (Watts / Neumann): 5 MW into the block.
        m_heatCount = 1; m_heatSrc[0] = glm::vec4(0, -0.4f, 0, 250.0f);
        m_heatRadius[0] = 0.6f; m_heatPower[0] = 5.0e6f;
        Diag d0 = sample(gl);
        for (int f = 0; f < 90; ++f) runThermalStep(renderer, gl, 1.0f / 60.0f);
        Diag dHot = sample(gl);
        m_heatCount = 0; m_heatPower[0] = 0.0f; m_heatExchange = 3.0f; // source off -> dissipate
        for (int f = 0; f < 180; ++f) runThermalStep(renderer, gl, 1.0f / 60.0f);
        Diag dCool = sample(gl);
        check("heat source warms body (Watts)", dHot.tempMean > d0.tempMean + 30.0,
              fmt("T0", d0.tempMean) + " -> " + fmt("Thot", dHot.tempMean));
        check("heat dissipates to ambient", dCool.tempMean < dHot.tempMean - 10.0,
              fmt("Thot", dHot.tempMean) + " -> " + fmt("Tcool", dCool.tempMean));
        m_heatCount = 0; m_ambientT = savedAmb; m_heatExchange = savedHx;
    }
    // Test 8 — grid Fourier conduction between two touching bodies of DIFFERENT k
    // (Phase 4.5). A hot copper-like block (k=400) abuts a cooler block (k=100);
    // with no ambient sink the heat conducts across the contact via the harmonic-
    // mean face conductivity, the temperature spread shrinks, and the mass-weighted
    // mean (energy) is conserved. A modest conduction scale keeps the explicit
    // sweep below the stability clamp so conservation is exact.
    {
        const float savedAmb = m_ambientT, savedHx = m_heatExchange, savedS = m_conductionScale;
        m_ambientT = 20.0f; m_heatExchange = 0.0f;     // pure conduction, no reservoir
        m_conductionScale = 18.0f;                     // no per-cell clamping -> exact energy
        seedBlock(1, glm::vec3(-0.15f, -0.3f, 0), 0.15f, 0.05f, 1000.0f, 5.0e4f, 0.0f,
                  glm::vec3(0), 80.0f, 1.0e9f, 0.0f, 400.0f, false);  // hot, high-k
        seedBlock(1, glm::vec3( 0.15f, -0.3f, 0), 0.15f, 0.05f, 1000.0f, 5.0e4f, 0.0f,
                  glm::vec3(0), 20.0f, 1.0e9f, 0.0f, 100.0f, true);   // cool, lower-k, touching
        Diag d0 = sample(gl);
        for (int f = 0; f < 300; ++f) runThermalStep(renderer, gl, 1.0f / 60.0f);
        Diag d1 = sample(gl);
        const float spread0 = d0.tempMax - d0.tempMin;
        const float spread1 = d1.tempMax - d1.tempMin;
        const float meanErr = std::abs(float(d1.tempMean - d0.tempMean));
        check("conduction across contact (spread shrinks)", spread1 < 0.8f * spread0,
              fmt("spread0", spread0) + " -> " + fmt("spread1", spread1));
        check("conduction conserves energy (mean stable)", meanErr < 6.0f, fmt("meanErr", meanErr));
        m_ambientT = savedAmb; m_heatExchange = savedHx; m_conductionScale = savedS;
    }
    // Test 9 — floor contact (Phase 5 Task 1). An elastic block dropped under gravity
    // must settle with its lowest particle CENTRE at floor + radius (no penetration);
    // the splat radius is the contact offset, so surfaces rest ON the floor.
    {
        const float savedR = m_renderRadius;
        const float spacing = 0.05f;
        m_renderRadius = spacing;                       // splat radius = dx (Task 1.3)
        const float dx = m_size.x / float(m_N);
        const float floorY = m_origin.y + 2.0f * dx;    // grid floor plane (bound=2)
        seedBlock(1, glm::vec3(0.0f, -0.8f, 0.0f), 0.2f, spacing, 2700.0f, 2.0e6f, 0.33f,
                  glm::vec3(0), 20.0f, 1.0e9f, 0.0f);
        runFor(2.0f, glm::vec3(0.0f, -9.81f, 0.0f));    // drop + settle
        Diag d = sample(gl);
        const float floorPlusR = floorY + m_renderRadius;
        check("floor: no penetration (minY >= floor + radius)", d.minY >= floorPlusR - 1.0e-3f,
              fmt("minY", d.minY) + " floor+r " + std::to_string(floorPlusR));
        check("floor: block actually settled near floor", d.minY <= floorPlusR + 4.0f * spacing,
              fmt("minY", d.minY) + " floor+r " + std::to_string(floorPlusR));
        m_renderRadius = savedR;
    }

    qInfo() << "[MPM selftest] overall:" << (allPass ? "ALL PASS" : "FAILURES PRESENT");
    // Restore empty state so the live scene seeds fresh from the registry.
    m_N = savedN;
    m_particleCount = 0;
    m_seeded = false;
    return allPass;
}

// Phase 1 GATE 1.4 -- MPM <-> THERMAL energy conservation. A fluid block with a 20->80C gradient
// conducts heat (Fourier): the mass-weighted total thermal energy (totalMass*c_p*tempMean, and c_p
// + mass are constant so tempMean IS the energy proxy) is CONSERVED while the spread shrinks toward
// the mean. NEG-CTRL A (injected non-conservation): turn on ambient exchange -> tempMean drifts ->
// the SAME conservation check is violated and caught. NEG-CTRL B (severed coupling): conduction
// scale = 0 -> no Fourier flux -> the spread does NOT shrink.
bool MpmSystem::runThermalGate1_4(RenderingSystem& renderer, QOpenGLFunctions_4_3_Core* gl)
{
    using std::printf;
    setvbuf(stdout, nullptr, _IONBF, 0);
    printf("[mpm-thermal] GATE 1.4 -- MPM thermal ENERGY conservation (Fourier conduction) + 2 neg-ctrls\n");
    if (!renderer.getShader("mpm_heat_diffuse") || !renderer.getShader("mpm_heat_gather")) {
        printf("[mpm-thermal] vacuous pass (heat shaders unavailable)\n");
        std::fflush(stdout); return true;
    }

    const int   savedN = m_N, savedCount = m_particleCount;
    const glm::vec3 savedOrigin = m_origin, savedSize = m_size;
    const float savedAmb = m_ambientT, savedHx = m_heatExchange, savedS = m_conductionScale;
    const float savedWave = m_maxWaveSpeed;
    m_N = std::min(m_N, 64); m_origin = glm::vec3(-1.5f); m_size = glm::vec3(3.0f);

    // seed a fluid block (material 0) with a T0..T0+span gradient along x (no motion: thermal only).
    auto seedGradient = [&]() {
        m_seedScratch.clear(); m_particleCount = 0;
        const float spacing = 0.05f, half = 0.3f, density = 1000.0f, T0 = 20.0f, span = 60.0f, k = 60.0f;
        const glm::vec3 center(0.0f, -0.3f, 0.0f);
        const float vol = spacing * spacing * spacing, mass = density * vol;
        const int n = std::max(1, int(std::round(2.0f * half / spacing)));
        int count = 0;
        for (int ix = 0; ix < n; ++ix) for (int iy = 0; iy < n; ++iy) for (int iz = 0; iz < n; ++iz) {
            const glm::vec3 pp = center - glm::vec3(half) + (glm::vec3(ix, iy, iz) + 0.5f) * spacing;
            const float nx = (pp.x - (center.x - half)) / (2.0f * half);
            float v[kStride] = { 0 };
            v[0] = pp.x; v[1] = pp.y; v[2] = pp.z; v[3] = mass;
            v[7] = vol; v[20] = 1.0f; v[25] = 1.0f; v[30] = 1.0f; v[32] = 1.0f;
            v[33] = T0 + span * nx; v[34] = 900.0f; v[35] = 1.0e9f;     // T, c_p, meltT
            v[36] = 5.0e4f; v[37] = 7.0f; v[38] = 0.0f;                 // fluid K, gamma
            v[39] = 0.0f; v[40] = 0.6f; v[41] = 0.7f; v[42] = 0.9f; v[43] = 1.0f; v[44] = k;
            m_seedScratch.insert(m_seedScratch.end(), v, v + kStride); ++count;
        }
        m_particleCount += count;
        gl->glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_particleSSBO);
        gl->glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0,
                            GLsizeiptr(sizeof(float)) * m_seedScratch.size(), m_seedScratch.data());
        gl->glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
    };
    auto runHeat = [&](int frames) { for (int f = 0; f < frames; ++f) runThermalStep(renderer, gl, 1.0f / 60.0f); };

    // PRIMARY: closed conduction (no ambient sink) -> energy conserved + spread shrinks.
    m_ambientT = 50.0f; m_heatExchange = 0.0f; m_conductionScale = 18.0f;
    seedGradient(); const Diag c0 = sample(gl); runHeat(200); const Diag c1 = sample(gl);
    const double energyErrOn = std::abs(c1.tempMean - c0.tempMean);
    const float  spreadOn0 = c0.tempMax - c0.tempMin, spreadOn1 = c1.tempMax - c1.tempMin;

    // NEG-CTRL A: ambient exchange injects energy -> tempMean drifts (non-conservation), caught.
    m_ambientT = 200.0f; m_heatExchange = 5.0f; m_conductionScale = 18.0f;
    seedGradient(); const Diag a0 = sample(gl); runHeat(200); const Diag a1 = sample(gl);
    const double energyErrLeak = std::abs(a1.tempMean - a0.tempMean);

    // NEG-CTRL B: SEVERED thermal coupling -- run NO thermal step -> no heat moves -> spread unchanged.
    // (A conduction-scale=0 control is NOT used: the P2G/G2P scatter+gather round-trip is itself a
    // smoother, so the spread shrinks even with zero Fourier flux -- that control would be vacuous.)
    m_ambientT = 50.0f; m_heatExchange = 0.0f; m_conductionScale = 18.0f;
    seedGradient(); const Diag b0 = sample(gl); /* deliberately no runHeat */ const Diag b1 = sample(gl);
    const float spreadOff0 = b0.tempMax - b0.tempMin, spreadOff1 = b1.tempMax - b1.tempMin;

    m_N = savedN; m_origin = savedOrigin; m_size = savedSize; m_particleCount = savedCount;
    m_ambientT = savedAmb; m_heatExchange = savedHx; m_conductionScale = savedS; m_maxWaveSpeed = savedWave;
    m_seedScratch.clear(); m_seeded = false;

    const double tol = 6.0;  // tempMean drift bound (matches Test 5/8)
    const bool energyConserved = energyErrOn < tol;
    const bool diffuses = spreadOn1 < 0.8f * spreadOn0;
    const bool leakCaught = energyErrLeak > 3.0 * tol;                 // ambient exchange clearly non-conserving
    const bool severedNoShrink = spreadOff1 > 0.95f * spreadOff0;      // no thermal step -> no redistribution
    const bool pass = energyConserved && diffuses && leakCaught && severedNoShrink;

    printf("[mpm-thermal]   closed conduction: energyErr(tempMean)=%.3f C (tol<%.1f); spread %.2f->%.2f (shrinks)  %s\n",
           energyErrOn, tol, spreadOn0, spreadOn1, (energyConserved && diffuses) ? "CONSERVED+DIFFUSES" : "FAIL");
    printf("[mpm-thermal]   NEG-CTRL A (ambient exchange = energy leak): tempMean drift=%.3f C (>>tol -> caught)  %s\n",
           energyErrLeak, leakCaught ? "REJECTS(non-vacuous)" : "VACUOUS!");
    printf("[mpm-thermal]   NEG-CTRL B (severed: no thermal step): spread %.2f->%.2f (must stay = no heat moves)  %s\n",
           spreadOff0, spreadOff1, severedNoShrink ? "REJECTS(non-vacuous)" : "VACUOUS!");
    printf("[mpm-thermal] %s\n", pass ? "ALL PASS (energy conserved + diffuses; energy-leak + severed-step caught)" : "FAILURES PRESENT");
    std::fflush(stdout);
    return pass;
}

glm::vec2 MpmSystem::vizRange() const
{
    switch (m_appearance.mode) {
        case VizMode::Thermal:  return m_appearance.thermal;
        case VizMode::VonMises: return m_appearance.vonMises;
        case VizMode::Strain:   return m_appearance.strain;
        default:                return glm::vec2(0.0f, 1.0f);
    }
}

// One-shot CPU min/max of the active viz scalar over all live particles, so the
// colour ramp spans the data. Same StVK strain/stress proxy the shader uses.
void MpmSystem::autoCalibrate(QOpenGLFunctions_4_3_Core* gl, bool smooth)
{
    if (m_appearance.freezeRange) return;            // F2: pinned range (gates / deterministic)
    if (m_particleCount <= 0 || m_appearance.mode == VizMode::Default) return;
    std::vector<float> buf(size_t(m_particleCount) * kStride);
    gl->glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_particleSSBO);
    gl->glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0,
                           GLsizeiptr(sizeof(float)) * buf.size(), buf.data());
    gl->glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
    float lo = 1e30f, hi = -1e30f;
    for (int i = 0; i < m_particleCount; ++i) {
        const float* p = &buf[size_t(i) * kStride];
        if (p[43] <= 0.0f) continue;
        float s = 0.0f;
        if (m_appearance.mode == VizMode::Thermal) {
            s = p[33];                                       // temperature
        } else {
            glm::mat3 F(glm::vec3(p[20], p[21], p[22]), glm::vec3(p[24], p[25], p[26]),
                        glm::vec3(p[28], p[29], p[30]));     // F columns
            glm::mat3 E = 0.5f * (glm::transpose(F) * F - glm::mat3(1.0f)); // Green strain
            if (m_appearance.mode == VizMode::Strain) {
                s = std::sqrt(glm::dot(E[0], E[0]) + glm::dot(E[1], E[1]) + glm::dot(E[2], E[2]));
            } else {                                         // VonMises (StVK)
                float mu = p[36], lambda = p[37];
                float trE = E[0][0] + E[1][1] + E[2][2];
                glm::mat3 sg = 2.0f * mu * E + lambda * trE * glm::mat3(1.0f);
                float a = sg[0][0], b = sg[1][1], c = sg[2][2];
                float d = sg[1][0], e = sg[2][1], f = sg[2][0];
                s = std::sqrt(0.5f * ((a - b) * (a - b) + (b - c) * (b - c) + (c - a) * (c - a))
                              + 3.0f * (d * d + e * e + f * f));
            }
        }
        lo = std::min(lo, s); hi = std::max(hi, s);
    }
    if (hi < lo) return;                                     // no live particles
    glm::vec2 r = (m_appearance.mode == VizMode::Thermal) ? glm::vec2(lo, std::max(hi, lo + 1.0f))
                                                          : glm::vec2(0.0f, std::max(hi, 1.0e-4f));
    glm::vec2& cur = (m_appearance.mode == VizMode::Thermal) ? m_appearance.thermal
                   : (m_appearance.mode == VizMode::Strain)  ? m_appearance.strain : m_appearance.vonMises;
    // F2: smooth (play-mode periodic) recalibration EMA-blends toward the target so
    // the colour scale no longer pops every 45 frames; the paused / mode-switch path
    // (smooth=false) snaps to the exact range so a static scene reads correctly at once.
    cur = smooth ? glm::mix(cur, r, 0.2f) : r;
    qInfo() << "[MPM viz] calibrated mode" << int(m_appearance.mode) << "range" << cur.x << cur.y
            << (smooth ? "(smoothed)" : "(exact)");
}

MpmSystem::Diag MpmSystem::sample(QOpenGLFunctions_4_3_Core* gl)
{
    Diag d;
    if (m_particleCount <= 0) return d;
    std::vector<float> buf(size_t(m_particleCount) * kStride);
    gl->glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_particleSSBO);
    gl->glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0,
                           GLsizeiptr(sizeof(float)) * buf.size(), buf.data());
    gl->glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
    double tempAccum = 0.0;
    d.tempMin = 1.0e30f;
    d.tempMax = -1.0e30f;
    d.minY = 1.0e30f;
    for (int i = 0; i < m_particleCount; ++i) {
        const float* p = &buf[size_t(i) * kStride];
        if (p[43] <= 0.0f) continue;
        const double m = p[3];
        d.totalMass += m;
        d.comPosition += glm::dvec3(p[0], p[1], p[2]) * m;
        d.comVelocity += glm::dvec3(p[4], p[5], p[6]) * m;
        d.maxSpeed = std::max(d.maxSpeed, glm::length(glm::vec3(p[4], p[5], p[6])));
        d.minY = std::min(d.minY, p[1]);  // lowest particle-centre y
        const float T = p[33];           // plastic.y = temperature
        tempAccum += T * m;
        d.tempMin = std::min(d.tempMin, T);
        d.tempMax = std::max(d.tempMax, T);
        if (p[39] < 0.5f) ++d.fluidCount; // matl.w == 0 -> Fluid
        ++d.live;
    }
    if (d.totalMass > 0) {
        d.comPosition /= d.totalMass;
        d.comVelocity /= d.totalMass;
        d.tempMean = tempAccum / d.totalMass;
    }
    if (d.live == 0) { d.tempMin = 0.0f; d.tempMax = 0.0f; d.minY = 0.0f; }
    return d;
}
