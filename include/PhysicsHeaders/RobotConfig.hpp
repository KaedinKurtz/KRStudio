#pragma once
// ===========================================================================
// STAGED HOT-SWAPPABLE PROPERTY CONFIGURATION (krs::rcfg) -- the chain exists;
// now CONFIGURE its per-joint engineering properties. Staged by category (limits,
// efforts, speeds, interface type, naming) but HOT-SWAPPABLE: every property is
// live-editable (NOT a one-way wizard), and an edit updates what the planner reads
// LIVE -- toJointLimits() re-derives the krs::plan::JointLimits from the CURRENT
// joints every call, so there is no stale cache.
//
// PROVENANCE is honest: the GEOMETRY gives axes/origins (frameProv=GeometryDerived,
// from the parse); LIMITS/EFFORTS/SPEEDS are the USER's (engProv=UserSupplied),
// NEVER fabricated. A limit marked GeometryDerived is a fabricated value (geometry
// produces no limits) and is flagged.
// ===========================================================================
#include <Eigen/Dense>
#include <string>
#include <vector>
#include "RobotModel.hpp"     // krs::robot::Robot / Joint / Provenance
#include "PlanningWorld.hpp"  // krs::plan::JointLimits

namespace krs::rcfg {

enum class InterfaceType { Position = 0, Velocity = 1, Effort = 2 };

inline const char* interfaceName(InterfaceType t) {
    switch (t) { case InterfaceType::Position: return "position";
                 case InterfaceType::Velocity: return "velocity";
                 case InterfaceType::Effort:   return "effort"; }
    return "position";
}

// A robot + per-joint display names. The engineering fields live on robot.joints
// (qLower/qUpper/vMax/effortMax/controlMode/engProv) -- the single source of truth
// the planner reads through toJointLimits().
struct RobotConfig {
    krs::robot::Robot robot;
    std::vector<std::string> jointNames;     // parallel to robot.joints (display only)

    void ensureNames() { jointNames.resize(robot.joints.size()); }

    // --- HOT-SWAPPABLE per-joint property edits (user-supplied; provenance stays
    //     UserSupplied -- these are the user's values, not geometry-derived) -------
    bool setPositionLimit(int j, double lo, double hi) {
        if (j < 0 || j >= int(robot.joints.size())) return false;
        robot.joints[j].qLower = lo; robot.joints[j].qUpper = hi;
        robot.joints[j].engProv = krs::robot::Provenance::UserSupplied;
        return true;
    }
    bool setVMax(int j, double v) {
        if (j < 0 || j >= int(robot.joints.size())) return false;
        robot.joints[j].vMax = v; robot.joints[j].engProv = krs::robot::Provenance::UserSupplied; return true;
    }
    bool setEffortMax(int j, double e) {
        if (j < 0 || j >= int(robot.joints.size())) return false;
        robot.joints[j].effortMax = e; robot.joints[j].engProv = krs::robot::Provenance::UserSupplied; return true;
    }
    bool setInterface(int j, InterfaceType t) {
        if (j < 0 || j >= int(robot.joints.size())) return false;
        robot.joints[j].controlMode = interfaceName(t); return true;
    }
    bool setName(int j, const std::string& name) {
        if (j < 0 || j >= int(robot.joints.size())) return false;
        ensureNames(); jointNames[j] = name; return true;
    }

    // LIVE derivation of the planner's limits, over MEMBER non-fixed joints in chain
    // order (matching robot.toChain()'s dof ordering). Re-reads the CURRENT joints --
    // an edit is reflected immediately (hot-swap, no cache).
    krs::plan::JointLimits toJointLimits() const {
        std::vector<double> lo, hi, vm;
        for (const auto& j : robot.joints) {
            if (!j.member || j.type == krs::dyn::JType::Fixed) continue;
            lo.push_back(j.qLower); hi.push_back(j.qUpper); vm.push_back(j.vMax);
        }
        krs::plan::JointLimits L;
        L.qLower.resize(int(lo.size())); L.qUpper.resize(int(hi.size())); L.vMax.resize(int(vm.size()));
        for (int i = 0; i < int(lo.size()); ++i) { L.qLower[i] = lo[i]; L.qUpper[i] = hi[i]; L.vMax[i] = vm[i]; }
        return L;
    }
};

// PROVENANCE honesty: the frame (axis/Rtree/ptree) must be GeometryDerived and the
// engineering fields UserSupplied. A joint whose engineering provenance claims
// GeometryDerived is a FABRICATED limit (geometry never produces limits) -> flagged.
inline bool isFabricatedLimit(const krs::robot::Joint& j) {
    return j.engProv == krs::robot::Provenance::GeometryDerived;
}
inline bool propertiesHonest(const krs::robot::Robot& r) {
    for (const auto& j : r.joints) {
        if (j.frameProv != krs::robot::Provenance::GeometryDerived) return false;  // axes must be geometry
        if (isFabricatedLimit(j)) return false;                                    // limits must be user-supplied
    }
    return true;
}

// PROPERTY gates (env KRS_PROPCFG_SELFTEST; folded into KRS_OVERNIGHT_BENCH).
bool runPropertyHotswapGate();     // an edit propagates LIVE to the planner's limits; stale-cache neg-ctrl
bool runPropertyProvenanceGate();  // axes geometry-derived, limits user-supplied; fabricated-value flagged

} // namespace krs::rcfg
