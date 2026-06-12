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
#if PX_SUPPORT_GPU_PHYSX
    PxCudaContextManager* cudaContextManager = nullptr;
#endif
    std::unordered_map<entt::entity, PxRigidActor*> actors;

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
        foundation = PxCreateFoundation(PX_PHYSICS_VERSION, allocator, errorCallback);
        if (!foundation) { qCritical() << "[Sim] PxCreateFoundation failed"; return false; }
        physics = PxCreatePhysics(PX_PHYSICS_VERSION, *foundation, PxTolerancesScale());
        if (!physics) { qCritical() << "[Sim] PxCreatePhysics failed"; return false; }
        dispatcher = PxDefaultCpuDispatcherCreate(std::max(1u, std::thread::hardware_concurrency() - 2));
        CollisionCookingService::instance().initialize(physics);
        probeCuda();
        qInfo() << "[Sim] PhysX" << PX_PHYSICS_VERSION_MAJOR << "." << PX_PHYSICS_VERSION_MINOR << "initialized";
        return true;
    }

    void releaseCore()
    {
        if (dispatcher) { dispatcher->release(); dispatcher = nullptr; }
        if (physics) { physics->release(); physics = nullptr; }
#if PX_SUPPORT_GPU_PHYSX
        if (cudaContextManager) { cudaContextManager->release(); cudaContextManager = nullptr; }
#endif
        if (foundation) { foundation->release(); foundation = nullptr; }
    }

    PxMaterial* makeMaterial(const PhysicsMaterial& m)
    {
        if (qEnvironmentVariableIsSet("KRS_BENCH"))
            qInfo() << "[Sim] material: e=" << m.restitution << "muS=" << m.staticFriction
                    << "muD=" << m.dynamicFriction;
        return physics->createMaterial(m.staticFriction, m.dynamicFriction, m.restitution);
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
    CollisionCookingService::instance().shutdown(); // waits for in-flight cooks
    m_px->releaseCore();
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

    int steps = 0;
    constexpr int kMaxStepsPerTick = 32;
    while (m_accumulator >= kFixedDt && steps < kMaxStepsPerTick) {
        pushKinematicTargets();
        stepOnce(kFixedDt);
        m_accumulator -= kFixedDt;
        ++steps;
    }
    // Never silently drop simulated time (it desynchronizes sim time from
    // wall clock — caught by the free-fall benchmark). The 0.25 s frame
    // clamp above already prevents a death spiral.

    if (steps > 0) writeBackTransforms();
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
    m_px->defaultMaterial = m_px->physics->createMaterial(0.6f, 0.6f, 0.1f);

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

        if (auto* rb = reg.try_get<RigidBodyComponent>(e)) {
            const PxVec3 lv = dyn->getLinearVelocity();
            const PxVec3 av = dyn->getAngularVelocity();
            rb->linearVelocity = { lv.x, lv.y, lv.z };
            rb->angularVelocity = { av.x, av.y, av.z };
        }
    }
#endif
}
