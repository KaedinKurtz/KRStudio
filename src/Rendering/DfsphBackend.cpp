#include "DfsphBackend.hpp"
#include "RenderingSystem.hpp"
#include "FluidSystem.hpp"
#include "components.hpp"

#include <QOpenGLFunctions_4_3_Core>
#include <QDebug>
#include <QElapsedTimer>

#include <glm/gtc/quaternion.hpp>
#include <vector>

#if defined(KR_WITH_SPLISHSPLASH)

#if defined(_WIN32)
// SPlisHSPlasH's FileSystem.h (pulled in by Simulation.h) calls ANSI WinAPI
// with char buffers; Qt builds define UNICODE which maps those names to the
// W variants. Remap to the A variants for this TU so their inline helpers
// compile as written.
#include <windows.h>
#include <commdlg.h>
#undef GetModuleFileName
#define GetModuleFileName GetModuleFileNameA
#undef WIN32_FIND_DATA
#define WIN32_FIND_DATA WIN32_FIND_DATAA
#undef FindFirstFile
#define FindFirstFile FindFirstFileA
#undef FindNextFile
#define FindNextFile FindNextFileA
#undef OPENFILENAME
#define OPENFILENAME OPENFILENAMEA
#undef GetOpenFileName
#define GetOpenFileName GetOpenFileNameA
#undef GetSaveFileName
#define GetSaveFileName GetSaveFileNameA
#endif

// Qt's lowercase macros collide with SPlisHSPlasH/GenericParameters code.
#ifdef emit
#undef emit
#endif
#ifdef signals
#undef signals
#endif
#ifdef slots
#undef slots
#endif
#ifdef foreach
#undef foreach
#endif

#include <SPlisHSPlasH/Common.h>
#include <SPlisHSPlasH/Simulation.h>
#include <SPlisHSPlasH/TimeManager.h>
#include <SPlisHSPlasH/FluidModel.h>
#include <SPlisHSPlasH/StaticRigidBody.h>
#include <SPlisHSPlasH/BoundaryModel_Akinci2012.h>
#include <SPlisHSPlasH/EmitterSystem.h>
#include <SPlisHSPlasH/TimeStep.h>
#include <SPlisHSPlasH/Viscosity/Viscosity_Weiler2018.h>
#include <SPlisHSPlasH/Viscosity/Viscosity_Standard.h>
#include <SPlisHSPlasH/SurfaceTension/SurfaceTension_Akinci2013.h>
#include <Utilities/Logger.h>
#include <Utilities/Timing.h>
#include <Utilities/Counting.h>

// The SPlisHSPlasH libraries expect the HOST application to instantiate
// their logging/timing/counting globals (their simulator's main.cpp does
// this). This TU is our "main" for the library.
INIT_LOGGING
INIT_TIMING
INIT_COUNTING

using namespace SPH;
#endif

namespace {

// Must match the GpuParticle layout in FluidSystem.cpp / fluid shaders.
struct GpuParticleMirror {
    glm::vec4 posLife;
    glm::vec4 vel;
    glm::vec4 pred;
};
static_assert(sizeof(GpuParticleMirror) == 48, "fluid SSBO layout drift");

#if defined(KR_WITH_SPLISHSPLASH)
/// Regular grid samples over the six faces of an oriented box (world space).
void sampleBoxFaces(const glm::vec3& center, const glm::vec3& halfExtents,
                    const glm::quat& rotation, float spacing,
                    std::vector<Vector3r>& out)
{
    auto emitFace = [&](int axis, float sign) {
        const int u = (axis + 1) % 3, v = (axis + 2) % 3;
        const int nu = std::max(1, int(std::ceil(2.0f * halfExtents[u] / spacing)));
        const int nv = std::max(1, int(std::ceil(2.0f * halfExtents[v] / spacing)));
        for (int i = 0; i <= nu; ++i) {
            for (int j = 0; j <= nv; ++j) {
                glm::vec3 p(0.0f);
                p[axis] = sign * halfExtents[axis];
                p[u] = -halfExtents[u] + 2.0f * halfExtents[u] * float(i) / float(nu);
                p[v] = -halfExtents[v] + 2.0f * halfExtents[v] * float(j) / float(nv);
                const glm::vec3 w = center + rotation * p;
                out.emplace_back(w.x, w.y, w.z);
            }
        }
    };
    for (int axis = 0; axis < 3; ++axis) {
        emitFace(axis, 1.0f);
        emitFace(axis, -1.0f);
    }
}
#endif

} // namespace

struct DfsphBackend::Impl
{
#if defined(KR_WITH_SPLISHSPLASH)
    bool built = false;
    bool playing = false;
    bool needsRebuild = true;
    double targetTime = 0.0;
    int lastCount = 0;
    int slowFrames = 0;
    std::vector<GpuParticleMirror> staging;

    void destroySim()
    {
        if (!Simulation::hasCurrent()) return;
        delete Simulation::getCurrent();
        Simulation::setCurrent(nullptr);
        built = false;
        lastCount = 0;
        targetTime = 0.0;
    }

    void buildSim(entt::registry& registry, const FluidParams& params)
    {
        destroySim();

        const Real radius = std::max(0.01f, params.particleRadius * 0.7f);
        const Real spacing = 2.0f * radius;

        Simulation* sim = Simulation::getCurrent();
        sim->init(radius, false);
        sim->setValue<int>(Simulation::CFL_METHOD, Simulation::ENUM_CFL_STANDARD);
        sim->setValue<Real>(Simulation::CFL_FACTOR, static_cast<Real>(0.5));
        sim->setValue<Real>(Simulation::CFL_MIN_TIMESTEPSIZE, static_cast<Real>(1e-4));
        sim->setValue<Real>(Simulation::CFL_MAX_TIMESTEPSIZE, static_cast<Real>(1.0 / 240.0));
        const Real grav[3] = { params.gravity.x, params.gravity.y, params.gravity.z };
        sim->setVecValue<Real>(Simulation::GRAVITATION, const_cast<Real*>(grav));
        // NOTE: the simulation METHOD is set after the models exist —
        // TimeStepDFSPH's constructor sizes its per-particle data from the
        // fluid models present at that moment. Likewise the FLUID model must
        // be added BEFORE any boundary models: SPlisHSPlasH assumes fluids
        // occupy neighborhood-search point sets [0, nFluids).
        sim->setBoundaryHandlingMethod(BoundaryHandlingMethods::Akinci2012);

        // ---- Fluid block(s) from FluidVolumeComponents ----
        std::vector<Vector3r> positions;
        std::vector<Vector3r> velocities;
        for (auto e : registry.view<FluidVolumeComponent, TransformComponent>()) {
            const auto& vol = registry.get<FluidVolumeComponent>(e);
            const auto& xf = registry.get<TransformComponent>(e);
            const glm::vec3 mn = xf.translation - vol.halfExtents;
            const glm::vec3 mx = xf.translation + vol.halfExtents;
            for (float x = mn.x + radius; x < mx.x; x += spacing)
                for (float y = mn.y + radius; y < mx.y; y += spacing)
                    for (float z = mn.z + radius; z < mx.z; z += spacing) {
                        positions.emplace_back(x, y, z);
                        velocities.emplace_back(0, 0, 0);
                    }
        }

        // Reserve emitter capacity (their emitters activate from this pool).
        unsigned maxEmitParticles = 0;
        if (!qEnvironmentVariableIsSet("KRS_DFSPH_NO_EMIT"))
            for (auto e : registry.view<FluidEmitterComponent>())
                if (registry.get<FluidEmitterComponent>(e).enabled) maxEmitParticles = 30000;

        if (positions.empty() && maxEmitParticles == 0) {
            qWarning() << "[DFSPH] no fluid volumes or emitters in scene — nothing to simulate";
            destroySim();
            return;
        }

        std::vector<unsigned int> objectIds(positions.size(), 0u);
        sim->addFluidModel("fluid0", unsigned(positions.size()),
                           positions.data(), velocities.data(), objectIds.data(),
                           maxEmitParticles);
        FluidModel* fm = sim->getFluidModel(0);
        fm->setDensity0(params.restDensity);

        // Real units: the viscosity coefficient is KINEMATIC ν = μ/ρ₀ [m²/s].
        // NOTE: Weiler 2018 (implicit, preferred) crashes in this build — its
        // AVX Cholesky path dies on the first step (tracked); the explicit
        // standard solver honors the same units and is stable for low-to-
        // moderate viscosity.
        fm->setViscosityMethod("Standard viscosity");
        if (auto* visco = fm->getViscosityBase()) {
            const Real nu = params.dynamicViscosityPaS / params.restDensity;
            visco->setValue<Real>(Viscosity_Standard::VISCOSITY_COEFFICIENT, nu);
        }
        fm->setSurfaceTensionMethod("Akinci et al. 2013");
        if (auto* st = fm->getSurfaceTensionBase()) {
            st->setValue<Real>(SurfaceTension_Akinci2013::SURFACE_TENSION,
                               params.surfaceTensionNpm);
        }

        // ---- Boundaries: ground plane + every box collider in the scene ----
        // (added after the fluid model — point-set order matters, see above)
        auto addBoundary = [&](std::vector<Vector3r>&& pts) {
            if (pts.empty()) return;
            auto* rbo = new StaticRigidBody();
            rbo->setPosition0(Vector3r::Zero());
            rbo->setPosition(Vector3r::Zero());
            rbo->setRotation0(Quaternionr::Identity());
            rbo->setRotation(Quaternionr::Identity());
            auto* bm = new BoundaryModel_Akinci2012();
            bm->initModel(rbo, unsigned(pts.size()), pts.data());
            sim->addBoundaryModel(bm);
        };

        {
            // Ground plane y=0 (matches the grid / PhysX plane), ±4 m.
            std::vector<Vector3r> ground;
            const float ext = 4.0f;
            const int n = int(2.0f * ext / spacing);
            ground.reserve(size_t(n + 1) * size_t(n + 1));
            for (int i = 0; i <= n; ++i)
                for (int j = 0; j <= n; ++j)
                    ground.emplace_back(-ext + i * spacing, 0.0f, -ext + j * spacing);
            addBoundary(std::move(ground));
        }
        int boxCount = 0;
        for (auto e : registry.view<BoxCollider, TransformComponent>()) {
            const auto& box = registry.get<BoxCollider>(e);
            const auto& xf = registry.get<TransformComponent>(e);
            std::vector<Vector3r> pts;
            sampleBoxFaces(xf.translation + xf.rotation * (box.offset * xf.scale),
                           box.halfExtents * xf.scale, xf.rotation, spacing, pts);
            addBoundary(std::move(pts));
            ++boxCount;
        }

        // ---- Emitters (circle nozzles emitting along the component dir) ----
        for (auto e : registry.view<FluidEmitterComponent, TransformComponent>()) {
            const auto& em = registry.get<FluidEmitterComponent>(e);
            if (!em.enabled) continue;
            const auto& xf = registry.get<TransformComponent>(e);
            const glm::vec3 dir = glm::normalize(xf.rotation * em.direction);
            // SPlisHSPlasH emitters emit along local +x.
            const glm::vec3 x = dir;
            const glm::vec3 helper = std::abs(x.y) < 0.9f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
            const glm::vec3 z = glm::normalize(glm::cross(x, helper));
            const glm::vec3 y = glm::cross(z, x);
            Matrix3r rot;
            rot << x.x, y.x, z.x,
                   x.y, y.y, z.y,
                   x.z, y.z, z.z;
            const unsigned size = std::max(1u, unsigned(em.emitterRadius / radius));
            fm->getEmitterSystem()->addEmitter(
                size, size,
                Vector3r(xf.translation.x, xf.translation.y, xf.translation.z),
                rot, em.initialSpeed, 1 /*circle*/);
        }

        sim->setSimulationMethod(int(SimulationMethods::DFSPH));
        sim->deferredInit();
        sim->updateBoundaryVolume(); // Akinci psi for all static boundaries

        TimeManager::getCurrent()->setTime(0.0);
        TimeManager::getCurrent()->setTimeStepSize(static_cast<Real>(1.0 / 240.0));
        targetTime = 0.0;
        built = true;
        qInfo().nospace() << "[DFSPH] sim built: " << positions.size() << " particles, "
                          << (boxCount + 1) << " boundaries, r=" << radius
                          << " m, rho0=" << params.restDensity
                          << " kg/m3, mu=" << params.dynamicViscosityPaS
                          << " Pa.s, sigma=" << params.surfaceTensionNpm << " N/m";
    }
#endif
};

#if defined(KR_WITH_SPLISHSPLASH)
namespace {
/// KRS_DFSPH_SELFTEST=<level>: staged pure-library scenario with
/// step-by-step logging. Level 1 = bare fluid block; 2 = + CFL/gravity/
/// viscosity/surface-tension config; 3 = + Akinci ground boundary;
/// 4 = + emitter with reserve pool.
void dfsphSelfTest(int level)
{
    qInfo() << "[DFSPH-selftest] level" << level << "— create sim";
    Simulation* sim = Simulation::getCurrent();
    sim->init(static_cast<Real>(0.025), false);

    if (level >= 2) {
        qInfo() << "[DFSPH-selftest] CFL + gravity config";
        sim->setValue<int>(Simulation::CFL_METHOD, Simulation::ENUM_CFL_STANDARD);
        sim->setValue<Real>(Simulation::CFL_FACTOR, static_cast<Real>(0.5));
        sim->setValue<Real>(Simulation::CFL_MIN_TIMESTEPSIZE, static_cast<Real>(1e-4));
        sim->setValue<Real>(Simulation::CFL_MAX_TIMESTEPSIZE, static_cast<Real>(1.0 / 240.0));
        const Real grav[3] = { 0.0f, -9.81f, 0.0f };
        sim->setVecValue<Real>(Simulation::GRAVITATION, const_cast<Real*>(grav));
        sim->setBoundaryHandlingMethod(BoundaryHandlingMethods::Akinci2012);
    }
    const bool wantViscosity = level >= 3;
    const bool wantSurfaceTension = level >= 4;
    const bool wantBoundary = level >= 5;
    const bool wantEmitter = level >= 6;

    qInfo() << "[DFSPH-selftest] add 216-particle block";
    std::vector<Vector3r> pos;
    std::vector<Vector3r> vel;
    for (int x = 0; x < 6; ++x)
        for (int y = 0; y < 6; ++y)
            for (int z = 0; z < 6; ++z) {
                pos.emplace_back(x * 0.05f, 1.0f + y * 0.05f, z * 0.05f);
                vel.emplace_back(0, 0, 0);
            }
    std::vector<unsigned int> ids(pos.size(), 0u);
    const unsigned reserve = wantEmitter ? 30000u : 0u;
    sim->addFluidModel("selftest", unsigned(pos.size()), pos.data(), vel.data(), ids.data(),
                       reserve);
    FluidModel* fm = sim->getFluidModel(0);
    fm->setDensity0(1000);

    if (wantViscosity) {
        const QString method = qEnvironmentVariable("KRS_DFSPH_VISCO", "weiler");
        qInfo() << "[DFSPH-selftest] viscosity method:" << method;
        if (method == QLatin1String("weiler")) {
            fm->setViscosityMethod("Weiler et al. 2018");
            if (auto* visco = fm->getViscosityBase())
                visco->setValue<Real>(Viscosity_Weiler2018::VISCOSITY_COEFFICIENT,
                                      static_cast<Real>(1e-6));
        }
        else if (method == QLatin1String("weiler-def")) {
            fm->setViscosityMethod("Weiler et al. 2018"); // keep default coefficient
        }
        else if (method == QLatin1String("standard")) {
            fm->setViscosityMethod("Standard viscosity");
        }
        else if (method == QLatin1String("xsph")) {
            fm->setViscosityMethod("XSPH");
        }
    }
    if (wantSurfaceTension) {
        qInfo() << "[DFSPH-selftest] surface tension method (Akinci 2013)";
        fm->setSurfaceTensionMethod("Akinci et al. 2013");
        if (auto* st = fm->getSurfaceTensionBase())
            st->setValue<Real>(SurfaceTension_Akinci2013::SURFACE_TENSION,
                               static_cast<Real>(0.0728));
    }

    if (wantBoundary) {
        qInfo() << "[DFSPH-selftest] Akinci ground boundary";
        std::vector<Vector3r> ground;
        for (int i = 0; i <= 40; ++i)
            for (int j = 0; j <= 40; ++j)
                ground.emplace_back(-1.0f + i * 0.05f, 0.0f, -1.0f + j * 0.05f);
        auto* rbo = new StaticRigidBody();
        rbo->setPosition0(Vector3r::Zero());
        rbo->setPosition(Vector3r::Zero());
        rbo->setRotation0(Quaternionr::Identity());
        rbo->setRotation(Quaternionr::Identity());
        auto* bm = new BoundaryModel_Akinci2012();
        bm->initModel(rbo, unsigned(ground.size()), ground.data());
        sim->addBoundaryModel(bm);
    }

    if (wantEmitter) {
        qInfo() << "[DFSPH-selftest] emitter";
        Matrix3r rot = Matrix3r::Identity();
        fm->getEmitterSystem()->addEmitter(3, 3, Vector3r(0, 2, 0), rot,
                                           static_cast<Real>(1.0), 1);
    }

    qInfo() << "[DFSPH-selftest] set DFSPH method";
    sim->setSimulationMethod(int(SimulationMethods::DFSPH));
    sim->deferredInit();
    if (wantBoundary) {
        qInfo() << "[DFSPH-selftest] updateBoundaryVolume";
        sim->updateBoundaryVolume();
    }
    qInfo() << "[DFSPH-selftest] stepping...";
    for (int i = 0; i < 5; ++i) {
        sim->getTimeStep()->step();
        qInfo() << "[DFSPH-selftest] step" << i << "ok, t ="
                << TimeManager::getCurrent()->getTime();
    }
    qInfo() << "[DFSPH-selftest] PASS — tearing down";
    delete Simulation::getCurrent();
    Simulation::setCurrent(nullptr);
    qInfo() << "[DFSPH-selftest] done";
}
} // namespace
#endif

DfsphBackend::DfsphBackend() : m_impl(std::make_unique<Impl>())
{
#if defined(KR_WITH_SPLISHSPLASH)
    if (qEnvironmentVariableIsSet("KRS_DFSPH_SELFTEST"))
        dfsphSelfTest(qEnvironmentVariable("KRS_DFSPH_SELFTEST").toInt());
#endif
}
DfsphBackend::~DfsphBackend()
{
#if defined(KR_WITH_SPLISHSPLASH)
    m_impl->destroySim();
#endif
}

bool DfsphBackend::available() const
{
#if defined(KR_WITH_SPLISHSPLASH)
    return true;
#else
    return false;
#endif
}

void DfsphBackend::initialize(RenderingSystem&, QOpenGLFunctions_4_3_Core*) {}

void DfsphBackend::samplePositions(std::vector<glm::vec4>& out) const
{
#if defined(KR_WITH_SPLISHSPLASH)
    out.resize(m_impl->staging.size());
    for (size_t i = 0; i < m_impl->staging.size(); ++i)
        out[i] = m_impl->staging[i].posLife;
#else
    out.clear();
#endif
}

void DfsphBackend::shutdown(QOpenGLFunctions_4_3_Core*)
{
#if defined(KR_WITH_SPLISHSPLASH)
    m_impl->destroySim();
#endif
}

void DfsphBackend::setPlaying(bool playing)
{
#if defined(KR_WITH_SPLISHSPLASH)
    if (playing && !m_impl->playing) m_impl->needsRebuild = true;
    m_impl->playing = playing;
#else
    Q_UNUSED(playing);
#endif
}

void DfsphBackend::reset()
{
#if defined(KR_WITH_SPLISHSPLASH)
    m_impl->destroySim();
    m_impl->needsRebuild = true;
#endif
}

int DfsphBackend::update(RenderingSystem& renderer, QOpenGLFunctions_4_3_Core* gl,
                         entt::registry& registry, float dt)
{
#if defined(KR_WITH_SPLISHSPLASH)
    FluidSystem* fluidSystem = renderer.getFluidSystem();
    if (!fluidSystem) return 0;

    if (m_impl->needsRebuild) {
        m_impl->buildSim(registry, fluidSystem->params());
        m_impl->needsRebuild = false;
    }
    if (!m_impl->built) return 0;

    Simulation* sim = Simulation::getCurrent();
    TimeManager* tm = TimeManager::getCurrent();

    // Advance with adaptive CFL substeps inside a hard CPU budget: when the
    // CPU can't keep up, sim time dilates instead of freezing the editor.
    static bool firstStep = true;
    if (firstStep) qInfo() << "[DFSPH] first step starting...";
    m_impl->targetTime += dt;
    QElapsedTimer wall;
    wall.start();
    int substeps = 0;
    while (tm->getTime() < m_impl->targetTime && substeps < 32 && wall.elapsed() < 50) {
        sim->getTimeStep()->step();
        if (firstStep) {
            qInfo() << "[DFSPH] first substep OK in" << wall.elapsed() << "ms, dt ="
                    << tm->getTimeStepSize();
            firstStep = false;
        }
        ++substeps;
    }
    if (tm->getTime() < m_impl->targetTime) {
        m_impl->targetTime = tm->getTime(); // dilation: drop unmet sim time
        if (++m_impl->slowFrames == 90)
            qInfo() << "[DFSPH] CPU-bound: simulation running slower than real time"
                    << "(reference tier — bake for full speed)";
    }

    // ---- Publish positions/velocities into the shared particle SSBO ----
    FluidModel* fm = sim->getFluidModel(0);
    const unsigned n = std::min<unsigned>(fm->numActiveParticles(),
                                          unsigned(FluidSystem::kMaxParticles));
    auto& staging = m_impl->staging;
    staging.resize(n);
    for (unsigned i = 0; i < n; ++i) {
        const Vector3r& p = fm->getPosition(i);
        const Vector3r& v = fm->getVelocity(i);
        staging[i].posLife = glm::vec4(p[0], p[1], p[2], 1.0f);
        staging[i].vel = glm::vec4(v[0], v[1], v[2], 0.0f);
        staging[i].pred = glm::vec4(p[0], p[1], p[2], 0.0f);
    }
    if (n > 0) {
        gl->glBindBuffer(GL_SHADER_STORAGE_BUFFER, fluidSystem->particleBuffer());
        gl->glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0,
                            GLsizeiptr(n * sizeof(GpuParticleMirror)), staging.data());
        gl->glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
    }
    m_impl->lastCount = int(n);
    return int(n);
#else
    Q_UNUSED(renderer); Q_UNUSED(gl); Q_UNUSED(registry); Q_UNUSED(dt);
    return 0;
#endif
}
