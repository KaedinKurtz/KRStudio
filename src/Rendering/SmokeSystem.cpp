#include "SmokeSystem.hpp"
#include "RenderingSystem.hpp"
#include "Shader.hpp"
#include "components.hpp"
#include "HardwareCaps.hpp"

#include <QOpenGLFunctions_4_3_Core>
#include <QDebug>
#include <vector>
#include <algorithm>

namespace {
GLuint make3D(QOpenGLFunctions_4_3_Core* gl, int n, GLenum internalFormat)
{
    GLuint t = 0;
    gl->glGenTextures(1, &t);
    gl->glBindTexture(GL_TEXTURE_3D, t);
    gl->glTexStorage3D(GL_TEXTURE_3D, 1, internalFormat, n, n, n);
    gl->glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    gl->glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    gl->glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl->glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    gl->glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    return t;
}
constexpr int kLocal = 8;
} // namespace

void SmokeSystem::initialize(RenderingSystem& renderer, QOpenGLFunctions_4_3_Core* gl)
{
    Q_UNUSED(renderer);
    if (m_initialized) return;

    // 96^3 default (~46 MB of volumes, iGPU-safe). NVIDIA/opt-in gets 128.
    m_N = krs::hardwareCaps().cudaPhysics ? 128 : 96;
    bool ok = false;
    const int envN = qEnvironmentVariable("KRS_SMOKE_GRID").toInt(&ok);
    if (ok && envN >= 32 && envN <= 192) m_N = (envN / 8) * 8; // multiple of local size

    allocate(gl);
    m_initialized = true;
    qInfo() << "[Smoke] initialized grid" << m_N << "^3";
}

void SmokeSystem::allocate(QOpenGLFunctions_4_3_Core* gl)
{
    for (int i = 0; i < 2; ++i) {
        m_velocity[i] = make3D(gl, m_N, GL_RGBA16F);
        m_scalars[i] = make3D(gl, m_N, GL_RGBA16F);
        m_pressure[i] = make3D(gl, m_N, GL_R32F);
    }
    m_divergence = make3D(gl, m_N, GL_R32F);
    m_curl = make3D(gl, m_N, GL_RGBA16F);
    m_needsZero = true;
}

void SmokeSystem::zeroAll(QOpenGLFunctions_4_3_Core* gl)
{
    // glClearTexImage is GL 4.4 (unavailable). One host-side zero upload is
    // fine since this only runs at init and on reset, never per frame.
    const size_t cells = size_t(m_N) * m_N * m_N;
    std::vector<float> zerosRGBA(cells * 4, 0.0f);
    std::vector<float> zerosR(cells, 0.0f);
    auto up = [&](GLuint t, GLenum fmt, const void* data) {
        gl->glBindTexture(GL_TEXTURE_3D, t);
        gl->glTexSubImage3D(GL_TEXTURE_3D, 0, 0, 0, 0, m_N, m_N, m_N, fmt, GL_FLOAT, data);
    };
    for (int i = 0; i < 2; ++i) {
        up(m_velocity[i], GL_RGBA, zerosRGBA.data());
        up(m_scalars[i], GL_RGBA, zerosRGBA.data());
        up(m_pressure[i], GL_RED, zerosR.data());
    }
    up(m_divergence, GL_RED, zerosR.data());
    up(m_curl, GL_RGBA, zerosRGBA.data());
    gl->glBindTexture(GL_TEXTURE_3D, 0);
    m_velCur = m_sclCur = m_prsCur = 0;
    m_needsZero = false;
}

void SmokeSystem::shutdown(QOpenGLFunctions_4_3_Core* gl)
{
    if (!m_initialized) return;
    GLuint texs[] = { m_velocity[0], m_velocity[1], m_scalars[0], m_scalars[1],
                      m_pressure[0], m_pressure[1], m_divergence, m_curl };
    gl->glDeleteTextures(8, texs);
    m_initialized = false;
}

void SmokeSystem::computeDomain(entt::registry& registry)
{
    // A SmokeDomainTag entity's transform wins; otherwise fit a box around
    // the active emitters with headroom for the plume to rise.
    for (auto e : registry.view<SmokeDomainTag, TransformComponent>()) {
        const auto& xf = registry.get<TransformComponent>(e);
        m_size = glm::max(xf.scale, glm::vec3(1.0f));
        m_origin = xf.translation - m_size * 0.5f;
        m_domainReady = true;
        return;
    }
    glm::vec3 lo(1e9f), hi(-1e9f);
    int n = 0;
    for (auto e : registry.view<SmokeEmitterComponent, TransformComponent>()) {
        const auto& xf = registry.get<TransformComponent>(e);
        lo = glm::min(lo, xf.translation);
        hi = glm::max(hi, xf.translation);
        ++n;
    }
    if (n == 0) return;
    const glm::vec3 c = 0.5f * (lo + hi);
    m_size = glm::max(hi - lo + glm::vec3(3.2f, 5.5f, 3.2f), glm::vec3(4.0f, 6.0f, 4.0f));
    m_origin = glm::vec3(c.x - m_size.x * 0.5f, std::max(-0.2f, lo.y - 0.6f),
                         c.z - m_size.z * 0.5f);
    m_domainReady = true;
}

void SmokeSystem::update(RenderingSystem& renderer, QOpenGLFunctions_4_3_Core* gl,
                         entt::registry& registry, float dt)
{
    if (!m_initialized || !m_playing) { m_hasEmitters = false; return; }

    // Gather emitters.
    struct Emit { glm::vec4 c; glm::vec4 p; };
    std::vector<Emit> emitters;
    m_params.fireEnabled = false;
    for (auto e : registry.view<SmokeEmitterComponent, TransformComponent>()) {
        if (emitters.size() >= 8) break;
        const auto& em = registry.get<SmokeEmitterComponent>(e);
        if (!em.enabled) continue;
        const auto& xf = registry.get<TransformComponent>(e);
        emitters.push_back({ glm::vec4(xf.translation, em.radius),
                             glm::vec4(em.densityRate, em.temperature, em.fuelRate, em.jetSpeed) });
        if (em.fuelRate > 0.0f) m_params.fireEnabled = true;
        // Pick up the smoke colour from the first emitter for the renderer.
        if (emitters.size() == 1) m_params.smokeColor = em.color;
    }
    m_hasEmitters = !emitters.empty();
    if (!m_hasEmitters) return;

    if (!m_domainReady) computeDomain(registry);
    if (m_needsZero) zeroAll(gl);

    // NB: 'emit' is a Qt macro — never name a C++ identifier that.
    Shader* emitSh = renderer.getShader("smoke_emit");
    Shader* advect = renderer.getShader("smoke_advect");
    Shader* curl = renderer.getShader("smoke_curl");
    Shader* forces = renderer.getShader("smoke_forces");
    Shader* combust = renderer.getShader("smoke_combust");
    Shader* diverge = renderer.getShader("smoke_divergence");
    Shader* jacobi = renderer.getShader("smoke_jacobi");
    Shader* project = renderer.getShader("smoke_project");
    if (!emitSh || !advect || !curl || !forces || !combust || !diverge || !jacobi || !project) return;

    const float sdt = glm::clamp(dt, 1.0f / 240.0f, 1.0f / 45.0f);
    const int groups = (m_N + kLocal - 1) / kLocal;
    const glm::ivec3 gridv(m_N);

    auto dispatch = [&]() {
        gl->glDispatchCompute(groups, groups, groups);
        gl->glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);
    };
    auto setGrid = [&](Shader* s) {
        gl->glUniform3i(gl->glGetUniformLocation(s->ID, "u_grid"), m_N, m_N, m_N);
    };
    auto img = [&](int unit, GLuint tex, GLenum access, GLenum fmt) {
        gl->glBindImageTexture(unit, tex, 0, GL_TRUE, 0, access, fmt);
    };
    auto sampler = [&](Shader* s, int unit, const char* name, GLuint tex) {
        gl->glActiveTexture(GL_TEXTURE0 + unit);
        gl->glBindTexture(GL_TEXTURE_3D, tex);
        s->setInt(gl, name, unit);
    };

    // 1) Emit (in place on current velocity + scalars).
    emitSh->use(gl);
    setGrid(emitSh);
    emitSh->setVec3(gl, "u_origin", m_origin);
    emitSh->setVec3(gl, "u_size", m_size);
    emitSh->setFloat(gl, "u_dt", sdt);
    emitSh->setInt(gl, "u_emitterCount", int(emitters.size()));
    for (size_t i = 0; i < emitters.size(); ++i) {
        emitSh->setVec4(gl, ("u_emitter[" + std::to_string(i) + "]").c_str(), emitters[i].c);
        emitSh->setVec4(gl, ("u_emitterP[" + std::to_string(i) + "]").c_str(), emitters[i].p);
    }
    img(0, m_scalars[m_sclCur], GL_READ_WRITE, GL_RGBA16F);
    img(1, m_velocity[m_velCur], GL_READ_WRITE, GL_RGBA16F);
    dispatch();

    // 2) Self-advect velocity (src -> dst, advecting field = src).
    advect->use(gl);
    setGrid(advect);
    advect->setVec3(gl, "u_origin", m_origin);
    advect->setVec3(gl, "u_size", m_size);
    advect->setFloat(gl, "u_dt", sdt);
    advect->setFloat(gl, "u_dissipation", 1.0f);
    sampler(advect, 0, "u_vel", m_velocity[m_velCur]);
    sampler(advect, 1, "u_src", m_velocity[m_velCur]);
    img(0, m_velocity[1 - m_velCur], GL_WRITE_ONLY, GL_RGBA16F);
    dispatch();
    m_velCur = 1 - m_velCur;

    // 3) Curl + 4) confinement & buoyancy (in place on velocity).
    curl->use(gl); setGrid(curl);
    img(0, m_velocity[m_velCur], GL_READ_ONLY, GL_RGBA16F);
    img(1, m_curl, GL_WRITE_ONLY, GL_RGBA16F);
    dispatch();

    forces->use(gl); setGrid(forces);
    forces->setFloat(gl, "u_dt", sdt);
    forces->setFloat(gl, "u_vorticity", m_params.vorticity);
    forces->setFloat(gl, "u_buoyancy", m_params.buoyancy);
    forces->setFloat(gl, "u_densityWeight", m_params.densityWeight);
    forces->setFloat(gl, "u_ambient", m_params.ambientTemperature);
    img(0, m_velocity[m_velCur], GL_READ_WRITE, GL_RGBA16F);
    img(1, m_curl, GL_READ_ONLY, GL_RGBA16F);
    img(2, m_scalars[m_sclCur], GL_READ_ONLY, GL_RGBA16F);
    dispatch();

    // 5) Scalar reactions (combustion, cooling, dissipation).
    combust->use(gl); setGrid(combust);
    combust->setFloat(gl, "u_dt", sdt);
    combust->setFloat(gl, "u_cooling", m_params.cooling);
    combust->setFloat(gl, "u_densityDissipation", m_params.densityDissipation);
    combust->setFloat(gl, "u_burnRate", m_params.fireEnabled ? m_params.burnRate : 0.0f);
    combust->setFloat(gl, "u_ambient", m_params.ambientTemperature);
    img(0, m_scalars[m_sclCur], GL_READ_WRITE, GL_RGBA16F);
    dispatch();

    // 6) Divergence.
    diverge->use(gl); setGrid(diverge);
    img(0, m_velocity[m_velCur], GL_READ_ONLY, GL_RGBA16F);
    img(1, m_divergence, GL_WRITE_ONLY, GL_R32F);
    dispatch();

    // 7) Jacobi pressure (warm-started from last frame).
    jacobi->use(gl); setGrid(jacobi);
    for (int it = 0; it < m_params.pressureIterations; ++it) {
        img(0, m_pressure[m_prsCur], GL_READ_ONLY, GL_R32F);
        img(1, m_pressure[1 - m_prsCur], GL_WRITE_ONLY, GL_R32F);
        img(2, m_divergence, GL_READ_ONLY, GL_R32F);
        gl->glDispatchCompute(groups, groups, groups);
        gl->glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
        m_prsCur = 1 - m_prsCur;
    }

    // 8) Project (subtract gradient, free-slip walls).
    project->use(gl); setGrid(project);
    img(0, m_velocity[m_velCur], GL_READ_WRITE, GL_RGBA16F);
    img(1, m_pressure[m_prsCur], GL_READ_ONLY, GL_R32F);
    dispatch();

    // 9) Advect scalars with the divergence-free field.
    advect->use(gl);
    setGrid(advect);
    advect->setVec3(gl, "u_origin", m_origin);
    advect->setVec3(gl, "u_size", m_size);
    advect->setFloat(gl, "u_dt", sdt);
    advect->setFloat(gl, "u_dissipation", 1.0f);
    sampler(advect, 0, "u_vel", m_velocity[m_velCur]);
    sampler(advect, 1, "u_src", m_scalars[m_sclCur]);
    img(0, m_scalars[1 - m_sclCur], GL_WRITE_ONLY, GL_RGBA16F);
    dispatch();
    m_sclCur = 1 - m_sclCur;

    gl->glActiveTexture(GL_TEXTURE0);

    // Diagnostic: report peak density/temperature so we can tell the solver
    // from the renderer when nothing shows.
    static const int dbg = qEnvironmentVariable("KRS_SMOKE_DEBUG").toInt();
    if (dbg > 0) {
        static int f = 0;
        if ((++f % 60) == 0) {
            const size_t cells = size_t(m_N) * m_N * m_N;
            std::vector<float> buf(cells * 4);
            gl->glBindTexture(GL_TEXTURE_3D, m_scalars[m_sclCur]);
            gl->glGetTexImage(GL_TEXTURE_3D, 0, GL_RGBA, GL_FLOAT, buf.data());
            gl->glBindTexture(GL_TEXTURE_3D, 0);
            float maxD = 0, maxT = 0;
            for (size_t i = 0; i < cells; ++i) { maxD = std::max(maxD, buf[i * 4]); maxT = std::max(maxT, buf[i * 4 + 1]); }
            qInfo() << "[Smoke] maxDensity" << maxD << "maxTemp" << maxT
                    << "emitters" << int(emitters.size())
                    << "origin" << m_origin.x << m_origin.y << m_origin.z
                    << "size" << m_size.x << m_size.y << m_size.z;
        }
    }
}
