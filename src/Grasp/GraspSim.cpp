#include "GraspSim.hpp"

#if !defined(KR_WITH_PHYSX)
#include <cstdio>
namespace krs::grasp {
GraspResult runGripperSim(const RenderableMeshComponent&, const GraspSpec&, const WorldOverride&, const std::string&) { return {}; }
bool graspSucceeded(const GraspResult&) { return false; }
bool assertPhysicsLocked(physx::PxScene*, physx::PxMaterial*, float) { return false; }
}
#else

#include "GraspMesh.hpp"
#include "CoacdCollider.hpp"
#include "CollisionCookingService.hpp"
#include "SimulationController.hpp"
#include "components.hpp"          // Vertex
#include <PxPhysicsAPI.h>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

using namespace physx;

namespace krs::grasp {
namespace {

enum Tag { TAG_NONE = 0, TAG_OBJ = 1, TAG_JAW0 = 2, TAG_JAW1 = 3, TAG_GROUND = 4 };
inline int tagOf(const PxActor* a) { return a ? int(reinterpret_cast<intptr_t>(a->userData)) : 0; }
inline PxVec3 toPx(const glm::vec3& v) { return PxVec3(v.x, v.y, v.z); }
inline PxQuat toPxQ(const glm::quat& q) { return PxQuat(q.x, q.y, q.z, q.w); }
inline glm::vec3 toGlm(const PxVec3& v) { return glm::vec3(v.x, v.y, v.z); }

// per-step contact latches: set on TOUCH_FOUND, cleared on TOUCH_LOST -> reflect the current contact state.
// Also measures the per-step contact NORMAL force on the object from a jaw (impulse/dt) while `recording`, so
// the gate can PROVE the squeeze is bounded by gripForceN (the kinematic anvil cannot clamp harder than the
// finger pushes -- not a hidden infinite grip).
struct Tracker : PxSimulationEventCallback {
    bool objJaw0 = false, objJaw1 = false, objGround = false;
    bool recording = false;
    float maxJawForce = 0.0f;
    void onContact(const PxContactPairHeader& h, const PxContactPair* pairs, PxU32 n) override {
        const int ta = tagOf(h.actors[0]), tb = tagOf(h.actors[1]);
        int other = 0;
        if (ta == TAG_OBJ) other = tb; else if (tb == TAG_OBJ) other = ta; else return;
        bool* latch = (other == TAG_JAW0) ? &objJaw0 : (other == TAG_JAW1) ? &objJaw1 : (other == TAG_GROUND) ? &objGround : nullptr;
        if (!latch) return;
        const bool isJaw = (other == TAG_JAW0 || other == TAG_JAW1);
        for (PxU32 i = 0; i < n; ++i) {
            if (pairs[i].events & PxPairFlag::eNOTIFY_TOUCH_FOUND) *latch = true;
            if (pairs[i].events & PxPairFlag::eNOTIFY_TOUCH_LOST)  *latch = false;
            if (recording && isJaw) {
                PxContactPairPoint pts[24];
                const PxU32 np = pairs[i].extractContacts(pts, 24);
                PxVec3 imp(0.0f);
                for (PxU32 k = 0; k < np; ++k) imp += pts[k].impulse;   // total contact impulse this substep
                const float force = imp.magnitude() / kLockedPhysics.fixedDt;
                if (force > maxJawForce) maxJawForce = force;
            }
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
    pf = PxPairFlag::eCONTACT_DEFAULT | PxPairFlag::eNOTIFY_TOUCH_FOUND | PxPairFlag::eNOTIFY_TOUCH_LOST
       | PxPairFlag::eNOTIFY_TOUCH_PERSISTS    // fire onContact EVERY step while touching (else steady HOLD is silent)
       | PxPairFlag::eNOTIFY_CONTACT_POINTS;   // expose per-substep contact impulse -> prove the squeeze is bounded
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
    const bool c4 = r.maxJawForceN <= kLockedPhysics.maxGripForceFactor * kLockedPhysics.gripForceN; // BOUNDED grip,
                                                                   // not a kinematic phantom clamp (anti-cheat)
    return r.physicsLocked && c1 && c2 && c3 && c4;
}

// Build the object collider. If coacdPath loads, cook each precomputed CoACD convex part into a PxConvexMesh
// (via the same convex-hull cooker the V-HACD path uses, so flags/64-vert cap are identical); else fall back to
// the runtime V-HACD decomposition. Returns the hull list to attach.
static std::vector<PxConvexMesh*> cookObjectCollider(CollisionCookingService& cook,
                                                     const RenderableMeshComponent& objectMesh,
                                                     const std::string& coacdPath) {
    std::vector<std::vector<glm::vec3>> parts;
    if (!coacdPath.empty() && loadCoacdParts(coacdPath, parts)) {
        std::vector<PxConvexMesh*> hulls;
        hulls.reserve(parts.size());
        for (size_t i = 0; i < parts.size(); ++i) {
            std::vector<Vertex> vv(parts[i].size());
            for (size_t k = 0; k < parts[i].size(); ++k) vv[k].position = parts[i][k];   // hull cook uses position only
            PxConvexMesh* h = cook.requestConvexHull(vv, "coacd_part").get();
            if (h) hulls.push_back(h);
        }
        if (!hulls.empty()) return hulls;            // CoACD collider
    }
    return cook.requestConvexDecomposition(objectMesh.vertices, objectMesh.indices, "grasp_obj").get();  // V-HACD fallback
}

GraspResult runGripperSim(const RenderableMeshComponent& objectMesh, const GraspSpec& grasp, const WorldOverride& world,
                          const std::string& coacdPath) {
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

    // object collider, seated with AABB bottom on the ground, CoM over the origin. The collider is the ONLY
    // variable: CoACD precomputed parts if coacdPath loads, else the runtime V-HACD decomposition (fallback).
    const MeshMetrics mm = computeMetrics(objectMesh);
    R.objectMassKg = float(kLockedPhysics.densityKgM3) * float(mm.volume);   // mass is from the render-mesh volume, collider-independent
    std::vector<PxConvexMesh*> hulls = cookObjectCollider(cook, objectMesh, coacdPath);
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

    // SYMMETRIC parallel-jaw gripper: a KINEMATIC palm (frame, no collision shape) holds TWO DYNAMIC FINGERS
    // that slide only along the closing axis (D6, eX free) and press toward the grasp centre, EACH with the
    // locked gripForceN. Because BOTH jaws are force-limited and the squeeze is balanced:
    //   (a) neither jaw can impose an UNBOUNDED clamp -- the phantom-grip cheat is STRUCTURALLY impossible
    //       (there is no kinematic surface the object is pressed against; contact force <= ~gripForceN);
    //   (b) closing does NOT shove the grounded object sideways -- it is pinched in place at the grasp centre,
    //       so a poor grasp SLIPS OUT (honest) instead of being tipped by a one-sided push.
    // The palm raises kinematically during the lift, carrying both fingers (D6 locks Y/Z); the object is then
    // held ONLY by the bounded friction of the two finite-force fingers -- exactly a real parallel-jaw grip.
    PxRigidDynamic* palm = phys->createRigidDynamic(PxTransform(toPx(graspC), toPxQ(q)));
    palm->setRigidBodyFlag(PxRigidBodyFlag::eKINEMATIC, true);   // joint anchor + lift driver; carries no shape
    scene->addActor(*palm);

    // The squeeze is a constant bounded force (addForce, gripForceN) -- a CLEAN finite grip with no clamp. To stop
    // the grip assembly (fingerA-object-fingerB) from sliding freely along the closing axis (both fingers are X
    // -free, so the chain has an unanchored X DOF), each finger carries a PURE DAMPER on its closing axis: it
    // resists only RELATIVE X velocity vs the palm (stiffness 0), so it bleeds off any drift momentum but adds NO
    // normal grip force at steady state (zero X-velocity -> zero damper force). It does not resist the object's
    // Y-slip or rotation, so a bad grasp still fails honestly.
    auto makeFinger = [&](float sideSign, int tag) -> PxRigidDynamic* {
        PxRigidDynamic* f = phys->createRigidDynamic(PxTransform(toPx(graspC + closeAxis * (sideSign * half)), toPxQ(q)));
        PxShape* s = phys->createShape(PxBoxGeometry(0.006f, 0.045f, 0.045f), *mat, true); f->attachShape(*s); s->release();
        PxRigidBodyExt::setMassAndUpdateInertia(*f, 0.30f);
        f->setRigidBodyFlag(PxRigidBodyFlag::eENABLE_CCD, true);
        f->setMaxLinearVelocity(0.35f);                   // moderate close speed (gentle enough not to kick a
                                                          // symmetrically-approached object); raised before LIFT.
        f->userData = (void*)(intptr_t)tag;
        scene->addActor(*f);
        PxD6Joint* j = PxD6JointCreate(*phys, palm, PxTransform(PxVec3(sideSign * half, 0, 0)), f, PxTransform(PxIdentity));
        j->setMotion(PxD6Axis::eX, PxD6Motion::eFREE);    // closing axis -- the finger slides under the squeeze force
        j->setMotion(PxD6Axis::eY, PxD6Motion::eLOCKED);
        j->setMotion(PxD6Axis::eZ, PxD6Motion::eLOCKED);
        j->setMotion(PxD6Axis::eTWIST, PxD6Motion::eLOCKED);
        j->setMotion(PxD6Axis::eSWING1, PxD6Motion::eLOCKED);
        j->setMotion(PxD6Axis::eSWING2, PxD6Motion::eLOCKED);
        j->setDrive(PxD6Drive::eX, PxD6JointDrive(0.0f, 1.5e2f, PX_MAX_F32, false));   // pure damper: kills X drift
        return f;
    };
    PxRigidDynamic* fingerA = makeFinger(+1.0f, TAG_JAW0);   // +closeAxis side -> presses -closeAxis
    PxRigidDynamic* fingerB = makeFinger(-1.0f, TAG_JAW1);   // -closeAxis side -> presses +closeAxis

    bool squeezeOn = false;
    auto squeeze = [&]() {                                   // both jaws press toward the centre with the locked force
        if (!squeezeOn) return;
        fingerA->addForce(toPx(-closeAxis * Fgrip), PxForceMode::eFORCE);
        fingerB->addForce(toPx( closeAxis * Fgrip), PxForceMode::eFORCE);
    };
    auto stepOnce = [&]() { squeeze(); scene->simulate(dt); scene->fetchResults(true); };

    // SETTLE (no squeeze): object rests, fingers open (held by the damper).
    for (int s = 0; s < 120; ++s) stepOnce();
    // CLOSE (fingers press with gripForceN); early-exit on 24 consec both-jaw contacts.
    squeezeOn = true;
    int consec = 0;
    for (int s = 0; s < 480; ++s) {
        stepOnce();
        consec = (tracker.objJaw0 && tracker.objJaw1) ? consec + 1 : 0;
        if (consec >= 24) break;
    }
    // SQUEEZE settle: firm the grip before lifting.
    for (int s = 0; s < 120; ++s) stepOnce();
    // raise the finger speed cap so they can follow the lift (palm rises at liftSpeed); the grip is already seated.
    fingerA->setMaxLinearVelocity(1.0f);
    fingerB->setMaxLinearVelocity(1.0f);
    R.grippedAtLiftoff = tracker.objJaw0 && tracker.objJaw1;
    const bool dbg = std::getenv("KRS_GRASP_DEBUG") != nullptr;
    if (dbg) {
        const glm::vec3 oc = worldCoM(obj);
        const PxVec3 fa = fingerA->getGlobalPose().p, fb = fingerB->getGlobalPose().p;
        std::printf("    [dbg] post-squeeze objCoM=(%.3f,%.3f,%.3f) jawA=(%.3f,%.3f,%.3f) jawB=(%.3f,%.3f,%.3f) jawAT=%d jawBT=%d gnd=%d closeAx=(%.2f,%.2f,%.2f) graspC=(%.3f,%.3f,%.3f)\n",
                    oc.x, oc.y, oc.z, fa.x, fa.y, fa.z, fb.x, fb.y, fb.z, tracker.objJaw0, tracker.objJaw1, tracker.objGround,
                    closeAxis.x, closeAxis.y, closeAxis.z, graspC.x, graspC.y, graspC.z);
    }

    // LIFT (force, palm ascends at liftSpeed); contact accounting starts.
    const int liftSteps = int(std::lround(kLockedPhysics.liftHeightM / kLockedPhysics.liftSpeedMps / dt));
    const int holdSteps = int(std::lround(kLockedPhysics.holdSeconds / dt));
    R.windowSteps = liftSteps + holdSteps;
    bool hasLiftedOff = false; bool groundAfter = false;
    // Record jaw->object contact force across BOTH LIFT and HOLD: LIFT is the palm-acceleration phase where a
    // disguised infinite clamp or inertial spike would manifest most strongly -- recording only the steady HOLD
    // would leave that window untested. The peak over lift+hold must still be bounded near gripForceN.
    tracker.recording = true;
    for (int s = 0; s < liftSteps; ++s) {
        const float y = graspC.y + kLockedPhysics.liftSpeedMps * dt * float(s + 1);
        palm->setKinematicTarget(PxTransform(toPx(glm::vec3(graspC.x, y, graspC.z)), toPxQ(q)));
        stepOnce();
        if (s == 0) R.startCenter = worldCoM(obj);
        if (tracker.objJaw0 && tracker.objJaw1) ++R.contactSteps;
        if (!tracker.objGround) hasLiftedOff = true;
        if (hasLiftedOff && tracker.objGround) groundAfter = true;
        if (dbg && (s % 36 == 0 || s == liftSteps - 1)) {
            const glm::vec3 oc = worldCoM(obj);
            std::printf("    [dbg-lift s=%3d] palmY=%.3f objCoM=(%.3f,%.3f,%.3f) j0t=%d j1t=%d\n", s, y, oc.x, oc.y, oc.z, tracker.objJaw0, tracker.objJaw1);
        }
    }
    // HOLD (palm frozen at apex). Force recording continues from LIFT (proves the squeeze is bounded by
    // gripForceN across the whole high-force window -- both force-limited jaws cannot press harder than the grip).
    const float apexY = graspC.y + kLockedPhysics.liftHeightM;
    for (int s = 0; s < holdSteps; ++s) {
        palm->setKinematicTarget(PxTransform(toPx(glm::vec3(graspC.x, apexY, graspC.z)), toPxQ(q)));
        stepOnce();
        if (tracker.objJaw0 && tracker.objJaw1) ++R.contactSteps;
        if (!tracker.objGround) hasLiftedOff = true;
        if (hasLiftedOff && tracker.objGround) groundAfter = true;
    }
    tracker.recording = false;
    R.maxJawForceN = tracker.maxJawForce;
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
