#pragma once
// GraspSim.hpp -- headless parallel-jaw gripper simulation + the LOCKED grasp-success verdict + the
// live-physics guard. A KINEMATIC palm carries two DYNAMIC jaws via D6 joints that slide only on the closing
// axis; the jaws squeeze under a finite locked force (addForce, gripForceN) so the object is held ONLY by the
// friction cone -- a bad grasp slips out. Object = cooked convex-decomposition collider (never a solid hull),
// mass = densityKgM3 * enclosed volume. The verdict implements kLockedPhysics EXACTLY; assertPhysicsLocked()
// re-reads the LIVE scene/material/force and fails on any drift. No knob here lives outside kLockedPhysics.
#include "GraspPhysicsConfig.hpp"
#include "components.hpp"          // RenderableMeshComponent
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace physx { class PxScene; class PxMaterial; }

namespace krs::grasp {

// The grasp the planner proposes, RELATIVE to the object's resting centre of mass (so it is pose-independent).
struct GraspSpec {
    glm::vec3 centerOffset{0.0f};   // grasp centre = resting object CoM + this (0 = grasp at the CoM)
    glm::quat approach{1, 0, 0, 0}; // gripper frame; closing axis = approach * (1,0,0); identity = world +X
    float     jawSpanM = 0.12f;     // open jaw separation before closing (m); must exceed object width on the axis
};

// Physics overrides for the anti-cheat negative control. The LOCKED world uses NONE of these (it is the ruler).
struct WorldOverride {
    bool  softened   = false;                         // false => kLockedPhysics verbatim
    float gravity    = kLockedPhysics.gravity;        // softened may set 0 (object can't fall)
    float frictionMu = kLockedPhysics.frictionMu;     // softened may set e.g. 5.0 (sticky)
};

// Everything the verdict reads (all measured from the live sim).
struct GraspResult {
    bool      ok = false;                  // the full LOCKED verdict (guard AND the three clauses)
    glm::vec3 startCenter{0.0f};           // object centre at the first LIFT step
    glm::vec3 endCenter{0.0f};             // object centre at the end of HOLD
    glm::vec3 targetCenter{0.0f};          // startCenter + liftHeightM up
    float     centerErrM = 1e30f;          // |endCenter - targetCenter|              (clause 1)
    bool      groundContactAfterLiftoff = true;   // object re-touched the ground post-liftoff (clause 2, inverted)
    int       contactSteps = 0;            // LIFT+HOLD steps with BOTH jaws touching the object
    int       windowSteps  = 0;            // = liftSteps + holdSteps (the denominator)
    float     contactFrac  = 0.0f;         // contactSteps / windowSteps              (clause 3)
    bool      grippedAtLiftoff = false;    // did both jaws ever close on the object before lift
    float     objectMassKg = 0.0f;         // densityKgM3 * volume (logged + Phase-0 mass re-assert)
    bool      physicsLocked = false;       // assertPhysicsLocked() verdict for this run's LIVE scene
    float     maxJawForceN = 0.0f;         // peak per-substep jaw->object contact force during HOLD (impulse/dt);
                                           // PROVES the squeeze is bounded by ~gripForceN (anvil is no infinite clamp)
    float     medianJawForceN = 0.0f;      // MEDIAN per-substep jaw->object contact force over LIFT+HOLD. Peak>>median
                                           // means an UNBOUNDED peak is a TRANSIENT solver spike, not a sustained wedge
                                           // (the fidelity UNBOUNDED-DIAGNOSIS reads this to call real-vs-artifact).
};

// Run the full settle->close->lift->hold sequence for one grasp on one object mesh, under `world` (LOCKED
// unless world.softened). Seats the object on a ground plane. The OBJECT COLLIDER is the ONLY thing the
// `coacdPath` argument changes: if it names a loadable CoACD parts file the collider is the precomputed CoACD
// convex decomposition; otherwise (empty/missing) it falls back to the runtime V-HACD decomposition. The
// physics + success criterion are identical in both cases (same configHash) -- the collider is the variable.
GraspResult runGripperSim(const RenderableMeshComponent& objectMesh, const GraspSpec& grasp, const WorldOverride& world,
                          const std::string& coacdPath = "");

// The LOCKED criterion, EXACTLY: centerErrM < successDistM AND no post-liftoff ground contact AND
// contactFrac >= contactFrac AND the live-physics guard held. Pure function of a measured GraspResult.
bool graspSucceeded(const GraspResult& r);

// LIVE-physics guard: reads scene->getGravity() + jawMat static/dynamic friction + the applied squeeze, and
// compares to kLockedPhysics. Returns false (and prints the offending value) on ANY mismatch; prints the
// configHash on success. This is what makes a softened world un-passable.
bool assertPhysicsLocked(physx::PxScene* scene, physx::PxMaterial* jawMat, float appliedForceN);

} // namespace krs::grasp
