// GraspGate_Coacd.cpp -- GATE COACD (Phase 1): the collider must be GRASP-FAITHFUL, i.e. preserve concavities,
// not fill them. Physical discriminator: orient a real concave object (024_bowl) opening-up, drop a small ball
// onto its axis, settle under gravity, and measure the ball's rest height vs the bowl rim.
//   - cavity-PRESERVING colliders (exact triangle mesh; V-HACD convex decomposition) -> ball rests INSIDE,
//     well below the rim;
//   - the cavity-FILLING NEG-CONTROL (single convex hull -> a solid dome) -> ball rests ON TOP, at/above the rim.
// The separation between "preserving" and "filling" is the proof the collider is grasp-faithful (a gripper
// can reach into the cavity), not a solid lump. (CoACD is the named Phase-2 swap for the decomposition backend
// behind CollisionCookingService; it must clear this identical drop test -- see ROADMAP §GR.)
#include "GraspGates.hpp"
#include <cstdio>

#if !defined(KR_WITH_PHYSX)
namespace krs::grasp { bool runGraspCoacdGate() { std::printf("[GRASP GATE COACD] SKIP (no PhysX)\n"); return true; } }
#else

#include "GraspPhysicsConfig.hpp"
#include "YcbCatalog.hpp"
#include "GraspMesh.hpp"
#include "MeshUtils.hpp"
#include "CollisionCookingService.hpp"
#include "SimulationController.hpp"
#include "GraspSim.hpp"           // assertPhysicsLocked (the live-physics guard)
#include <PxPhysicsAPI.h>
#include <functional>
#include <vector>
#include <cstdio>
#include <cmath>

using namespace physx;

namespace krs::grasp {

namespace {
constexpr float kBallRadius = 0.01f;     // 1 cm ball -- small enough to enter a bowl, large enough not to tunnel
constexpr float kDropAbove  = 0.05f;     // spawn 5 cm above the rim
constexpr int   kSettleSteps = 480;      // 2.0 s at 1/240 (the locked fixedDt)

// Drop a ball onto a bowl whose collider is attached by `attach`, with the bowl rotated by `q`. Returns the
// ball's full final position + the bowl world rim-Y and rim radius (so "caught inside" can require the ball
// be BOTH low (in the cavity Y-band) AND near the axis -- an escaped ball that fell into the void or rolled
// off cannot read as caught).
struct Drop { float restY, restX, restZ, rimY, floorY, rimRadius; };

Drop runDrop(PxPhysics* phys, PxMaterial* mat, const PxQuat& q,
             const glm::dvec3& aabbMin, const glm::dvec3& aabbMax,
             const std::function<void(PxRigidStatic*, PxMaterial*)>& attach) {
    PxDefaultCpuDispatcher* disp = PxDefaultCpuDispatcherCreate(2);
    PxSceneDesc sd(phys->getTolerancesScale());
    sd.gravity = PxVec3(0.0f, -kLockedPhysics.gravity, 0.0f);
    sd.cpuDispatcher = disp;
    sd.filterShader = PxDefaultSimulationFilterShader;
    sd.flags |= PxSceneFlag::eENABLE_CCD;                       // no tunnelling through the thin shell
    PxScene* scene = phys->createScene(sd);

    // world extents of the rotated bowl (rotate the 8 local-AABB corners), to seat its base at y=0.
    float wymin = 1e30f, wymax = -1e30f, wxmax = 0.0f, wzmax = 0.0f;
    for (int c = 0; c < 8; ++c) {
        const PxVec3 lc((c & 1) ? float(aabbMax.x) : float(aabbMin.x),
                        (c & 2) ? float(aabbMax.y) : float(aabbMin.y),
                        (c & 4) ? float(aabbMax.z) : float(aabbMin.z));
        const PxVec3 wc = q.rotate(lc);
        wymin = std::min(wymin, wc.y); wymax = std::max(wymax, wc.y);
        wxmax = std::max(wxmax, std::fabs(wc.x)); wzmax = std::max(wzmax, std::fabs(wc.z));
    }
    const float rimY = wymax - wymin, floorY = 0.0f, rimRadius = std::max(wxmax, wzmax);
    PxRigidStatic* bowl = phys->createRigidStatic(PxTransform(PxVec3(0.0f, -wymin, 0.0f), q));
    attach(bowl, mat);
    scene->addActor(*bowl);

    PxRigidDynamic* ball = PxCreateDynamic(*phys, PxTransform(PxVec3(0.0f, rimY + kDropAbove, 0.0f)),
                                           PxSphereGeometry(kBallRadius), *mat, 1000.0f);
    ball->setRigidBodyFlag(PxRigidBodyFlag::eENABLE_CCD, true);
    scene->addActor(*ball);

    for (int s = 0; s < kSettleSteps; ++s) { scene->simulate(kLockedPhysics.fixedDt); scene->fetchResults(true); }
    const PxVec3 bp = ball->getGlobalPose().p;

    scene->release();
    disp->release();
    return { bp.y, bp.x, bp.z, rimY, floorY, rimRadius };
}
} // namespace

bool runGraspCoacdGate() {
    if (ycbSkipIfAbsent("GRASP GATE COACD")) return true;
    std::printf("\n[GRASP GATE COACD] concavity survives (ball rests INSIDE) vs convex-hull filler FAILS  (locked-cfg %016llx)\n",
                (unsigned long long)lockedConfigHash());

    PxPhysics* phys = &PxGetPhysics();
    SimulationController::ensurePhysxExtensions();
    auto& cook = CollisionCookingService::instance();
    if (!cook.isInitialized()) cook.initialize(phys);
    PxMaterial* mat = phys->createMaterial(kLockedPhysics.frictionMu, kLockedPhysics.frictionMu, 0.0f);

    // LIVE-physics guard: assert the scene this gate builds matches the locked config (gravity + friction).
    bool guardOk = false;
    { PxDefaultCpuDispatcher* d = PxDefaultCpuDispatcherCreate(1);
      PxSceneDesc gd(phys->getTolerancesScale()); gd.gravity = PxVec3(0.0f, -kLockedPhysics.gravity, 0.0f);
      gd.cpuDispatcher = d; gd.filterShader = PxDefaultSimulationFilterShader;
      PxScene* gs = phys->createScene(gd);
      guardOk = assertPhysicsLocked(gs, mat, kLockedPhysics.gripForceN);
      gs->release(); d->release(); }

    // bowl mesh + AABB.
    YcbObject bowl{}; for (const auto& o : ycbCatalog()) if (o.id == "024_bowl") bowl = o;
    RenderableMeshComponent mesh;
    try { mesh = MeshUtils::loadMeshFromFile(bowl.meshPath()); }
    catch (const std::exception& e) { std::printf("  load failed: %s\n", e.what()); return false; }
    const MeshMetrics mm = computeMetrics(mesh);

    // cook the three colliders (blocking .get()).
    PxTriangleMesh* tm = cook.requestTriangleMesh(mesh.vertices, mesh.indices, "bowl_trimesh").get();
    std::vector<PxConvexMesh*> hulls = cook.requestConvexDecomposition(mesh.vertices, mesh.indices, "bowl_vhacd").get();
    PxConvexMesh* hull = cook.requestConvexHull(mesh.vertices, "bowl_hull").get();
    if (!tm || hulls.empty() || !hull) {
        std::printf("  cook failed: trimesh=%p hulls=%zu hull=%p\n", (void*)tm, hulls.size(), (void*)hull);
        return false;
    }

    auto attachTri = [&](PxRigidStatic* a, PxMaterial* mt) {
        PxShape* s = phys->createShape(PxTriangleMeshGeometry(tm, PxMeshScale(1.0f)), *mt); a->attachShape(*s); s->release();
    };
    auto attachDecomp = [&](PxRigidStatic* a, PxMaterial* mt) {
        for (PxConvexMesh* h : hulls) { PxShape* s = phys->createShape(PxConvexMeshGeometry(h, PxMeshScale(1.0f)), *mt); a->attachShape(*s); s->release(); }
    };
    auto attachHull = [&](PxRigidStatic* a, PxMaterial* mt) {
        PxShape* s = phys->createShape(PxConvexMeshGeometry(hull, PxMeshScale(1.0f)), *mt); a->attachShape(*s); s->release();
    };

    // The bowl's cavity axis is its shortest extent; +-90deg about X maps it to +-world-Y. Auto-orient
    // opening-UP using the GROUND-TRUTH trimesh: pick the rotation where the ball is CAUGHT below the rim.
    const PxQuat qUp(-PxHalfPi, PxVec3(1, 0, 0)), qDn(+PxHalfPi, PxVec3(1, 0, 0));
    Drop tA = runDrop(phys, mat, qUp, mm.aabbMin, mm.aabbMax, attachTri);
    PxQuat q = qUp;
    if (tA.restY - tA.rimY > -0.02f) { Drop tA2 = runDrop(phys, mat, qDn, mm.aabbMin, mm.aabbMax, attachTri); if (tA2.restY - tA2.rimY < tA.restY - tA.rimY) { q = qDn; tA = tA2; } }

    const Drop dB = runDrop(phys, mat, q, mm.aabbMin, mm.aabbMax, attachDecomp);   // V-HACD (concavity-preserving)
    const Drop dC = runDrop(phys, mat, q, mm.aabbMin, mm.aabbMax, attachHull);     // single convex hull (filling)

    const float depth = tA.rimY - tA.floorY;
    // "caught inside" is two-sided: the ball must rest in the LOWER cavity Y-band (low enough to be inside,
    // not below the floor) AND be near the bowl axis. This rejects an escaped ball (fell into the void -> very
    // low / off-axis) AND a partially-filled cavity (ball not deep enough). Purely one-sided "low Y" cannot.
    auto caught = [&](const Drop& d) {
        const float clr = d.restY - d.rimY;
        const float horiz = std::sqrt(d.restX * d.restX + d.restZ * d.restZ);
        return (clr < -0.7f * depth) && (clr > -(depth + 2.0f * kBallRadius)) && (horiz < d.rimRadius);
    };
    const float clrA = tA.restY - tA.rimY, clrB = dB.restY - dB.rimY, clrC = dC.restY - dC.rimY;

    const bool oracle_trim = caught(tA);                  // trimesh: the orientation ORACLE (selected to catch), not scored
    const bool t_vhacd     = caught(dB);                  // V-HACD -> caught inside (concavity preserved)  [SCORED]
    const bool t_hull      = !caught(dC);                 // convex hull -> NOT caught (cavity filled, ball on top) [SCORED]
    const bool t_sep       = (clrC - clrB) > 0.02f;       // clear preserve-vs-fill separation [SCORED]

    std::printf("  bowl rim=%.4f floor=%.4f depth=%.4f rimR=%.4f hulls=%zu orient=%s  guard=%s\n",
                tA.rimY, tA.floorY, depth, tA.rimRadius, hulls.size(), (q.x < 0 ? "up" : "flip"), guardOk ? "LOCKED" : "FAIL");
    std::printf("  trimesh (ORACLE)    : clr=%+.4f horiz=%.4f -> %s (opening-up oracle, not scored)\n", clrA, std::sqrt(tA.restX * tA.restX + tA.restZ * tA.restZ), oracle_trim ? "caught" : "miss");
    std::printf("  V-HACD  (decomp)    : clr=%+.4f horiz=%.4f -> %s (cavity preserved)\n", clrB, std::sqrt(dB.restX * dB.restX + dB.restZ * dB.restZ), t_vhacd ? "ok" : "FAIL");
    std::printf("  NEG-CTRL convex hull: clr=%+.4f horiz=%.4f -> %s (cavity FILLED, ball not caught)\n", clrC, std::sqrt(dC.restX * dC.restX + dC.restZ * dC.restZ), t_hull ? "ok" : "FAIL");
    std::printf("  separation (hull-vhacd)=%.4f (>0.02) -> %s\n", clrC - clrB, t_sep ? "ok" : "FAIL");

    const bool pass = guardOk && oracle_trim && t_vhacd && t_hull && t_sep;   // oracle must catch (sanity), V-HACD+hull+sep scored
    std::printf("[GRASP GATE COACD] %s\n", pass ? "PASS" : "FAIL");
    return pass;
}

} // namespace krs::grasp
#endif
