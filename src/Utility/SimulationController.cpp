#include "SimulationController.hpp"
#include "Scene.hpp"
#include "components.hpp"
#include "CollisionCookingService.hpp"
#include "HardwareCaps.hpp"

#include <QDebug>
#include <algorithm>
#include <chrono>
#include <thread>
#include <unordered_map>

#if defined(KR_WITH_PHYSX)
#include <PxPhysicsAPI.h>
#if PX_SUPPORT_GPU_PHYSX
#include <gpu/PxGpu.h>
#endif
using namespace physx;
#endif

#if defined(KR_WITH_PHYSX)
namespace {
/// KRS_BENCH instrumentation: log every new contact (position, normal,
/// separation) so contact-generation bugs are visible in benchmark logs.
struct BenchContactLogger : PxSimulationEventCallback
{
    void onContact(const PxContactPairHeader&, const PxContactPair* pairs, PxU32 n) override
    {
        for (PxU32 i = 0; i < n; ++i) {
            PxContactPairPoint pts[8];
            const PxU32 c = pairs[i].extractContacts(pts, 8);
            if (c == 0)
                qInfo() << "[Sim] contact pair (no points reported)";
            for (PxU32 j = 0; j < c; ++j)
                qInfo().nospace() << "[Sim] contact pos(" << pts[j].position.x << ","
                                  << pts[j].position.y << "," << pts[j].position.z << ") n("
                                  << pts[j].normal.x << "," << pts[j].normal.y << ","
                                  << pts[j].normal.z << ") sep " << pts[j].separation;
        }
    }
    void onConstraintBreak(PxConstraintInfo*, PxU32) override {}
    void onWake(PxActor**, PxU32) override {}
    void onSleep(PxActor**, PxU32) override {}
    void onTrigger(PxTriggerPair*, PxU32) override {}
    void onAdvance(const PxRigidBody* const*, const PxTransform*, PxU32) override {}
};
BenchContactLogger g_benchContactLogger;

PxFilterFlags benchFilterShader(PxFilterObjectAttributes a0, PxFilterData d0,
                                PxFilterObjectAttributes a1, PxFilterData d1,
                                PxPairFlags& pairFlags, const void* cb, PxU32 cbSize)
{
    const PxFilterFlags r = PxDefaultSimulationFilterShader(a0, d0, a1, d1, pairFlags, cb, cbSize);
    pairFlags |= PxPairFlag::eNOTIFY_TOUCH_FOUND | PxPairFlag::eNOTIFY_CONTACT_POINTS;
    return r;
}
} // namespace
#endif

// ===========================================================================
// PhysX backend (pimpl)
// ===========================================================================
struct SimulationController::PxImpl
{
#if defined(KR_WITH_PHYSX)
    PxDefaultAllocator allocator;
    PxDefaultErrorCallback errorCallback;
    PxFoundation* foundation = nullptr;
    PxPhysics* physics = nullptr;
    PxDefaultCpuDispatcher* dispatcher = nullptr;
    PxScene* scene = nullptr;
    PxMaterial* defaultMaterial = nullptr;
    PxRigidStatic* groundPlane = nullptr;
    // Phase G: live FANUC articulation (one robot for now).
    PxArticulationReducedCoordinate* articulation = nullptr;
    PxArticulationCache* articCache = nullptr;
    std::vector<PxArticulationLink*> articLinks;   // [0]=fixed root, [b+1]=joint b
    PxD6Joint* loopD6 = nullptr;                    // parallelogram closure (added in G.2)
    // Phase V (V.3): per MOVING-link (0-based) solid entities + the rest link-pose
    // inverse, so writeBackArticulationViz drives each solid by its link delta-pose.
    std::vector<std::vector<entt::entity>> articVizEntities;
    std::vector<PxTransform> articVizRestInv;
#if PX_SUPPORT_GPU_PHYSX
    PxCudaContextManager* cudaContextManager = nullptr;
#endif
    std::unordered_map<entt::entity, PxRigidActor*> actors;
    // Last pose WE wrote to the ECS: any divergence means the user moved
    // the entity (gizmo/panel) while playing — push it into the actor.
    std::unordered_map<entt::entity, std::pair<glm::vec3, glm::quat>> lastWritten;

    // --- Process-wide PhysX core (G.0) -------------------------------------
    // PhysX permits exactly ONE PxFoundation/PxPhysics per process. The first
    // SimulationController CREATES the core; later ones (e.g. the GATE-H live
    // harness) BORROW it. The shared core + the CollisionCookingService singleton
    // are torn down only when the LAST holder is destroyed (refcount). The
    // dispatcher + any CUDA context stay per-instance. Core lifecycle is
    // single-threaded here (Qt main / headless self-test), so no lock is needed.
    bool ownsCore = false;
    inline static PxFoundation* s_foundation = nullptr;
    inline static PxPhysics*    s_physics    = nullptr;
    inline static int           s_coreRefs   = 0;
    inline static bool          s_extInit    = false;   // PxInitExtensions called (closed at refs==0)
    static int coreRefCount() { return s_coreRefs; }
    static bool coreAlive()   { return s_physics != nullptr; }

    /// One-time CUDA probe: decides the GPU tiers (PhysX GPU dynamics, SDF
    /// dynamic trimeshes, GPU PBD fluids). Returns -1 path silently on
    /// non-NVIDIA hardware — no DLLs touched, CPU tiers stay default.
    void probeCuda()
    {
        auto& caps = krs::hardwareCaps();
        caps.probed = true;
#if PX_SUPPORT_GPU_PHYSX
        const int ordinal = PxGetSuggestedCudaDeviceOrdinal(errorCallback);
        if (ordinal < 0) {
            qInfo() << "[Sim] No CUDA-capable GPU — CPU solver tiers selected";
            return;
        }
        PxCudaContextManagerDesc cudaDesc;
        cudaDesc.deviceOrdinal = ordinal;
        cudaContextManager = PxCreateCudaContextManager(*foundation, cudaDesc, PxGetProfilerCallback());
        if (cudaContextManager && !cudaContextManager->contextIsValid()) {
            cudaContextManager->release(); // driver/DLL missing despite NVIDIA GPU
            cudaContextManager = nullptr;
        }
        if (cudaContextManager) {
            caps.cudaPhysics = true;
            caps.cudaDeviceName = cudaContextManager->getDeviceName();
            qInfo() << "[Sim] CUDA GPU available:" << caps.cudaDeviceName.c_str()
                    << "— GPU dynamics + SDF collision + GPU fluid tiers enabled";
        }
        else {
            qInfo() << "[Sim] CUDA device found but context invalid — CPU tiers selected";
        }
#else
        qInfo() << "[Sim] Built without PhysX GPU support — CPU solver tiers selected";
#endif
    }

    bool ensureCore()
    {
        if (physics) return true;
        const unsigned nThreads = std::max(1u, std::thread::hardware_concurrency() - 2);
        if (s_physics) {
            // Borrow the process-wide singleton; own a per-instance dispatcher only.
            foundation = s_foundation;
            physics    = s_physics;
            ownsCore   = false;
            dispatcher = PxDefaultCpuDispatcherCreate(nThreads);
            ++s_coreRefs;
            qInfo() << "[Sim] PhysX core borrowed (refs=" << s_coreRefs << ")";
            return true;
        }
        foundation = PxCreateFoundation(PX_PHYSICS_VERSION, allocator, errorCallback);
        if (!foundation) { qCritical() << "[Sim] PxCreateFoundation failed"; return false; }
        physics = PxCreatePhysics(PX_PHYSICS_VERSION, *foundation, PxTolerancesScale());
        if (!physics) { qCritical() << "[Sim] PxCreatePhysics failed"; foundation->release(); foundation = nullptr; return false; }
        dispatcher = PxDefaultCpuDispatcherCreate(nThreads);
        CollisionCookingService::instance().initialize(physics);
        probeCuda();
        s_foundation = foundation; s_physics = physics; ownsCore = true; ++s_coreRefs;
        qInfo() << "[Sim] PhysX" << PX_PHYSICS_VERSION_MAJOR << "." << PX_PHYSICS_VERSION_MINOR
                << "initialized (refs=" << s_coreRefs << ")";
        return true;
    }

    void releaseCore()
    {
        // Per-instance resources are always released by this instance.
        if (dispatcher) { dispatcher->release(); dispatcher = nullptr; }
#if PX_SUPPORT_GPU_PHYSX
        if (cudaContextManager) { cudaContextManager->release(); cudaContextManager = nullptr; }
#endif
        // The shared core (+ cooking singleton) is released only when the LAST
        // holder goes — a borrower's teardown must NOT free what others still use.
        if (physics || foundation) {
            if (s_coreRefs > 0) --s_coreRefs;
            if (s_coreRefs == 0) {
                CollisionCookingService::instance().shutdown();   // waits for in-flight cooks
                if (s_extInit) { PxCloseExtensions(); s_extInit = false; } // match PxInitExtensions (before physics)
                if (s_physics)    { s_physics->release();    s_physics = nullptr; }
                if (s_foundation) { s_foundation->release(); s_foundation = nullptr; }
            }
            physics = nullptr; foundation = nullptr;
        }
        ownsCore = false;
    }

    PxMaterial* makeMaterial(const PhysicsMaterial& m)
    {
        if (qEnvironmentVariableIsSet("KRS_BENCH"))
            qInfo() << "[Sim] material: e=" << m.restitution << "muS=" << m.staticFriction
                    << "muD=" << m.dynamicFriction;
        PxMaterial* mat = physics->createMaterial(m.staticFriction, m.dynamicFriction, m.restitution);
        // MAX combine: "bounciness 1" on a ball should bounce on a dead
        // floor. AVERAGE halved every user setting against the default
        // e=0.1 ground (bounciness 1.0 felt like 0.55).
        mat->setRestitutionCombineMode(PxCombineMode::eMAX);
        return mat;
    }

    static std::string debugNameFor(entt::registry& reg, entt::entity e)
    {
        if (auto* tag = reg.try_get<TagComponent>(e); tag && !tag->tag.empty()) return tag->tag;
        if (auto* mesh = reg.try_get<RenderableMeshComponent>(e); mesh && !mesh->sourcePath.empty())
            return mesh->sourcePath;
        return "entity-" + std::to_string(uint32_t(e));
    }

    /// Geometry is cooked unscaled; the entity's scale is applied here so one
    /// cooked mesh serves all instances. Negative scale can't flip cooked
    /// geometry (PhysX rejects it on dynamics) — collide with |scale|.
    static PxMeshScale meshScaleFor(const TransformComponent& xf)
    {
        const glm::vec3 s = glm::abs(xf.scale);
        return PxMeshScale(PxVec3(std::max(1e-4f, s.x), std::max(1e-4f, s.y), std::max(1e-4f, s.z)));
    }

    /// Explicit collider chain: Box -> Sphere -> Capsule -> ConvexMesh.
    /// These always take priority over automatic collision.
    PxShape* buildExplicitShape(entt::registry& reg, entt::entity e, const TransformComponent& xf)
    {
        PxShape* shape = nullptr;
        if (auto* box = reg.try_get<BoxCollider>(e)) {
            PxMaterial* mat = makeMaterial(box->material);
            shape = physics->createShape(
                PxBoxGeometry(box->halfExtents.x * xf.scale.x,
                              box->halfExtents.y * xf.scale.y,
                              box->halfExtents.z * xf.scale.z), *mat);
            shape->setLocalPose(PxTransform(PxVec3(box->offset.x * xf.scale.x,
                                                   box->offset.y * xf.scale.y,
                                                   box->offset.z * xf.scale.z)));
        }
        else if (auto* sph = reg.try_get<SphereCollider>(e)) {
            PxMaterial* mat = makeMaterial(sph->material);
            const float maxScale = std::max({ xf.scale.x, xf.scale.y, xf.scale.z });
            shape = physics->createShape(PxSphereGeometry(sph->radius * maxScale), *mat);
            shape->setLocalPose(PxTransform(PxVec3(sph->offset.x * xf.scale.x,
                                                   sph->offset.y * xf.scale.y,
                                                   sph->offset.z * xf.scale.z)));
        }
        else if (auto* cap = reg.try_get<CapsuleCollider>(e)) {
            PxMaterial* mat = makeMaterial(cap->material);
            const float s = std::max({ xf.scale.x, xf.scale.y, xf.scale.z });
            shape = physics->createShape(
                PxCapsuleGeometry(cap->radius * s, cap->height * 0.5f * s), *mat);
        }
        else if (auto* cvx = reg.try_get<ConvexMeshCollider>(e)) {
            if (auto* mesh = reg.try_get<RenderableMeshComponent>(e)) {
                auto fut = CollisionCookingService::instance().requestConvexHull(
                    mesh->vertices, debugNameFor(reg, e));
                if (PxConvexMesh* hull = fut.valid() ? fut.get() : nullptr) {
                    PxMaterial* mat = makeMaterial(cvx->material);
                    shape = physics->createShape(PxConvexMeshGeometry(hull, meshScaleFor(xf)), *mat);
                }
            }
        }
        return shape;
    }

    /// Automatic real-shape collision (AutoCollisionComponent, attached to
    /// every spawned mesh). Statics/kinematics resolve to an exact cooked
    /// triangle mesh; dynamics to a convex hull or a V-HACD decomposition
    /// (multiple hull shapes — concavity preserved). The CPU solver cannot
    /// simulate dynamic trimeshes (PhysX GPU SDF collision lifts this once
    /// CUDA hardware exists). rb == nullptr means static scenery.
    std::vector<PxShape*> buildAutoShapes(entt::registry& reg, entt::entity e,
                                          const TransformComponent& xf,
                                          const RigidBodyComponent* rb)
    {
        std::vector<PxShape*> shapes;
        auto* autoCol = reg.try_get<AutoCollisionComponent>(e);
        if (!autoCol || autoCol->mode == AutoCollisionComponent::Mode::None) return shapes;
        auto* mesh = reg.try_get<RenderableMeshComponent>(e);
        if (!mesh || mesh->vertices.empty()) return shapes;

        auto& cooking = CollisionCookingService::instance();
        const std::string name = debugNameFor(reg, e);
        const PxMeshScale meshScale = meshScaleFor(xf);
        const bool dynamicBody = rb && rb->bodyType == RigidBodyComponent::BodyType::Dynamic;
        // PhysX forbids triangle-mesh trigger shapes; triggers use the hull.
        const bool wantExact = autoCol->mode == AutoCollisionComponent::Mode::AutoExact
                               && !dynamicBody && !autoCol->isTrigger;

        if (autoCol->mode == AutoCollisionComponent::Mode::ConvexDecomposition) {
            auto fut = cooking.requestConvexDecomposition(mesh->vertices, mesh->indices, name);
            const bool ready = fut.valid()
                && fut.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
            if (ready && !fut.get().empty()) {
                PxMaterial* mat = makeMaterial(autoCol->material);
                for (PxConvexMesh* hull : fut.get())
                    shapes.push_back(physics->createShape(PxConvexMeshGeometry(hull, meshScale), *mat));
            }
            else if (!ready) {
                qInfo() << "[Sim]" << name.c_str()
                        << ": V-HACD still cooking — single hull this run, decomposition next rebuild";
            }
        }
        else if (wantExact) {
            auto fut = cooking.requestTriangleMesh(mesh->vertices, mesh->indices, name);
            if (PxTriangleMesh* tm = fut.valid() ? fut.get() : nullptr) {
                PxMaterial* mat = makeMaterial(autoCol->material);
                shapes.push_back(physics->createShape(PxTriangleMeshGeometry(tm, meshScale), *mat));
            }
            else {
                qWarning() << "[Sim]" << name.c_str()
                           << ": exact trimesh unavailable, falling back to convex hull";
            }
        }

        if (shapes.empty()) {
            if (dynamicBody && autoCol->mode == AutoCollisionComponent::Mode::AutoExact)
                qInfo() << "[Sim] dynamic" << name.c_str()
                        << ": exact trimesh needs the GPU solver — using convex hull";
            auto fut = cooking.requestConvexHull(mesh->vertices, name);
            if (PxConvexMesh* hull = fut.valid() ? fut.get() : nullptr) {
                PxMaterial* mat = makeMaterial(autoCol->material);
                shapes.push_back(physics->createShape(PxConvexMeshGeometry(hull, meshScale), *mat));
            }
        }
        if (autoCol->isTrigger) {
            for (PxShape* s : shapes) {
                s->setFlag(PxShapeFlag::eSIMULATION_SHAPE, false);
                s->setFlag(PxShapeFlag::eTRIGGER_SHAPE, true);
            }
        }
        return shapes;
    }

    /// Collision-only scenery: entities with collision intent but no rigid
    /// body become immovable colliders with their real shape. Skips fluid
    /// markers (their meshes are gizmos, not obstacles) and parented
    /// hierarchy members (their TransformComponent is parent-local — a
    /// world-pose static actor would sit in the wrong place).
    bool createStaticSceneryActor(entt::registry& reg, entt::entity e)
    {
        if (!scene || !reg.valid(e)) return false;
        if (reg.any_of<RigidBodyComponent>(e)) return false;
        if (reg.any_of<FluidEmitterComponent, FluidVolumeComponent>(e)) return false;
        if (reg.any_of<ParentComponent>(e)) return false;
        const auto* xfPtr = reg.try_get<TransformComponent>(e);
        if (!xfPtr) return false;

        std::vector<PxShape*> shapes;
        if (PxShape* s = buildExplicitShape(reg, e, *xfPtr)) shapes.push_back(s);
        if (shapes.empty()) shapes = buildAutoShapes(reg, e, *xfPtr, nullptr);
        if (shapes.empty()) return false;

        const PxTransform pose(
            PxVec3(xfPtr->translation.x, xfPtr->translation.y, xfPtr->translation.z),
            PxQuat(xfPtr->rotation.x, xfPtr->rotation.y, xfPtr->rotation.z, xfPtr->rotation.w));
        PxRigidStatic* actor = physics->createRigidStatic(pose);
        for (PxShape* s : shapes) {
            actor->attachShape(*s);
            s->release();
        }
        scene->addActor(*actor);
        actors[e] = actor;
        if (qEnvironmentVariableIsSet("KRS_BENCH")) {
            const PxBounds3 b = actor->getWorldBounds();
            qInfo().nospace() << "[Sim] scenery '" << debugNameFor(reg, e).c_str()
                              << "' bounds (" << b.minimum.x << "," << b.minimum.y << ","
                              << b.minimum.z << ") -> (" << b.maximum.x << "," << b.maximum.y
                              << "," << b.maximum.z << ")";
        }
        return true;
    }
#endif
};

// ===========================================================================
// Lifecycle
// ===========================================================================
SimulationController::SimulationController(Scene* scene, QObject* parent)
    : QObject(parent), m_scene(scene), m_px(std::make_unique<PxImpl>())
{
    m_clock.start();
#if defined(KR_WITH_PHYSX)
    // Eager init: spawn paths request speculative collision cooks long before
    // the first Play, so PxPhysics (and the cooking service) must exist now.
    m_px->ensureCore();
#else
    qWarning() << "[Sim] Built without PhysX (KR_WITH_PHYSX off) — rigid body simulation disabled.";
#endif
}

SimulationController::~SimulationController()
{
#if defined(KR_WITH_PHYSX)
    destroyPhysicsWorld();
    m_px->releaseCore();   // releaseCore shuts down the cooking singleton + frees the
                           // shared core only when this is the last holder (refcount).
#endif
}

int SimulationController::physxCoreRefCount()
{
#if defined(KR_WITH_PHYSX)
    return PxImpl::s_coreRefs;
#else
    return 0;
#endif
}

bool SimulationController::physxCoreAlive()
{
#if defined(KR_WITH_PHYSX)
    return PxImpl::s_physics != nullptr;
#else
    return false;
#endif
}

bool SimulationController::runLifecycleSelfTest()
{
#if !defined(KR_WITH_PHYSX)
    return true;
#else
    using std::printf;
    setvbuf(stdout, nullptr, _IONBF, 0);
    printf("[sim lifecycle] PhysX-core borrow/release across SimulationControllers\n");
    const int base = PxImpl::s_coreRefs;      // the app's controller already holds >=1 here
    bool ok = PxImpl::s_physics != nullptr;
    // (1) repeated create/destroy: each ctor borrows the shared core, dtor releases its ref.
    for (int i = 0; i < 12 && ok; ++i) {
        Scene sc;
        SimulationController sim(&sc);
        sim.play();           // buildPhysicsWorld on the borrowed core
        sim.singleStep();
        sim.stop();
        if (!PxImpl::s_physics) { ok = false; printf("[sim lifecycle] FAIL: core died in cycle %d\n", i); }
    }
    const bool balancedCycles = (PxImpl::s_coreRefs == base);
    // (2) two coexisting; destroy A first — B and the shared core must survive + still step.
    bool coreAfterA = false, bStillSteps = false;
    {
        Scene scA, scB;
        auto a = std::make_unique<SimulationController>(&scA);
        auto b = std::make_unique<SimulationController>(&scB);
        a->play(); b->play();
        const int refsBoth = PxImpl::s_coreRefs;     // base + 2
        a.reset();                                   // tear down A while B holds the core
        coreAfterA = (PxImpl::s_physics != nullptr) && (PxImpl::s_coreRefs == refsBoth - 1);
        b->singleStep();                             // B must still simulate (no use-after-free)
        bStillSteps = (PxImpl::s_physics != nullptr);
        b.reset();
    }
    const bool balancedFinal = (PxImpl::s_coreRefs == base) && ((PxImpl::s_physics != nullptr) == (base > 0));
    ok = ok && balancedCycles && coreAfterA && bStillSteps && balancedFinal;
    printf("[sim lifecycle] %s  baseRefs=%d cyclesBalanced=%d coreAliveAfterA=%d Bsteps=%d finalBalanced=%d\n",
           ok ? "PASS" : "FAIL", base, int(balancedCycles), int(coreAfterA), int(bStillSteps), int(balancedFinal));
    fflush(stdout);
    return ok;
#endif
}

void SimulationController::setState(SimulationState s)
{
    if (m_state == s) return;
    m_state = s;
    emit stateChanged(s);
}

void SimulationController::play()
{
    if (m_state == SimulationState::Playing) return;
    if (m_state == SimulationState::Stopped) {
        takeSnapshot();
        buildPhysicsWorld();
        m_accumulator = 0.0;
    }
    m_clock.restart();
    openHilCan();   // HIL CAN telemetry, if KRS_HIL_CAN is set (no-op otherwise)
    setState(SimulationState::Playing);
    qInfo() << "[Sim] Play";
}

void SimulationController::pause()
{
    if (m_state != SimulationState::Playing) return;
    setState(SimulationState::Paused);
    qInfo() << "[Sim] Pause";
}

void SimulationController::stop()
{
    if (m_state == SimulationState::Stopped) return;
    closeHilCan();
    destroyPhysicsWorld();
    restoreSnapshot();
    m_accumulator = 0.0;
    setState(SimulationState::Stopped);
    qInfo() << "[Sim] Stop (scene restored)";
}

void SimulationController::singleStep()
{
    if (m_state == SimulationState::Stopped) {
        takeSnapshot();
        buildPhysicsWorld();
        setState(SimulationState::Paused);
    }
    else if (m_state == SimulationState::Playing) {
        pause();
    }
    stepOnce(kFixedDt);
    writeBackTransforms();
}

void SimulationController::tick()
{
    if (m_state != SimulationState::Playing) { m_clock.restart(); return; }

    // nsecsElapsed: millisecond truncation here made simulation time run up
    // to ~12% slow vs wall clock (caught by the free-fall benchmark).
    const double frameSeconds = std::min(0.25, double(m_clock.nsecsElapsed()) * 1e-9);
    m_clock.restart();
    m_accumulator += frameSeconds;

    syncUserEdits(); // gizmo/panel moves while playing land in the actors

    int steps = 0;
    constexpr int kMaxStepsPerTick = 32;
    while (m_accumulator >= kFixedDt && steps < kMaxStepsPerTick) {
        pushKinematicTargets();
        if (m_can) applyCanCommands();   // CAN effort commands -> body forces (this step)
        stepOnce(kFixedDt);
        if (m_can) publishCanState();    // body pose/velocity/effort -> CAN state frames
        m_accumulator -= kFixedDt;
        ++steps;
    }
    // Never silently drop simulated time (it desynchronizes sim time from
    // wall clock — caught by the free-fall benchmark). The 0.25 s frame
    // clamp above already prevents a death spiral.

    if (steps > 0) { writeBackTransforms(); writeBackArticulationViz(); }
}

// ===========================================================================
// Snapshot / restore
// ===========================================================================
void SimulationController::takeSnapshot()
{
    m_snapshot.clear();
    auto& reg = m_scene->getRegistry();
    for (auto e : reg.view<RigidBodyComponent, TransformComponent>()) {
        const auto& xf = reg.get<TransformComponent>(e);
        m_snapshot.push_back({ e, xf.translation, xf.rotation, xf.scale });
    }
}

void SimulationController::restoreSnapshot()
{
    auto& reg = m_scene->getRegistry();
    for (const auto& s : m_snapshot) {
        if (!reg.valid(s.entity)) continue;
        auto& xf = reg.get<TransformComponent>(s.entity);
        xf.translation = s.translation;
        xf.rotation = s.rotation;
        xf.scale = s.scale;
        if (auto* rb = reg.try_get<RigidBodyComponent>(s.entity)) {
            rb->linearVelocity = glm::vec3(0.0f);
            rb->angularVelocity = glm::vec3(0.0f);
        }
    }
    m_snapshot.clear();
}

// ===========================================================================
// World construction
// ===========================================================================
void SimulationController::buildPhysicsWorld()
{
#if defined(KR_WITH_PHYSX)
    if (!m_px->ensureCore()) return;

    PxSceneDesc sceneDesc(m_px->physics->getTolerancesScale());
    sceneDesc.gravity = PxVec3(0.0f, -9.81f, 0.0f);
    sceneDesc.cpuDispatcher = m_px->dispatcher;
    sceneDesc.filterShader = PxDefaultSimulationFilterShader;
    // Accuracy configuration (validated by the KRS_BENCH suite):
    // TGS is PhysX 5's high-accuracy solver (same as Omniverse defaults);
    // CCD stops fast bodies tunneling; a low bounce threshold keeps
    // restitution physical down to slow impacts.
    sceneDesc.solverType = PxSolverType::eTGS; // TGS: accurate friction/stacks (benchmarked: PGS creeps on inclines, restitution identical)
    sceneDesc.flags |= PxSceneFlag::eENABLE_CCD | PxSceneFlag::eENABLE_STABILIZATION;
    sceneDesc.bounceThresholdVelocity = 0.5f;
    // Config bisection hooks (diagnosing solver-vs-flag interactions):
    if (qEnvironmentVariableIsSet("KRS_NO_STAB"))
        sceneDesc.flags &= ~PxSceneFlags(PxSceneFlag::eENABLE_STABILIZATION);
    if (qEnvironmentVariableIsSet("KRS_NO_CCD"))
        sceneDesc.flags &= ~PxSceneFlags(PxSceneFlag::eENABLE_CCD);
    if (qEnvironmentVariableIsSet("KRS_PGS"))
        sceneDesc.solverType = PxSolverType::ePGS;
    if (qEnvironmentVariableIsSet("KRS_NO_PCM"))
        sceneDesc.flags &= ~PxSceneFlags(PxSceneFlag::eENABLE_PCM); // legacy SAT contact gen

#if PX_SUPPORT_GPU_PHYSX
    // NVIDIA tier: GPU rigid dynamics (Omniverse-class). Enables SDF
    // collision for dynamic triangle meshes once cooking provides them.
    // Opt out with KRS_NO_GPU_PHYSICS while the tier matures.
    if (m_px->cudaContextManager && krs::hardwareCaps().cudaPhysics
        && !qEnvironmentVariableIsSet("KRS_NO_GPU_PHYSICS")) {
        sceneDesc.cudaContextManager = m_px->cudaContextManager;
        sceneDesc.flags |= PxSceneFlag::eENABLE_GPU_DYNAMICS;
        sceneDesc.broadPhaseType = PxBroadPhaseType::eGPU;
        qInfo() << "[Sim] GPU rigid dynamics ON:" << krs::hardwareCaps().cudaDeviceName.c_str();
    }
#endif
    if (qEnvironmentVariableIsSet("KRS_BENCH")) {
        sceneDesc.filterShader = benchFilterShader;
        sceneDesc.simulationEventCallback = &g_benchContactLogger;
    }
    m_px->scene = m_px->physics->createScene(sceneDesc);
    // Dead ground by default: with MAX restitution combine, bounce should
    // come from the OBJECT's material, not an arbitrary floor default.
    m_px->defaultMaterial = m_px->physics->createMaterial(0.6f, 0.6f, 0.0f);
    m_px->defaultMaterial->setRestitutionCombineMode(PxCombineMode::eMAX);

    // Static ground plane at y=0 (matches the grid).
    m_px->groundPlane = PxCreatePlane(*m_px->physics, PxPlane(0, 1, 0, 0), *m_px->defaultMaterial);
    m_px->scene->addActor(*m_px->groundPlane);

    auto& reg = m_scene->getRegistry();
    int dynamicCount = 0, staticCount = 0;

    for (auto e : reg.view<RigidBodyComponent, TransformComponent>()) {
        if (createActorForEntity(e)) {
            if (reg.get<RigidBodyComponent>(e).bodyType == RigidBodyComponent::BodyType::Static)
                ++staticCount;
            else
                ++dynamicCount;
        }
    }

    // Static collision-only entities (no rigid body): every spawned mesh
    // carries AutoCollisionComponent, so imported scenery collides with its
    // exact triangle mesh; explicit legacy colliders still take priority.
    for (auto e : reg.view<TransformComponent>()) {
        if (m_px->createStaticSceneryActor(reg, e)) ++staticCount;
    }

    if (m_hasRobotSpec) buildArticulation();   // Phase G: live FANUC articulation

    qInfo() << "[Sim] world built:" << dynamicCount << "dynamic," << staticCount << "static bodies (+ground plane)";

    if (qEnvironmentVariableIsSet("KRS_BENCH")) {
        // Probe: confirms cooked scenery is visible to the narrowphase.
        PxRaycastBuffer hit;
        if (m_px->scene->raycast(PxVec3(0.0f, 1.0f, 0.0f), PxVec3(0.0f, -1.0f, 0.0f), 10.0f, hit)
            && hit.hasBlock)
            qInfo().nospace() << "[Sim] probe ray (0,1,0) down: hit y=" << (1.0f - hit.block.distance)
                              << " normal (" << hit.block.normal.x << "," << hit.block.normal.y
                              << "," << hit.block.normal.z << ")";
    }
#endif
}

void SimulationController::setRobotArticulationSpec(const krs::dyn::RobotArticSpec& spec)
{
    m_robotSpec = spec;
    m_hasRobotSpec = !spec.joints.empty();
}

void SimulationController::buildArticulation()
{
#if defined(KR_WITH_PHYSX)
    if (!m_hasRobotSpec || !m_px->scene || !m_px->physics) return;
    using namespace physx;
    // Defensive: never leak a stale articulation if buildArticulation runs twice
    // (e.g. a double buildPhysicsWorld) — release the old one + its cache/loop first.
    if (m_px->articulation) {
        if (m_px->articCache) { m_px->articCache->release(); m_px->articCache = nullptr; }
        if (m_px->loopD6)     { m_px->loopD6->release();     m_px->loopD6 = nullptr; }
        m_px->scene->removeArticulation(*m_px->articulation);
        m_px->articulation->release(); m_px->articulation = nullptr;
        m_px->articLinks.clear();
        m_px->articVizEntities.clear();   // V.3: stale link->entity map must not outlive its links
        m_px->articVizRestInv.clear();
    }
    const auto& js = m_robotSpec.joints;
    const int n = int(js.size());

    auto rtreeQuat = [](const std::array<float, 9>& r) {
        PxMat33 m(PxVec3(r[0], r[3], r[6]), PxVec3(r[1], r[4], r[7]), PxVec3(r[2], r[5], r[8]));
        return PxQuat(m);
    };
    // shortest rotation +X -> axis (matches the oracle's FromTwoVectors(UnitX, axis))
    auto qXto = [](const std::array<float, 3>& ain) {
        PxVec3 a(ain[0], ain[1], ain[2]); a.normalize();
        const float d = PxVec3(1, 0, 0).dot(a);
        if (d >=  1.0f - 1e-6f) return PxQuat(PxIdentity);
        if (d <= -1.0f + 1e-6f) return PxQuat(PxPi, PxVec3(0, 0, 1)); // 180° about a perp axis
        const PxVec3 c = PxVec3(1, 0, 0).cross(a);
        return PxQuat(c.x, c.y, c.z, 1.0f + d).getNormalized();
    };
    auto attach = [&](PxArticulationLink* link, float mass, const std::array<float, 3>& I, const std::array<float, 3>& com) {
        PxRigidActorExt::createExclusiveShape(*link, PxSphereGeometry(0.02f), *m_px->defaultMaterial);
        link->setCMassLocalPose(PxTransform(PxVec3(com[0], com[1], com[2])));
        link->setMass(PxReal(mass));
        link->setMassSpaceInertiaTensor(PxVec3(I[0], I[1], I[2]));
    };

    PxArticulationReducedCoordinate* art = m_px->physics->createArticulationReducedCoordinate();
    art->setArticulationFlag(PxArticulationFlag::eFIX_BASE, m_robotSpec.fixBase);
    art->setSolverIterationCounts(64, 16);
    auto& links = m_px->articLinks; links.clear(); links.reserve(n + 1);
    PxArticulationLink* root = art->createLink(nullptr, PxTransform(PxIdentity));
    attach(root, 1.0f, { 1, 1, 1 }, { 0, 0, 0 });
    links.push_back(root);

    // zero-config link world poses: wp0[b] = wp0[parent] * (Rtree, ptree) (joint angle 0 = identity)
    std::vector<PxTransform> wp0(n);
    for (int b = 0; b < n; ++b) {
        const auto& j = js[b];
        const PxTransform local(PxVec3(j.ptree[0], j.ptree[1], j.ptree[2]), rtreeQuat(j.Rtree));
        const PxTransform parentW = (j.parent < 0) ? PxTransform(PxIdentity) : wp0[j.parent];
        wp0[b] = parentW * local;
        PxArticulationLink* parentLink = (j.parent < 0) ? root : links[j.parent + 1];
        PxArticulationLink* link = art->createLink(parentLink, wp0[b]);
        attach(link, j.mass, j.inertiaDiag, j.com);
        PxArticulationJointReducedCoordinate* jt = link->getInboundJoint();
        const PxQuat qa = qXto(j.axis);
        jt->setJointType(j.revolute ? PxArticulationJointType::eREVOLUTE : PxArticulationJointType::ePRISMATIC);
        jt->setParentPose(PxTransform(PxVec3(j.ptree[0], j.ptree[1], j.ptree[2]), rtreeQuat(j.Rtree) * qa));
        jt->setChildPose(PxTransform(PxVec3(0, 0, 0), qa));
        jt->setMotion(j.revolute ? PxArticulationAxis::eTWIST : PxArticulationAxis::eX, PxArticulationMotion::eFREE);
        links.push_back(link);
    }

    // Optional closed loop (FANUC parallelogram): pin link `tipLink`'s +tipLocal to a
    // fixed world anchor — lock the 3 translations, free all rotations (locking the
    // planar rotation would over-constrain the 4-bar). Created BEFORE addArticulation.
    const auto& lp = m_robotSpec.loop;
    if (lp.enabled && lp.tipLink >= 0 && lp.tipLink + 1 < int(links.size())) {
        ensurePhysxExtensions();   // PxD6Joint needs the extensions library
        const PxTransform tipLocal(PxVec3(lp.tipLocal[0], lp.tipLocal[1], lp.tipLocal[2]));
        const PxTransform anchorW(PxVec3(lp.anchorWorld[0], lp.anchorWorld[1], lp.anchorWorld[2]));
        PxD6Joint* d6 = PxD6JointCreate(*m_px->physics, nullptr, anchorW, links[lp.tipLink + 1], tipLocal);
        d6->setMotion(PxD6Axis::eX, PxD6Motion::eLOCKED);
        d6->setMotion(PxD6Axis::eY, PxD6Motion::eLOCKED);
        d6->setMotion(PxD6Axis::eZ, PxD6Motion::eLOCKED);
        d6->setMotion(PxD6Axis::eSWING1, PxD6Motion::eFREE);
        d6->setMotion(PxD6Axis::eSWING2, PxD6Motion::eFREE);
        d6->setMotion(PxD6Axis::eTWIST,  PxD6Motion::eFREE);
        m_px->loopD6 = d6;
    }

    m_px->scene->addArticulation(*art);
    m_px->articulation = art;
    m_px->articCache = art->createCache();
    qInfo() << "[Sim] live articulation built:" << n << "links, dofs=" << art->getDofs()
            << (lp.enabled ? "(+loop D6)" : "");
#endif
}

bool SimulationController::ensurePhysxExtensions()
{
#if defined(KR_WITH_PHYSX)
    // PxInitExtensions must run exactly once per process; both GATE A and the live
    // articulation loop closure need it. Guard on a single static so the borrow model
    // (multiple controllers, one shared PxPhysics) never double-inits.
    if (!PxImpl::s_extInit && PxImpl::s_physics) PxImpl::s_extInit = PxInitExtensions(*PxImpl::s_physics, nullptr);
    return PxImpl::s_extInit;
#else
    return false;
#endif
}

void SimulationController::setSceneGravity(float gx, float gy, float gz)
{
#if defined(KR_WITH_PHYSX)
    if (m_px->scene) m_px->scene->setGravity(physx::PxVec3(gx, gy, gz));
#else
    (void)gx; (void)gy; (void)gz;
#endif
}

bool SimulationController::setArticJointVelocities(const std::vector<float>& qd)
{
#if defined(KR_WITH_PHYSX)
    if (!m_px->articulation || !m_px->articCache) return false;
    const physx::PxU32 nDof = m_px->articulation->getDofs();
    if (qd.size() != nDof) return false;
    for (physx::PxU32 d = 0; d < nDof; ++d) m_px->articCache->jointVelocity[d] = physx::PxReal(qd[d]);
    m_px->articulation->applyCache(*m_px->articCache, physx::PxArticulationCacheFlag::eVELOCITY);
    return true;
#else
    (void)qd; return false;
#endif
}

bool SimulationController::commandJointTorques(const std::vector<float>& tau)
{
#if defined(KR_WITH_PHYSX)
    if (!m_px->articulation || !m_px->articCache) return false;
    const physx::PxU32 nDof = m_px->articulation->getDofs();
    if (tau.size() != nDof) return false;
    for (physx::PxU32 d = 0; d < nDof; ++d) m_px->articCache->jointForce[d] = physx::PxReal(tau[d]);
    m_px->articulation->applyCache(*m_px->articCache, physx::PxArticulationCacheFlag::eFORCE);
    return true;
#else
    (void)tau; return false;
#endif
}

std::vector<float> SimulationController::articJointAccel()
{
    std::vector<float> out;
#if defined(KR_WITH_PHYSX)
    if (!m_px->articulation || !m_px->articCache) return out;
    m_px->articulation->commonInit();
    m_px->articulation->computeJointAcceleration(*m_px->articCache);  // gravity + Coriolis + applied torque
    const physx::PxU32 nDof = m_px->articulation->getDofs();
    out.reserve(nDof);
    for (physx::PxU32 d = 0; d < nDof; ++d) out.push_back(float(m_px->articCache->jointAcceleration[d]));
#endif
    return out;
}

std::vector<float> SimulationController::articJointPositions()
{
    std::vector<float> out;
#if defined(KR_WITH_PHYSX)
    if (!m_px->articulation || !m_px->articCache) return out;
    m_px->articulation->copyInternalStateToCache(*m_px->articCache, physx::PxArticulationCacheFlag::ePOSITION);
    const physx::PxU32 nDof = m_px->articulation->getDofs();
    out.reserve(nDof);
    for (physx::PxU32 d = 0; d < nDof; ++d) out.push_back(float(m_px->articCache->jointPosition[d]));
#endif
    return out;
}

std::vector<float> SimulationController::articJointVelocities()
{
    std::vector<float> out;
#if defined(KR_WITH_PHYSX)
    if (!m_px->articulation || !m_px->articCache) return out;
    m_px->articulation->copyInternalStateToCache(*m_px->articCache, physx::PxArticulationCacheFlag::eVELOCITY);
    const physx::PxU32 nDof = m_px->articulation->getDofs();
    out.reserve(nDof);
    for (physx::PxU32 d = 0; d < nDof; ++d) out.push_back(float(m_px->articCache->jointVelocity[d]));
#endif
    return out;
}

int SimulationController::articDofCount() const
{
#if defined(KR_WITH_PHYSX)
    return m_px->articulation ? int(m_px->articulation->getDofs()) : 0;
#else
    return 0;
#endif
}

bool SimulationController::setArticJointPositions(const std::vector<float>& q)
{
#if defined(KR_WITH_PHYSX)
    if (!m_px->articulation || !m_px->articCache) return false;
    const physx::PxU32 nDof = m_px->articulation->getDofs();
    if (q.size() != nDof) return false;
    for (physx::PxU32 d = 0; d < nDof; ++d) m_px->articCache->jointPosition[d] = physx::PxReal(q[d]);
    m_px->articulation->applyCache(*m_px->articCache, physx::PxArticulationCacheFlag::ePOSITION);
    return true;
#else
    (void)q; return false;
#endif
}

std::vector<std::array<float, 7>> SimulationController::articLinkPoses() const
{
    std::vector<std::array<float, 7>> out;
#if defined(KR_WITH_PHYSX)
    if (!m_px->articulation) return out;
    out.reserve(m_px->articLinks.size());
    for (size_t i = 1; i < m_px->articLinks.size(); ++i) {   // skip the fixed root [0]
        const physx::PxTransform p = m_px->articLinks[i]->getGlobalPose();
        out.push_back({ p.p.x, p.p.y, p.p.z, p.q.x, p.q.y, p.q.z, p.q.w });
    }
#endif
    return out;
}

bool SimulationController::createActorForEntity(entt::entity e)
{
#if defined(KR_WITH_PHYSX)
    auto& reg = m_scene->getRegistry();
    if (!m_px->scene || !reg.valid(e)) return false;
    const auto* rbPtr = reg.try_get<RigidBodyComponent>(e);
    const auto* xfPtr = reg.try_get<TransformComponent>(e);
    if (!rbPtr || !xfPtr) return false;
    const auto& rb = *rbPtr;
    const auto& xf = *xfPtr;

    const PxTransform pose(
        PxVec3(xf.translation.x, xf.translation.y, xf.translation.z),
        PxQuat(xf.rotation.x, xf.rotation.y, xf.rotation.z, xf.rotation.w));

    // --- Build the shapes: explicit collider -> auto real shape -> AABB box ---
    std::vector<PxShape*> shapes;
    if (PxShape* s = m_px->buildExplicitShape(reg, e, xf)) shapes.push_back(s);
    if (shapes.empty()) shapes = m_px->buildAutoShapes(reg, e, xf, &rb);

    const auto* autoCol = reg.try_get<AutoCollisionComponent>(e);
    const bool optedOut = autoCol && autoCol->mode == AutoCollisionComponent::Mode::None;

    if (shapes.empty() && !optedOut) {
        if (auto* mesh = reg.try_get<RenderableMeshComponent>(e)) {
            const glm::vec3 he = glm::max((mesh->aabbMax - mesh->aabbMin) * 0.5f, glm::vec3(0.01f));
            const glm::vec3 center = (mesh->aabbMax + mesh->aabbMin) * 0.5f;
            PxShape* shape = m_px->physics->createShape(
                PxBoxGeometry(he.x * xf.scale.x, he.y * xf.scale.y, he.z * xf.scale.z),
                *m_px->defaultMaterial);
            shape->setLocalPose(PxTransform(PxVec3(center.x * xf.scale.x,
                                                   center.y * xf.scale.y,
                                                   center.z * xf.scale.z)));
            shapes.push_back(shape);
        }
        else {
            shapes.push_back(
                m_px->physics->createShape(PxBoxGeometry(0.1f, 0.1f, 0.1f), *m_px->defaultMaterial));
        }
    }

    // --- Create the actor ---
    PxRigidActor* actor = nullptr;
    if (rb.bodyType == RigidBodyComponent::BodyType::Static) {
        actor = m_px->physics->createRigidStatic(pose);
    }
    else {
        PxRigidDynamic* dyn = m_px->physics->createRigidDynamic(pose);
        dyn->setLinearDamping(rb.linearDamping);
        dyn->setAngularDamping(rb.angularDamping);
        dyn->setLinearVelocity(PxVec3(rb.linearVelocity.x, rb.linearVelocity.y, rb.linearVelocity.z));
        dyn->setAngularVelocity(PxVec3(rb.angularVelocity.x, rb.angularVelocity.y, rb.angularVelocity.z));
        dyn->setSolverIterationCounts(8, 4); // accuracy: stable stacks, crisp restitution
        if (rb.bodyType == RigidBodyComponent::BodyType::Kinematic)
            dyn->setRigidBodyFlag(PxRigidBodyFlag::eKINEMATIC, true);
        // NOTE: CCD stays scene-enabled but per-body opt-in only for very
        // fast movers — blanket CCD contacts damp restitution noticeably.
        actor = dyn;
    }
    for (PxShape* s : shapes) {
        actor->attachShape(*s);
        s->release();
    }

    if (auto* dyn = actor->is<PxRigidDynamic>(); dyn && rb.bodyType == RigidBodyComponent::BodyType::Dynamic) {
        if (!shapes.empty()) {
            PxRigidBodyExt::setMassAndUpdateInertia(*dyn, std::max(0.001f, rb.mass));
        }
        else {
            // Collision opted out (AutoCollision::None): the body still has
            // mass and moves ballistically, it just touches nothing.
            const float m = std::max(0.001f, rb.mass);
            dyn->setMass(m);
            glm::vec3 he(0.1f);
            if (auto* mesh = reg.try_get<RenderableMeshComponent>(e))
                he = glm::max((mesh->aabbMax - mesh->aabbMin) * 0.5f * xf.scale, glm::vec3(0.01f));
            // Solid-box inertia about the principal axes.
            dyn->setMassSpaceInertiaTensor(PxVec3(
                m / 3.0f * (he.y * he.y + he.z * he.z),
                m / 3.0f * (he.x * he.x + he.z * he.z),
                m / 3.0f * (he.x * he.x + he.y * he.y)));
        }
    }

    m_px->scene->addActor(*actor);
    m_px->actors[e] = actor;
    return true;
#else
    Q_UNUSED(e);
    return false;
#endif
}

void SimulationController::applyFluidImpulse(entt::entity e, const glm::vec3& impulse)
{
#if defined(KR_WITH_PHYSX)
    if (m_state != SimulationState::Playing || !m_px->scene) return;
    auto it = m_px->actors.find(e);
    if (it == m_px->actors.end()) return;
    auto* dyn = it->second->is<PxRigidDynamic>();
    if (!dyn || (dyn->getRigidBodyFlags() & PxRigidBodyFlag::eKINEMATIC)) return;

    // Stability clamp (research brief): limit per-frame Δv to 2 m/s so a
    // light body can't be slingshot by an impulse spike.
    const float mass = std::max(0.001f, dyn->getMass());
    glm::vec3 J = impulse;
    const float maxJ = mass * 2.0f;
    const float len = glm::length(J);
    if (len > maxJ) J *= maxJ / len;

    dyn->addForce(PxVec3(J.x, J.y, J.z), PxForceMode::eIMPULSE, true);
#else
    Q_UNUSED(e); Q_UNUSED(impulse);
#endif
}

void SimulationController::removeActorForEntity(entt::entity e)
{
#if defined(KR_WITH_PHYSX)
    auto it = m_px->actors.find(e);
    if (it == m_px->actors.end()) return;
    if (m_px->scene) m_px->scene->removeActor(*it->second);
    it->second->release();
    m_px->actors.erase(it);
#else
    Q_UNUSED(e);
#endif
}

void SimulationController::notifyEntityChanged(entt::entity e)
{
#if defined(KR_WITH_PHYSX)
    if (m_state == SimulationState::Stopped || !m_px->scene) return;
    removeActorForEntity(e);
    auto& reg = m_scene->getRegistry();
    if (!reg.valid(e) || !reg.all_of<TransformComponent>(e)) return;
    if (reg.all_of<RigidBodyComponent>(e))
        createActorForEntity(e); // rebuilt with current pose + velocity
    else
        m_px->createStaticSceneryActor(reg, e); // collision-only scenery live-rebuilds too
#else
    Q_UNUSED(e);
#endif
}

void SimulationController::destroyPhysicsWorld()
{
#if defined(KR_WITH_PHYSX)
    if (!m_px->scene) return;
    m_px->actors.clear();
    m_px->lastWritten.clear();
    // Phase G: tear the articulation down before the scene (cache + loop joint first).
    if (m_px->articCache)   { m_px->articCache->release();   m_px->articCache = nullptr; }
    if (m_px->loopD6)       { m_px->loopD6->release();       m_px->loopD6 = nullptr; }
    if (m_px->articulation) { m_px->scene->removeArticulation(*m_px->articulation);
                              m_px->articulation->release(); m_px->articulation = nullptr; }
    m_px->articLinks.clear();
    m_px->scene->release();   // releases contained actors
    m_px->scene = nullptr;
    m_px->groundPlane = nullptr;
#endif
}

// ===========================================================================
// Stepping
// ===========================================================================
void SimulationController::stepOnce(float dt)
{
#if defined(KR_WITH_PHYSX)
    if (!m_px->scene) return;
    m_px->scene->simulate(dt);
    m_px->scene->fetchResults(true);
#else
    Q_UNUSED(dt);
#endif
}

void SimulationController::pushKinematicTargets()
{
#if defined(KR_WITH_PHYSX)
    auto& reg = m_scene->getRegistry();
    for (auto& [e, actor] : m_px->actors) {
        auto* dyn = actor->is<PxRigidDynamic>();
        if (!dyn || !(dyn->getRigidBodyFlags() & PxRigidBodyFlag::eKINEMATIC)) continue;
        if (!reg.valid(e)) continue;
        const auto& xf = reg.get<TransformComponent>(e);
        dyn->setKinematicTarget(PxTransform(
            PxVec3(xf.translation.x, xf.translation.y, xf.translation.z),
            PxQuat(xf.rotation.x, xf.rotation.y, xf.rotation.z, xf.rotation.w)));
    }
#endif
}

void SimulationController::syncUserEdits()
{
#if defined(KR_WITH_PHYSX)
    auto& reg = m_scene->getRegistry();
    for (auto& [e, actor] : m_px->actors) {
        if (!reg.valid(e)) continue;
        const auto* xf = reg.try_get<TransformComponent>(e);
        if (!xf) continue;
        auto it = m_px->lastWritten.find(e);
        if (it == m_px->lastWritten.end()) {
            m_px->lastWritten[e] = { xf->translation, xf->rotation };
            continue;
        }
        const bool moved = glm::distance(xf->translation, it->second.first) > 1e-5f
                           || std::abs(glm::dot(xf->rotation, it->second.second)) < 1.0f - 1e-6f;
        if (!moved) continue;

        const PxTransform pose(
            PxVec3(xf->translation.x, xf->translation.y, xf->translation.z),
            PxQuat(xf->rotation.x, xf->rotation.y, xf->rotation.z, xf->rotation.w));
        if (auto* dyn = actor->is<PxRigidDynamic>();
            dyn && (dyn->getRigidBodyFlags() & PxRigidBodyFlag::eKINEMATIC)) {
            dyn->setKinematicTarget(pose);
        }
        else {
            actor->setGlobalPose(pose); // teleport; drag-throw velocity is a follow-up
            if (auto* dyn = actor->is<PxRigidDynamic>()) {
                dyn->setLinearVelocity(PxVec3(0));
                dyn->setAngularVelocity(PxVec3(0));
                dyn->wakeUp();
            }
        }
        it->second = { xf->translation, xf->rotation };
    }
#endif
}

void SimulationController::writeBackTransforms()
{
#if defined(KR_WITH_PHYSX)
    auto& reg = m_scene->getRegistry();
    for (auto& [e, actor] : m_px->actors) {
        auto* dyn = actor->is<PxRigidDynamic>();
        if (!dyn || (dyn->getRigidBodyFlags() & PxRigidBodyFlag::eKINEMATIC)) continue;
        if (!reg.valid(e)) continue;

        const PxTransform pose = dyn->getGlobalPose();
        auto& xf = reg.get<TransformComponent>(e);
        xf.translation = { pose.p.x, pose.p.y, pose.p.z };
        xf.rotation = glm::quat(pose.q.w, pose.q.x, pose.q.y, pose.q.z);
        m_px->lastWritten[e] = { xf.translation, xf.rotation };

        if (auto* rb = reg.try_get<RigidBodyComponent>(e)) {
            const PxVec3 lv = dyn->getLinearVelocity();
            const PxVec3 av = dyn->getAngularVelocity();
            rb->linearVelocity = { lv.x, lv.y, lv.z };
            rb->angularVelocity = { av.x, av.y, av.z };
        }
    }
#endif
}

// ===========================================================================
// Phase V (V.3): articulation-link -> solid-mesh writeback. The imported FANUC
// solids are baked in WORLD coords at the rest config; each solid rigidly tracks
// its assigned MOVING link by the link's delta-from-rest pose (delta*rest = now),
// written into the TransformComponent the renderer already consumes.
// ===========================================================================
void SimulationController::setArticulationVizMapping(const std::vector<std::vector<entt::entity>>& movingLinkEntities)
{
#if defined(KR_WITH_PHYSX)
    m_px->articVizEntities = movingLinkEntities;
    m_px->articVizRestInv.clear();
    if (!m_px->articulation) return;
    // moving links are articLinks[1..]; capture their current poses as the rest reference
    for (size_t i = 1; i < m_px->articLinks.size(); ++i)
        m_px->articVizRestInv.push_back(m_px->articLinks[i]->getGlobalPose().getInverse());
#else
    (void)movingLinkEntities;
#endif
}

void SimulationController::writeBackArticulationViz()
{
#if defined(KR_WITH_PHYSX)
    if (!m_px->articulation || m_px->articVizEntities.empty() || !m_scene) return;
    auto& reg = m_scene->getRegistry();
    const size_t nMoving = m_px->articLinks.empty() ? 0 : m_px->articLinks.size() - 1;
    for (size_t i = 0; i < m_px->articVizEntities.size() && i < nMoving && i < m_px->articVizRestInv.size(); ++i) {
        const PxTransform now   = m_px->articLinks[i + 1]->getGlobalPose();
        const PxTransform delta = now * m_px->articVizRestInv[i];   // delta * rest = now
        const glm::vec3 t{ delta.p.x, delta.p.y, delta.p.z };
        const glm::quat q{ delta.q.w, delta.q.x, delta.q.y, delta.q.z };
        for (entt::entity e : m_px->articVizEntities[i]) {
            if (!reg.valid(e) || !reg.all_of<TransformComponent>(e)) continue;
            auto& xf = reg.get<TransformComponent>(e);
            xf.translation = t;
            xf.rotation = q;
        }
    }
#endif
}

// ===========================================================================
// HIL CAN telemetry (Phase 2): bidirectional plant <-> bus coupling.
// ===========================================================================
void SimulationController::openHilCan()
{
    if (m_can) return;                                    // already open (e.g. resume from pause)
    if (!qEnvironmentVariableIsSet("KRS_HIL_CAN")) return;
    m_can = krs::hil::makeVirtualCAN();
    QString iface = qEnvironmentVariable("KRS_HIL_CAN_IFACE");
    if (iface.isEmpty())
#ifdef __linux__
        iface = "vcan0";
#else
        iface = "57001:57000";                            // tx:rx; the host mirrors as 57000:57001
#endif
    if (!m_can->open(iface.toStdString())) { qWarning() << "[Sim] HIL CAN open failed"; m_can.reset(); return; }
    qInfo() << "[Sim] HIL CAN telemetry on" << iface << "via" << m_can->backendName();
}

void SimulationController::closeHilCan()
{
    if (m_can) { m_can->close(); m_can.reset(); }
}

void SimulationController::applyCanCommands()
{
#if defined(KR_WITH_PHYSX)
    if (!m_can || !m_px->scene) return;
    auto& reg = m_scene->getRegistry();
    const int nDof = m_px->articulation ? int(m_px->articulation->getDofs()) : 0;
    bool articTouched = false;
    krs::hil::CanFrame fr;
    while (m_can->recv(fr)) {                              // drain all pending command frames
        int axis; float f[3];
        if (!krs::hil::cancodec::decodeEffort(fr, axis, f)) continue; // ignore non-effort frames
        // Phase G: an articulated robot routes effort -> JOINT TORQUE (axis = DOF).
        // This RETIRES the Phase-2 addForce fake (which applied CAN effort as a body
        // force because no articulation existed); the robot is now a real reduced-
        // coordinate articulation driven through its cache.
        if (m_px->articulation && m_px->articCache && axis >= 0 && axis < nDof) {
            m_px->articCache->jointForce[axis] = PxReal(f[0]);
            articTouched = true;
            continue;
        }
        // Legacy path: genuine FREE rigid-body actuators (non-articulated) take a force.
        for (auto e : reg.view<HilActuatorComponent>()) {
            auto& act = reg.get<HilActuatorComponent>(e);
            if (act.axisId != axis) continue;
            auto it = m_px->actors.find(e);
            if (it != m_px->actors.end()) {
                auto* dyn = it->second->is<PxRigidDynamic>();
                if (dyn && !(dyn->getRigidBodyFlags() & PxRigidBodyFlag::eKINEMATIC)) {
                    dyn->addForce(PxVec3(f[0], f[1], f[2]), PxForceMode::eFORCE, true);
                    act.lastEffort = { f[0], f[1], f[2] };
                }
            }
            break;
        }
    }
    if (articTouched) m_px->articulation->applyCache(*m_px->articCache, PxArticulationCacheFlag::eFORCE);
#endif
}

void SimulationController::publishCanState()
{
#if defined(KR_WITH_PHYSX)
    if (!m_can || !m_px->scene) return;
    auto& reg = m_scene->getRegistry();
    // Phase G: articulated robot publishes JOINT encoders from the cache (not body pose).
    if (m_px->articulation && m_px->articCache) {
        m_px->articulation->copyInternalStateToCache(*m_px->articCache, PxArticulationCacheFlag::ePOSITION);
        m_px->articulation->copyInternalStateToCache(*m_px->articCache, PxArticulationCacheFlag::eVELOCITY);
        const PxU32 nDof = m_px->articulation->getDofs();
        for (PxU32 d = 0; d < nDof; ++d) {
            float p[3] = { float(m_px->articCache->jointPosition[d]), 0.f, 0.f };
            float v[3] = { float(m_px->articCache->jointVelocity[d]), 0.f, 0.f };
            float t[3] = { float(m_px->articCache->jointForce[d]),    0.f, 0.f };
            m_can->send(krs::hil::cancodec::encodePose(int(d), p));
            m_can->send(krs::hil::cancodec::encodeVel(int(d), v));
            m_can->send(krs::hil::cancodec::encodeTorque(int(d), t));
        }
    }
    for (auto e : reg.view<HilActuatorComponent>()) {
        auto& act = reg.get<HilActuatorComponent>(e);
        auto it = m_px->actors.find(e);
        if (it == m_px->actors.end()) continue;
        auto* dyn = it->second->is<PxRigidDynamic>();
        if (!dyn) continue;
        const PxTransform pose = dyn->getGlobalPose();    // encoder position
        const PxVec3 lv = dyn->getLinearVelocity();        // encoder velocity
        float p[3] = { pose.p.x, pose.p.y, pose.p.z };
        float v[3] = { lv.x, lv.y, lv.z };
        float t[3] = { act.lastEffort.x, act.lastEffort.y, act.lastEffort.z }; // applied effort metric
        m_can->send(krs::hil::cancodec::encodePose(act.axisId, p));
        m_can->send(krs::hil::cancodec::encodeVel(act.axisId, v));
        m_can->send(krs::hil::cancodec::encodeTorque(act.axisId, t));
    }
#endif
}
