// GraspGate_Success.cpp -- GATE SUCCESS-CRITERION (Phase 2): the ANTI-CHEAT heart. The success test must
// PASS a known-good grasp and FAIL a known-bad grasp under the SAME LOCKED physics, AND a softened world
// (gravity 0 / sticky friction) that would FALSELY pass the bad grasp must be REJECTED by the live-physics
// guard. Three runs on 036_wood_block:
//   Run 1  GOOD grasp, LOCKED world        -> PASS (object lifted + held).
//   Run 2  BAD grasp, SAME LOCKED world    -> FAIL (object slips out -- the criterion has teeth).
//   Run 3  BAD grasp, SOFTENED world (sticky mu=5.0) -> the three success clauses FALSELY read PASS, BUT assertPhysicsLocked
//                                            trips (friction != 0.70) so the verdict is FAIL. This proves the
//                                            criterion is non-trivially passable (softening DOES rescue a bad
//                                            grasp) AND that softening is un-fakeable (the guard catches it).
// This is what makes every later success number trustworthy.
#include "GraspGates.hpp"
#include <cstdio>

#if !defined(KR_WITH_PHYSX)
namespace krs::grasp { bool runGraspSuccessGate() { std::printf("[GRASP GATE SUCCESS-CRITERION] SKIP (no PhysX)\n"); return true; } }
#else

#include "GraspPhysicsConfig.hpp"
#include "GraspSim.hpp"
#include "YcbCatalog.hpp"
#include "MeshUtils.hpp"
#include <glm/gtc/quaternion.hpp>
#include <cmath>

namespace krs::grasp {

static void report(const char* tag, const GraspResult& r) {
    std::printf("  %-22s mass=%.3f kg gripped=%d centerErr=%.4f m groundAfter=%d contactFrac=%.3f jawF=%.1fN locked=%d -> %s\n",
                tag, r.objectMassKg, r.grippedAtLiftoff ? 1 : 0, r.centerErrM,
                r.groundContactAfterLiftoff ? 1 : 0, r.contactFrac, r.maxJawForceN, r.physicsLocked ? 1 : 0,
                r.ok ? "SUCCESS" : "fail");
}

bool runGraspSuccessGate() {
    std::printf("\n[GRASP GATE SUCCESS-CRITERION] good passes / bad fails (locked) + softened-world anti-cheat  (locked-cfg %016llx)\n",
                (unsigned long long)lockedConfigHash());

    YcbObject o{}; for (const auto& c : ycbCatalog()) if (c.id == "036_wood_block") o = c;
    RenderableMeshComponent mesh;
    try { mesh = MeshUtils::loadMeshFromFile(o.meshPath()); }
    catch (const std::exception& e) { std::printf("  load failed: %s\n", e.what()); return false; }

    // GOOD: closing axis = world +X, perpendicular to the block's flat faces, through the CoM.
    GraspSpec good; good.centerOffset = glm::vec3(0.0f); good.approach = glm::quat(1, 0, 0, 0); good.jawSpanM = 0.13f;
    // BAD: grip the long block near one END (off-centre 0.08 m along its long axis). It DOES grip, but the long
    // overhang's weight torques the block out of the grip during the lift -- the friction cone at mu=0.70 cannot
    // hold the rotation, so it slips and re-touches the ground. Sticky friction (the softened world) CAN hold it.
    GraspSpec bad; bad.centerOffset = glm::vec3(0.0f, 0.0f, 0.08f);
    bad.approach = glm::quat(1, 0, 0, 0); bad.jawSpanM = 0.13f;

    const WorldOverride locked{};                       // kLockedPhysics verbatim
    // the cheat: STICKY friction (mu 5.0 vs locked 0.70). The locked grip is strong (mu*F*2 ~ 56 N), so a
    // bad grasp fails by SLIP, not lack-of-weight -- sticky friction is the softening that rescues a slip,
    // and the guard catches mu != 0.70. (A no-gravity world instead exposes squeeze-ejection, a different mode.)
    WorldOverride soft; soft.softened = true; soft.frictionMu = 5.0f;

    const GraspResult r1 = runGripperSim(mesh, good, locked);  report("run1 GOOD/locked", r1);
    const GraspResult r2 = runGripperSim(mesh, bad,  locked);  report("run2 BAD /locked", r2);
    const GraspResult r3 = runGripperSim(mesh, bad,  soft);    report("run3 BAD /SOFTENED", r3);

    // r3's three success clauses, evaluated WITHOUT the guard precondition -- to show the softened world DOES
    // rescue the bad grasp (the criterion is not trivially passable), while the guard is what rejects it.
    const bool r3clauses = (r3.centerErrM < kLockedPhysics.successDistM) && !r3.groundContactAfterLiftoff
                           && (r3.contactFrac >= kLockedPhysics.contactFrac);

    const bool t_good   = r1.ok;                          // good grasp succeeds under the lock
    // SAME locked physics rejects the bad grasp -- AND rejects it by a COMFORTABLE MARGIN (not a knife-edge that
    // a small change could flip): either the object missed the target by >2x the success distance, or it fell
    // back to the ground. A "barely fails" bad grasp would mean the criterion's discriminating power had quietly
    // collapsed while still reading green.
    const bool badFailsHard = (r2.centerErrM > 2.0f * kLockedPhysics.successDistM) || r2.groundContactAfterLiftoff;
    const bool t_bad    = !r2.ok && r2.physicsLocked && badFailsHard;
    const bool t_cheat  = r3clauses;                      // softened world DOES make the bad grasp 'succeed'
    const bool t_guard  = !r3.physicsLocked && !r3.ok;    // ...but the guard catches the softening -> still FAIL

    // FORCE-BOUNDEDNESS (the kinematic-anvil anti-cheat): during HOLD the only thing pressing the object into
    // the kinematic anvil is the finger's locked gripForceN, so the measured jaw->object contact force must be
    // REAL (the grip exists) yet BOUNDED near gripForceN -- NOT an unbounded clamp a kinematic jaw could fake.
    // (An infinite-grip anvil would read hundreds of N here; we require < 2x the locked force.)
    const float Flock = kLockedPhysics.gripForceN;
    const bool t_force = (r1.maxJawForceN > 0.5f * Flock) && (r1.maxJawForceN < 2.0f * Flock);

    std::printf("  anti-cheat: bad-grasp clauses in softened world = %s (cheat works) ; guard rejected softening = %s\n",
                t_cheat ? "PASS" : "fail", (!r3.physicsLocked) ? "yes" : "NO");
    std::printf("  force-bound: good-grasp peak jaw force = %.1f N (locked grip %.1f N) -> %s (real grip, bounded -- no infinite clamp)\n",
                r1.maxJawForceN, Flock, t_force ? "PASS" : "FAIL");

    const bool pass = t_good && t_bad && t_cheat && t_guard && t_force;
    std::printf("[GRASP GATE SUCCESS-CRITERION] good=%d bad-fails=%d softened-cheats=%d guard-catches=%d force-bounded=%d -> %s\n",
                t_good, t_bad, t_cheat, t_guard, t_force, pass ? "PASS" : "FAIL");
    return pass;
}

} // namespace krs::grasp
#endif
