#include "SimulationController.hpp"
#include "Scene.hpp"
#include "components.hpp"

#include <QDebug>
#include <algorithm>
#include <thread>
#include <unordered_map>

#if defined(KR_WITH_PHYSX)
#include <PxPhysicsAPI.h>
using namespace physx;
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
    std::unordered_map<entt::entity, PxRigidActor*> actors;

    bool ensureCore()
    {
        if (physics) return true;
        foundation = PxCreateFoundation(PX_PHYSICS_VERSION, allocator, errorCallback);
        if (!foundation) { qCritical() << "[Sim] PxCreateFoundation failed"; return false; }
        physics = PxCreatePhysics(PX_PHYSICS_VERSION, *foundation, PxTolerancesScale());
        if (!physics) { qCritical() << "[Sim] PxCreatePhysics failed"; return false; }
        dispatcher = PxDefaultCpuDispatcherCreate(std::max(1u, std::thread::hardware_concurrency() - 2));
        qInfo() << "[Sim] PhysX" << PX_PHYSICS_VERSION_MAJOR << "." << PX_PHYSICS_VERSION_MINOR << "initialized";
        return true;
    }

    void releaseCore()
    {
        if (dispatcher) { dispatcher->release(); dispatcher = nullptr; }
        if (physics) { physics->release(); physics = nullptr; }
        if (foundation) { foundation->release(); foundation = nullptr; }
    }

    PxMaterial* makeMaterial(const PhysicsMaterial& m)
    {
        return physics->createMaterial(m.staticFriction, m.dynamicFriction, m.restitution);
    }

    /// Cook a convex hull from mesh vertices (subsampled if huge).
    PxConvexMesh* cookConvex(const std::vector<Vertex>& verts, const glm::vec3& scale)
    {
        std::vector<PxVec3> pts;
        const size_t stride = std::max<size_t>(1, verts.size() / 2048); // hulls don't need every vertex
        pts.reserve(verts.size() / stride + 1);
        for (size_t i = 0; i < verts.size(); i += stride) {
            const auto& p = verts[i].position;
            pts.emplace_back(p.x * scale.x, p.y * scale.y, p.z * scale.z);
        }

        PxConvexMeshDesc desc;
        desc.points.count = static_cast<PxU32>(pts.size());
        desc.points.stride = sizeof(PxVec3);
        desc.points.data = pts.data();
        desc.flags = PxConvexFlag::eCOMPUTE_CONVEX | PxConvexFlag::eQUANTIZE_INPUT;
        desc.vertexLimit = 64;

        PxTolerancesScale tolScale;
        PxCookingParams params(tolScale);
        PxConvexMesh* mesh = PxCreateConvexMesh(params, desc, physics->getPhysicsInsertionCallback());
        if (!mesh) qWarning() << "[Sim] convex cooking failed (" << pts.size() << "points )";
        return mesh;
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
#if !defined(KR_WITH_PHYSX)
    qWarning() << "[Sim] Built without PhysX (KR_WITH_PHYSX off) — rigid body simulation disabled.";
#endif
}

SimulationController::~SimulationController()
{
#if defined(KR_WITH_PHYSX)
    destroyPhysicsWorld();
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

    const double frameSeconds = std::min(0.25, m_clock.restart() * 1e-3); // clamp hitches
    m_accumulator += frameSeconds;

    int steps = 0;
    constexpr int kMaxStepsPerTick = 8;
    while (m_accumulator >= kFixedDt && steps < kMaxStepsPerTick) {
        pushKinematicTargets();
        stepOnce(kFixedDt);
        m_accumulator -= kFixedDt;
        ++steps;
    }
    if (steps == kMaxStepsPerTick) m_accumulator = 0.0; // running behind: drop time, don't spiral

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
    m_px->scene = m_px->physics->createScene(sceneDesc);
    m_px->defaultMaterial = m_px->physics->createMaterial(0.6f, 0.6f, 0.1f);

    // Static ground plane at y=0 (matches the grid).
    m_px->groundPlane = PxCreatePlane(*m_px->physics, PxPlane(0, 1, 0, 0), *m_px->defaultMaterial);
    m_px->scene->addActor(*m_px->groundPlane);

    auto& reg = m_scene->getRegistry();
    int dynamicCount = 0, staticCount = 0;

    for (auto e : reg.view<RigidBodyComponent, TransformComponent>()) {
        const auto& rb = reg.get<RigidBodyComponent>(e);
        const auto& xf = reg.get<TransformComponent>(e);

        const PxTransform pose(
            PxVec3(xf.translation.x, xf.translation.y, xf.translation.z),
            PxQuat(xf.rotation.x, xf.rotation.y, xf.rotation.z, xf.rotation.w));

        // --- Build the shape from whichever collider the entity carries ---
        PxShape* shape = nullptr;
        PxMaterial* mat = m_px->defaultMaterial;

        if (auto* box = reg.try_get<BoxCollider>(e)) {
            mat = m_px->makeMaterial(box->material);
            shape = m_px->physics->createShape(
                PxBoxGeometry(box->halfExtents.x * xf.scale.x,
                              box->halfExtents.y * xf.scale.y,
                              box->halfExtents.z * xf.scale.z), *mat);
        }
        else if (auto* sph = reg.try_get<SphereCollider>(e)) {
            mat = m_px->makeMaterial(sph->material);
            const float maxScale = std::max({ xf.scale.x, xf.scale.y, xf.scale.z });
            shape = m_px->physics->createShape(PxSphereGeometry(sph->radius * maxScale), *mat);
        }
        else if (auto* cap = reg.try_get<CapsuleCollider>(e)) {
            mat = m_px->makeMaterial(cap->material);
            const float s = std::max({ xf.scale.x, xf.scale.y, xf.scale.z });
            shape = m_px->physics->createShape(
                PxCapsuleGeometry(cap->radius * s, cap->height * 0.5f * s), *mat);
        }
        else if (auto* cvx = reg.try_get<ConvexMeshCollider>(e)) {
            if (auto* mesh = reg.try_get<RenderableMeshComponent>(e)) {
                mat = m_px->makeMaterial(cvx->material);
                if (PxConvexMesh* hull = m_px->cookConvex(mesh->vertices, xf.scale))
                    shape = m_px->physics->createShape(PxConvexMeshGeometry(hull), *mat);
            }
        }

        // Fallback: AABB box from the render mesh.
        if (!shape) {
            if (auto* mesh = reg.try_get<RenderableMeshComponent>(e)) {
                const glm::vec3 he = glm::max((mesh->aabbMax - mesh->aabbMin) * 0.5f, glm::vec3(0.01f));
                const glm::vec3 center = (mesh->aabbMax + mesh->aabbMin) * 0.5f;
                shape = m_px->physics->createShape(
                    PxBoxGeometry(he.x * xf.scale.x, he.y * xf.scale.y, he.z * xf.scale.z),
                    *m_px->defaultMaterial);
                shape->setLocalPose(PxTransform(PxVec3(center.x * xf.scale.x,
                                                       center.y * xf.scale.y,
                                                       center.z * xf.scale.z)));
            }
            else {
                shape = m_px->physics->createShape(PxBoxGeometry(0.1f, 0.1f, 0.1f), *m_px->defaultMaterial);
            }
        }

        // --- Create the actor ---
        PxRigidActor* actor = nullptr;
        if (rb.bodyType == RigidBodyComponent::BodyType::Static) {
            actor = m_px->physics->createRigidStatic(pose);
            ++staticCount;
        }
        else {
            PxRigidDynamic* dyn = m_px->physics->createRigidDynamic(pose);
            dyn->setLinearDamping(rb.linearDamping);
            dyn->setAngularDamping(rb.angularDamping);
            dyn->setLinearVelocity(PxVec3(rb.linearVelocity.x, rb.linearVelocity.y, rb.linearVelocity.z));
            if (rb.bodyType == RigidBodyComponent::BodyType::Kinematic)
                dyn->setRigidBodyFlag(PxRigidBodyFlag::eKINEMATIC, true);
            actor = dyn;
            ++dynamicCount;
        }
        actor->attachShape(*shape);
        shape->release();

        if (auto* dyn = actor->is<PxRigidDynamic>(); dyn && rb.bodyType == RigidBodyComponent::BodyType::Dynamic)
            PxRigidBodyExt::setMassAndUpdateInertia(*dyn, std::max(0.001f, rb.mass));

        m_px->scene->addActor(*actor);
        m_px->actors[e] = actor;
    }

    // Static collision-only entities (no rigid body): trimesh-tagged or
    // convex-tagged scenery participates as immovable colliders.
    for (auto e : reg.view<ConvexMeshCollider, TransformComponent>()) {
        if (reg.any_of<RigidBodyComponent>(e)) continue;
        const auto* mesh = reg.try_get<RenderableMeshComponent>(e);
        if (!mesh) continue;
        const auto& xf = reg.get<TransformComponent>(e);
        PxConvexMesh* hull = m_px->cookConvex(mesh->vertices, xf.scale);
        if (!hull) continue;
        const PxTransform pose(
            PxVec3(xf.translation.x, xf.translation.y, xf.translation.z),
            PxQuat(xf.rotation.x, xf.rotation.y, xf.rotation.z, xf.rotation.w));
        PxRigidStatic* actor = m_px->physics->createRigidStatic(pose);
        PxShape* shape = m_px->physics->createShape(PxConvexMeshGeometry(hull), *m_px->defaultMaterial);
        actor->attachShape(*shape);
        shape->release();
        m_px->scene->addActor(*actor);
        m_px->actors[e] = actor;
        ++staticCount;
    }

    qInfo() << "[Sim] world built:" << dynamicCount << "dynamic," << staticCount << "static bodies (+ground plane)";
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
