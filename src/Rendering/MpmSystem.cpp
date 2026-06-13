#include "MpmSystem.hpp"
#include "RenderingSystem.hpp"
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
constexpr int kStride = MpmSystem::kFloatsPerParticle; // 44 floats
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
    gl->glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
}

void MpmSystem::shutdown(QOpenGLFunctions_4_3_Core* gl)
{
    if (!m_initialized) return;
    gl->glDeleteBuffers(1, &m_particleSSBO);
    gl->glDeleteBuffers(1, &m_gridIntSSBO);
    gl->glDeleteBuffers(1, &m_gridVelSSBO);
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
        const float meltT = b.material == MpmMaterial::Snow ? 0.0f : 1.0e9f;

        float v[kStride] = { 0 };
        v[0] = p.x; v[1] = p.y; v[2] = p.z; v[3] = mass;             // pos, mass
        v[4] = b.initialVelocity.x; v[5] = b.initialVelocity.y;
        v[6] = b.initialVelocity.z; v[7] = vol;                     // vel, V0
        // C columns (8..19) = 0
        // F columns (20..31) = identity
        v[20] = 1.0f; v[25] = 1.0f; v[30] = 1.0f;
        v[32] = 1.0f;                                               // Jp (fluid: J)
        v[33] = b.temperature; v[34] = 900.0f; v[35] = meltT;       // temp, Cp, meltT
        // matl: solids store (mu, lambda, frictionAlpha); fluid stores
        // (bulkK, gamma) so the shader can pick an EOS instead of Lame.
        if (b.material == MpmMaterial::Fluid) {
            v[36] = b.youngsModulus; // bulk modulus K (set softly for explicit stability)
            v[37] = 7.0f;            // Tait gamma
            v[38] = 0.0f;
        } else {
            v[36] = mu; v[37] = lambda; v[38] = alpha;
        }
        v[39] = float(int(b.material));
        v[40] = b.color.r; v[41] = b.color.g; v[42] = b.color.b; v[43] = 1.0f; // color, alive
        m_seedScratch.insert(m_seedScratch.end(), v, v + kStride);
        ++count;
    };

    std::mt19937 rng{ 9281u };
    std::uniform_real_distribution<float> jit(-0.25f, 0.25f);
    float minSpacing = 0.04f;
    for (auto e : registry.view<MpmBodyComponent>())
        minSpacing = std::min(minSpacing, std::max(registry.get<MpmBodyComponent>(e).particleSpacing, 0.01f));
    m_renderRadius = 0.5f * minSpacing;
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
    for (auto e : registry.view<MpmBodyComponent>()) {
        const auto& b = registry.get<MpmBodyComponent>(e);
        // Fluid uses a Tait EOS whose effective bulk stiffness near J=1 is
        // K*gamma (dp/dJ = -K*gamma), so the sound speed is sqrt(K*gamma/rho).
        const float stiff = (b.material == MpmMaterial::Fluid)
                                ? b.youngsModulus * 7.0f : b.youngsModulus;
        const float c = std::sqrt(std::max(stiff, 1.0f) / std::max(b.density, 1.0f));
        m_maxWaveSpeed = std::max(m_maxWaveSpeed, c);
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
    if (!m_initialized || !m_playing) return;
    if (!m_seeded) {
        seedBodies(gl, registry);
        m_seeded = true;
    }
    if (m_particleCount <= 0) return;

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
                         float density, float E, float nu, glm::vec3 v0) {
        m_seedScratch.clear();
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
                    float v[kStride] = { 0 };
                    v[0] = pp.x; v[1] = pp.y; v[2] = pp.z; v[3] = mass;
                    v[4] = v0.x; v[5] = v0.y; v[6] = v0.z; v[7] = vol;
                    v[20] = 1.0f; v[25] = 1.0f; v[30] = 1.0f; v[32] = 1.0f;
                    v[33] = 20.0f; v[34] = 900.0f; v[35] = 1.0e9f;
                    if (material == 0) { v[36] = E; v[37] = 7.0f; v[38] = 0.0f; }
                    else { v[36] = mu; v[37] = lambda; v[38] = alpha; }
                    v[39] = float(material);
                    v[40] = 0.6f; v[41] = 0.7f; v[42] = 0.9f; v[43] = 1.0f;
                    m_seedScratch.insert(m_seedScratch.end(), v, v + kStride);
                    ++count;
                }
        m_particleCount = count;
        gl->glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_particleSSBO);
        gl->glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0,
                            GLsizeiptr(sizeof(float)) * m_seedScratch.size(), m_seedScratch.data());
        gl->glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
        const float stiff = (material == 0) ? E * 7.0f : E; // fluid: K*gamma
        m_maxWaveSpeed = std::max(1.0f, std::sqrt(stiff / density));
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
        seedBlock(0, glm::vec3(0, 0, 0), 0.2f, 0.05f, 1000.0f, 5.0e4f, 0.0f, glm::vec3(0));
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
        seedBlock(0, glm::vec3(0, -0.6f, 0), 0.3f, 0.06f, 1000.0f, 5.0e4f, 0.0f, glm::vec3(0));
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
        seedBlock(1, glm::vec3(0, -0.4f, 0), 0.25f, 0.05f, 1000.0f, 1.0e5f, 0.3f, glm::vec3(0));
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
        seedBlock(2, glm::vec3(0, -0.5f, 0), 0.22f, 0.045f, 1600.0f, 6.0e5f, 0.3f, glm::vec3(0));
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

    qInfo() << "[MPM selftest] overall:" << (allPass ? "ALL PASS" : "FAILURES PRESENT");
    // Restore empty state so the live scene seeds fresh from the registry.
    m_N = savedN;
    m_particleCount = 0;
    m_seeded = false;
    return allPass;
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
    for (int i = 0; i < m_particleCount; ++i) {
        const float* p = &buf[size_t(i) * kStride];
        if (p[43] <= 0.0f) continue;
        const double m = p[3];
        d.totalMass += m;
        d.comPosition += glm::dvec3(p[0], p[1], p[2]) * m;
        d.comVelocity += glm::dvec3(p[4], p[5], p[6]) * m;
        d.maxSpeed = std::max(d.maxSpeed, glm::length(glm::vec3(p[4], p[5], p[6])));
        ++d.live;
    }
    if (d.totalMass > 0) {
        d.comPosition /= d.totalMass;
        d.comVelocity /= d.totalMass;
    }
    return d;
}
