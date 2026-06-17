#pragma once
// GraspTaxonomy.hpp -- the shared failure taxonomy used by the scale gates. Same modes as GATE FAILURE-CATALOG,
// classified from a measured GraspResult so 100% of failures are accounted for. Checked in physical-root-cause
// order; the first matching test wins.
#include "GraspPhysicsConfig.hpp"
#include "GraspSim.hpp"

namespace krs::grasp {

enum FailMode { FM_SUCCESS = 0, FM_NO_GRASP, FM_NOT_SEATED, FM_UNBOUNDED, FM_SLIP_FELL, FM_INTERMITTENT, FM_DRIFT, FM_COUNT };
inline const char* failModeName(int m) {
    static const char* n[FM_COUNT] = { "SUCCESS", "NO_ANTIPODAL_GRASP", "GRIP_NOT_SEATED", "UNBOUNDED_GRIP",
                                       "SLIP_FELL", "CONTACT_INTERMITTENT", "DRIFT_ROTATE" };
    return (m >= 0 && m < FM_COUNT) ? n[m] : "?";
}

// `found` = the planner proposed a grasp for this attempt; r = the measured sim result. Returns a mode in
// [0, FM_COUNT) -- the taxonomy is exhaustive (every outcome maps to exactly one), so a separate "unclassified"
// bucket is never needed.
inline int classifyFailure(bool found, const GraspResult& r) {
    if (!found)                                                              return FM_NO_GRASP;
    if (graspSucceeded(r))                                                  return FM_SUCCESS;
    if (r.maxJawForceN > kLockedPhysics.maxGripForceFactor * kLockedPhysics.gripForceN) return FM_UNBOUNDED;
    if (!r.grippedAtLiftoff)                                                 return FM_NOT_SEATED;
    if (r.groundContactAfterLiftoff)                                         return FM_SLIP_FELL;
    if (r.contactFrac < kLockedPhysics.contactFrac)                         return FM_INTERMITTENT;
    return FM_DRIFT;                                                         // gripped, held, but missed the target pose
}

} // namespace krs::grasp
