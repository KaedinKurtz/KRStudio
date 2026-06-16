#include "GraspSim.hpp"

#if !defined(KR_WITH_PHYSX)
#include <cstdio>
namespace krs::grasp {
GraspResult runGripperSim(const RenderableMeshComponent&, const GraspSpec&, const WorldOverride&) { return {}; }
bool graspSucceeded(const GraspResult&) { return false; }
bool assertPhysicsLocked(physx::PxScene*, physx::PxMaterial*, float) { return false; }
}
#else

#include "GraspMesh.hpp"
#include "CollisionCookingService.hpp"
#include "SimulationController.hpp"
#include <PxPhysicsAPI.h>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <cstdint>

using namespace physx;

namespace krs::grasp {
namespace {

enum Tag { TAG_NONE = 0, TAG_OBJ = 1, TAG_JAW0 = 2, TAG_JAW1 = 3, TAG_GROUND = 4 };
inline int tagOf(const PxActor* a) { return a ? int(reinterpret_cast<intptr_t>(a->userData)) : 0; }
inline PxVec3 toPx(const glm::vec3& v) { return PxVec3(v.x, v.y, v.z); }
inline PxQuat toPxQ(const glm::quat& q) { return PxQuat(q.x, q.y, q.z, q.w); }
inline glm::vec3 toGlm(const PxVec3& v) { return glm::vec3(v.x, v.y, v.z); }

// per-step contact latches: set on TOUCH_FOUND, cleared on TOUCH_LOST -> reflect the current contact state.
struct Tracker : PxSimulationEventCallback {
    bool objJaw0 = false, objJaw1 = false, objGround = false;
    void onContact(const PxContactPairHeader& h, const PxContactPair* pairs, PxU32 n) override {
        const int ta = tagOf(h.actors[0]), tb = tagOf(h.actors[1]);
        int other = 0;
        if (ta == TAG_OBJ) other = tb; else if (tb == TAG_OBJ) other = ta; else return;
        bool* latch = (other == TAG_JAW0) ? &objJaw0 : (other == TAG_JAW1) ? &objJaw1 : (other == TAG_GROUND) ? &objGround : nullptr;
        if (!latch) return;
        for (PxU32 i = 0; i < n; ++i) {
            if (pairs[i].events & PxPairFlag::eNOTIFY_TOUCH_FOUND) *latch = true;
            if (pairs[i].events & PxPairFlag::eNOTIFY_TOUCH_LOST)  *latch = false;
        }
    }
    void onTrigger(PxTriggerPair*, PxU32) override {}
    void onConstraintBreak(PxConstraintInfo*, PxU32) override {}
    void onWake(PxActor**, PxU32) override {}
    void onSleep(PxActor**, PxU32) override {}
    void onAdvance(const PxRigidBody* const*, const PxTransform*, PxU32) override {}
};

PxFilterFlags graspFilter(PxFilterObjectAttributes, PxFilterData, PxFilterObjectAttributes, PxFilterData,
                          PxPairFlags& pf, const void*, PxU32) {
    pf = PxPairFlag::eCONTACT_DEFAULT | PxPairFlag::eNOTIFY_TOUCH_FOUND | PxPairFlag::eNOTIFY_TOUCH_LOST;
    return PxFilterFlag::eDEFAULT;
}

glm::vec3 worldCoM(PxRigidDynamic* a) {
    const PxTransform com = a->getGlobalPose() * a->getCMassLocalPose();
    return toGlm(com.p);
}
} // namespace

bool assertPhysicsLocked(PxScene* scene, PxMaterial* jawMat, float appliedForceN) {
    constexpr float eps = 1e-4f;
    bool ok = true;
    const PxVec3 g = scene->getGravity();
    if (std::fabs(g.x) > eps || std::fabs(g.z) > eps || std::fabs(g.y - (-kLockedPhysics.gravity)) > eps) {
        std::printf("    [LOCK FAIL] gravity=(%.4f,%.4f,%.4f) expected (0,%.4f,0)\n", g.x, g.y, g.z, -kLockedPhysics.gravity); ok = false;
    }
    const float sf = jawMat->getStaticFriction(), df = jawMat->getDynamicFriction();
    if (std::fabs(sf - kLockedPhysics.frictionMu) > eps) { std::printf("    [LOCK FAIL] staticFriction=%.4f expected %.4f\n", sf, kLockedPhysics.frictionMu); ok = false; }
    if (std::fabs(df - kLockedPhysics.frictionMu) > eps) { std::printf("    [LOCK FAIL] dynamicFriction=%.4f expected %.4f\n", df, kLockedPhysics.frictionMu); ok = false; }
    if (std::fabs(appliedForceN - kLockedPhysics.gripForceN) > eps) { std::printf("    [LOCK FAIL] gripForceN=%.4f expected %.4f\n", appliedForceN, kLockedPhysics.gripForceN); ok = false; }
    if (ok) std::printf("    [LOCK OK] g.y=%.3f mu_s=%.3f mu_d=%.3f F=%.1fN cfg=%016llx\n",
                        g.y, sf, df, appliedForceN, (unsigned long long)lockedConfigHash());
    return ok;
}

bool graspSucceeded(const GraspResult& r) {
    const bool c1 = r.centerErrM < kLockedPhysics.successDistM;     // lifted to target pose
    const bool c2 = !r.groundContactAfterLiftoff;                   // never re-touched the ground
    const bool c3 = r.contactFrac >= kLockedPhysics.contactFrac;    // continuous jaw grip
    return r.physicsLocked && c1 && c2 && c3;
}

GraspResult runGripperSim(const RenderableMeshComponent& objectMesh, const GraspSpec& grasp, const WorldOverride& world) {
    GraspResult R;
    PxPhysics* phys = &PxGetPhysics();
    SimulationController::ensurePhysxExtensions();
    auto& cook = CollisionCookingService::instance();
    if (!cook.isInitialized()) cook.initialize(phys);

    const float gravity = world.softened ? world.gravity : kLockedPhysics.gravity;
    const float muUse   = world.softened ? world.frictionMu : kLockedPhysics.frictionMu;
    const float Fgrip   = kLockedPhysics.gripForceN;     // squeeze is ALWAYS the locked force (never softened)
    const float dt      = kLockedPhysics.fixedDt;

    PxDefaultCpuDispatcher* disp = PxDefaultCpuDispatcherCreate(2);
    PxSceneDesc sd(phys->getTolerancesScale());
    sd.gravity = PxVec3(0.0f, -gravity, 0.0f);
    sd.cpuDispatcher = disp;
    sd.filterShader = graspFilter;
    sd.solverType = PxSolverType::eTGS;
    sd.flags |= PxSceneFlag::eENABLE_CCD | PxSceneFlag::eENABLE_STABILIZATION;
    PxScene* scene = phys->createScene(sd);
    Tracker tracker; scene->setSimulationEventCallback(&tracker);

    PxMaterial* mat = phys->createMaterial(muUse, muUse, 0.0f);
    R.physicsLocked = assertPhysicsLocked(scene, mat, Fgrip);   // the guard: softened world -> false

    // ground plane.
    PxRigidStatic* ground = PxCreatePlane(*phys, PxPlane(0, 1, 0, 0), *mat);
    ground->userData = (void*)(intptr_t)TAG_GROUND;
    scene->addActor(*ground);

    // object: cooked convex decomposition, seated with AABB bottom on the ground, CoM over the origin.
    const MeshMetrics mm = computeMetrics(objectMesh);
    R.objectMassKg = float(kLockedPhysics.densityKgM3) * float(mm.volume);
    std::vector<PxConvexMesh*> hulls = cook.requestConvexDecomposition(objectMesh.vertices, objectMesh.indices, "grasp_obj").get();
    if (hulls.empty()) { scene->release(); disp->release(); return R; }
    const PxTransform objPose(PxVec3(-float(mm.centroid.x), -float(mm.aabbMin.y), -float(mm.centroid.z)));
    PxRigidDynamic* obj = phys->createRigidDynamic(objPose);
    for (PxConvexMesh* h : hulls) { PxShape* s = phys->createShape(PxConvexMeshGeometry(h, PxMeshScale(1.0f)), *mat, true); obj->attachShape(*s); s->release(); }
    PxRigidBodyExt::setMassAndUpdateInertia(*obj, R.objectMassKg);
    obj->setRigidBodyFlag(PxRigidBodyFlag::eENABLE_CCD, true);
    obj->userData = (void*)(intptr_t)TAG_OBJ;
    scene->addActor(*obj);

    // grasp frame.
    const float comH = float(mm.centroid.y - mm.aabbMin.y);
    const glm::vec3 graspC = glm::vec3(0.0f, comH, 0.0f) + grasp.centerOffset;
    const glm::quat q = glm::normalize(grasp.approach);
    const glm::vec3 closeAxis = q * glm::vec3(1, 0, 0);
    const float half = grasp.jawSpanM * 0.5f;

    // VISE gripper: a KINEMATIC palm carrying a rigid ANVIL pad (the fixed jaw) on one side; the object is
    // squeezed against the anvil by a single DYNAMIC FINGER on the other side, force-driven with the locked
    // gripForceN. The anvil pins the object on its side (no unphysical along-axis drift) while the finger's
    // bounded force + friction is the ONLY thing holding it in the other directions -- so a poor contact lets
    // the object rotate/slip out (honest), and the squeeze can never exceed the finger's force (no kinematic clamp).
    PxRigidDynamic* palm = phys->createRigidDynamic(PxTransform(toPx(graspC), toPxQ(q)));
    palm->setRigidBodyFlag(PxRigidBodyFlag::eKINEMATIC, true);
    palm->userData = (void*)(intptr_t)TAG_JAW1;            // the anvil side
    { PxShape* anvil = phys->createShape(PxBoxGeometry(0.006f, 0.045f, 0.045f), *mat, true);
      anvil->setLocalPose(PxTransform(PxVec3(-half, 0.0f, 0.0f)));   // pad at -closing-axis side
      palm->attachShape(*anvil); anvil->release(); }
    scene->addActor(*palm);

    PxRigidDynamic* finger = phys->createRigidDynamic(PxTransform(toPx(graspC + closeAxis * half), toPxQ(q)));
    { PxShape* s = phys->createShape(PxBoxGeometry(0.006f, 0.045f, 0.045f), *mat, true); finger->attachShape(*s); s->release(); }
    PxRigidBodyExt::setMassAndUpdateInertia(*finger, 0.30f);
    finger->setRigidBodyFlag(PxRigidBodyFlag::eENABLE_CCD, true);
    finger->setMaxLinearVelocity(1.0f);                   // gentle close (no slam); above liftSpeed so it follows the lift
    finger->userData = (void*)(intptr_t)TAG_JAW0;         // the moving-jaw side
    scene->addActor(*finger);
    PxD6Joint* d6 = PxD6JointCreate(*phys, palm, PxTransform(PxVec3(half, 0, 0)), finger, PxTransform(PxIdentity));
    d6->setMotion(PxD6Axis::eX, PxD6Motion::eFREE);       // closing axis -- the finger slides under the squeeze force
    d6->setMotion(PxD6Axis::eY, PxD6Motion::eLOCKED);
    d6->setMotion(PxD6Axis::eZ, PxD6Motion::eLOCKED);
    d6->setMotion(PxD6Axis::eTWIST, PxD6Motion::eLOCKED);
    d6->setMotion(PxD6Axis::eSWING1, PxD6Motion::eLOCKED);
    d6->setMotion(PxD6Axis::eSWING2, PxD6Motion::eLOCKED);

    auto squeeze = [&]() { finger->addForce(toPx(-closeAxis * Fgrip), PxForceMode::eFORCE); };   // press toward anvil
    auto stepOnce = [&]() { scene->simulate(dt); scene->fetchResults(true); };

    // SETTLE (no force): object rests, finger open.
    for (int s = 0; s < 120; ++s) stepOnce();
    // CLOSE (finger presses with gripForceN); early-exit on 24 consec both-pad contacts.
    int consec = 0;
    for (int s = 0; s < 480; ++s) {
        squeeze(); stepOnce();
        consec = (tracker.objJaw0 && tracker.objJaw1) ? consec + 1 : 0;
        if (consec >= 24) break;
    }
    // SQUEEZE settle: firm the grip before lifting.
    for (int s = 0; s < 120; ++s) { squeeze(); stepOnce(); }
    R.grippedAtLiftoff = tracker.objJaw0 && tracker.objJaw1;
    const bool dbg = std::getenv("KRS_GRASP_DEBUG") != nullptr;
    if (dbg) {
        const glm::vec3 oc = worldCoM(obj);
        const PxVec3 fp = finger->getGlobalPose().p;
        std::printf("    [dbg] post-squeeze objCoM=(%.3f,%.3f,%.3f) finger=(%.3f,%.3f,%.3f) anvilT=%d fingerT=%d gnd=%d closeAx=(%.2f,%.2f,%.2f) graspC=(%.3f,%.3f,%.3f)\n",
                    oc.x, oc.y, oc.z, fp.x, fp.y, fp.z, tracker.objJaw1, tracker.objJaw0, tracker.objGround,
                    closeAxis.x, closeAxis.y, closeAxis.z, graspC.x, graspC.y, graspC.z);
    }

    // LIFT (force, palm ascends at liftSpeed); contact accounting starts.
    const int liftSteps = int(std::lround(kLockedPhysics.liftHeightM / kLockedPhysics.liftSpeedMps / dt));
    const int holdSteps = int(std::lround(kLockedPhysics.holdSeconds / dt));
    R.windowSteps = liftSteps + holdSteps;
    bool hasLiftedOff = false; bool groundAfter = false;
    for (int s = 0; s < liftSteps; ++s) {
        const float y = graspC.y + kLockedPhysics.liftSpeedMps * dt * float(s + 1);
        palm->setKinematicTarget(PxTransform(toPx(glm::vec3(graspC.x, y, graspC.z)), toPxQ(q)));
        squeeze(); stepOnce();
        if (s == 0) R.startCenter = worldCoM(obj);
        if (tracker.objJaw0 && tracker.objJaw1) ++R.contactSteps;
        if (!tracker.objGround) hasLiftedOff = true;
        if (hasLiftedOff && tracker.objGround) groundAfter = true;
        if (dbg && (s % 36 == 0 || s == liftSteps - 1)) {
            const glm::vec3 oc = worldCoM(obj);
            std::printf("    [dbg-lift s=%3d] palmY=%.3f objCoM=(%.3f,%.3f,%.3f) j0t=%d j1t=%d\n", s, y, oc.x, oc.y, oc.z, tracker.objJaw0, tracker.objJaw1);
        }
    }
    // HOLD (force, palm frozen at apex).
    const float apexY = graspC.y + kLockedPhysics.liftHeightM;
    for (int s = 0; s < holdSteps; ++s) {
        palm->setKinematicTarget(PxTransform(toPx(glm::vec3(graspC.x, apexY, graspC.z)), toPxQ(q)));
        squeeze(); stepOnce();
        if (tracker.objJaw0 && tracker.objJaw1) ++R.contactSteps;
        if (!tracker.objGround) hasLiftedOff = true;
        if (hasLiftedOff && tracker.objGround) groundAfter = true;
    }
    R.endCenter = worldCoM(obj);
    R.targetCenter = R.startCenter + glm::vec3(0.0f, kLockedPhysics.liftHeightM, 0.0f);
    R.centerErrM = glm::length(R.endCenter - R.targetCenter);
    R.groundContactAfterLiftoff = groundAfter || !hasLiftedOff;     // never lifting off counts as a failure too
    R.contactFrac = R.windowSteps ? float(R.contactSteps) / float(R.windowSteps) : 0.0f;
    R.ok = graspSucceeded(R);

    scene->release();
    disp->release();
    return R;
}

} // namespace krs::grasp
#endif
