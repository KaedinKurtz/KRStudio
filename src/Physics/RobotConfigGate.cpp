// RobotConfigGate.cpp -- STAGED HOT-SWAPPABLE PROPERTY CONFIG gates (Phase 2).
//   PROPERTY-HOTSWAP   : editing a joint's position limit updates what the planner
//                        reads LIVE (toJointLimits re-derives) -- a config beyond the
//                        OLD limit becomes valid after widening; a config within the
//                        old limit becomes INVALID after tightening. NEG-CTRL: a STALE
//                        cached limit set (captured before the edit) does NOT reflect
//                        the change -> it would mis-judge the config (hot-swap broken).
//   PROPERTY-PROVENANCE: axes/frames are geometry-derived, limits/efforts/speeds are
//                        user-supplied; a limit marked geometry-derived is a FABRICATED
//                        value (geometry produces no limits) and is flagged.

#include "RobotConfig.hpp"

#include <cstdio>
#include <vector>

namespace krs::rcfg {
namespace {

// a 2-DOF robot: axes geometry-derived (from a parse), limits user-supplied.
RobotConfig makeConfig() {
    RobotConfig cfg;
    cfg.robot.name = "cfg_robot"; cfg.robot.nLinks = 3;
    for (int i = 0; i < 2; ++i) {
        krs::robot::Joint j;
        j.type = krs::dyn::JType::Revolute; j.member = true;
        j.axis = Eigen::Vector3d::UnitZ();
        j.frameProv = krs::robot::Provenance::GeometryDerived;   // axis FROM the parse
        j.qLower = -1.0; j.qUpper = 1.0; j.vMax = 2.0; j.effortMax = 50.0;
        j.engProv = krs::robot::Provenance::UserSupplied;        // limits FROM the user
        j.controlMode = "position";
        cfg.robot.joints.push_back(j);
    }
    cfg.ensureNames();
    return cfg;
}

} // namespace

// ===========================================================================
bool runPropertyHotswapGate()
{
    using std::printf;
    setvbuf(stdout, nullptr, _IONBF, 0);
    printf("[propcfg] GATE PROPERTY-HOTSWAP -- a joint-limit edit updates the planner's limits LIVE (no stale cache)\n");

    RobotConfig cfg = makeConfig();

    // the limit set the planner reads BEFORE any edit (qUpper = 1.0 on joint 0).
    const krs::plan::JointLimits L0 = cfg.toJointLimits();
    Eigen::VectorXd qBeyond(2); qBeyond << 1.5, 0.0;     // beyond joint 0's [-1,1]
    const std::vector<Eigen::VectorXd> pathBeyond{ qBeyond };
    const double m0 = krs::plan::positionMargin(pathBeyond, L0);   // <0 -> invalid (beyond)

    // --- HOT-SWAP: WIDEN joint 0 to [-1, 2.0]; the planner's LIVE limits now accept qBeyond.
    cfg.setPositionLimit(0, -1.0, 2.0);
    const krs::plan::JointLimits L1 = cfg.toJointLimits();
    const double m1 = krs::plan::positionMargin(pathBeyond, L1);   // >=0 -> now valid (live edit applied)

    // NEG-CTRL: a STALE cached limit set (L0) does NOT reflect the edit -> still rejects.
    const double mStale = krs::plan::positionMargin(pathBeyond, L0);
    const bool hotswapWidens = (m0 < 0.0) && (m1 >= 0.0);
    const bool staleFails = (mStale < 0.0);                        // stale != live -> hot-swap is real

    // --- HOT-SWAP (safety direction): TIGHTEN joint 0 to [-1, 0.5]; a config within the
    //     OLD limit (q=0.8) becomes INVALID -> the planner respects the tighter limit live.
    cfg.setPositionLimit(0, -1.0, 0.5);
    const krs::plan::JointLimits L2 = cfg.toJointLimits();
    Eigen::VectorXd qWithinOld(2); qWithinOld << 0.8, 0.0;
    const double m2 = krs::plan::positionMargin({ qWithinOld }, L2);  // <0 -> now invalid
    const bool tightenPropagates = (m2 < 0.0);

    const bool pass = hotswapWidens && staleFails && tightenPropagates;

    printf("[propcfg]   widen [-1,1]->[-1,2]: q=1.5 margin %.3f(old,invalid) -> %.3f(live,valid)  %s\n",
           m0, m1, hotswapWidens ? "PASS" : "FAIL");
    printf("[propcfg]   NEG-CTRL stale cached limits still reject the now-valid config (margin=%.3f<0): %s  %s\n",
           mStale, staleFails ? "yes" : "no", staleFails ? "REJECTS(non-vacuous)" : "VACUOUS!");
    printf("[propcfg]   tighten [-1,2]->[-1,0.5]: q=0.8 now invalid (margin=%.3f<0): %s  %s\n",
           m2, tightenPropagates ? "yes" : "no", tightenPropagates ? "PASS" : "FAIL");
    printf("[propcfg] %s\n", pass ? "ALL PASS (limit edits propagate LIVE to the planner; stale cache is detectably wrong)"
                                   : "FAILURES PRESENT");
    std::fflush(stdout);
    return pass;
}

// ===========================================================================
bool runPropertyProvenanceGate()
{
    using std::printf;
    setvbuf(stdout, nullptr, _IONBF, 0);
    printf("[propcfg] GATE PROPERTY-PROVENANCE -- axes geometry-derived, limits user-supplied; fabricated values flagged\n");

    RobotConfig cfg = makeConfig();

    // honest: every axis geometry-derived, every limit user-supplied.
    const bool honest = propertiesHonest(cfg.robot);
    bool allAxesGeom = true, allLimitsUser = true;
    for (const auto& j : cfg.robot.joints) {
        if (j.frameProv != krs::robot::Provenance::GeometryDerived) allAxesGeom = false;
        if (j.engProv != krs::robot::Provenance::UserSupplied) allLimitsUser = false;
    }

    // NEG-CTRL A: a FABRICATED limit -- joint 1's engineering provenance claims
    // GeometryDerived (geometry produces no limits) -> flagged.
    RobotConfig fab = makeConfig();
    fab.robot.joints[1].qUpper = 2.5;
    fab.robot.joints[1].engProv = krs::robot::Provenance::GeometryDerived;   // the lie
    const bool fabFlagged = isFabricatedLimit(fab.robot.joints[1]) && !propertiesHonest(fab.robot);

    // NEG-CTRL B: an axis claimed USER-supplied (axes must come from geometry) -> flagged.
    RobotConfig badAxis = makeConfig();
    badAxis.robot.joints[0].frameProv = krs::robot::Provenance::UserSupplied;
    const bool badAxisFlagged = !propertiesHonest(badAxis.robot);

    const bool pass = honest && allAxesGeom && allLimitsUser && fabFlagged && badAxisFlagged;

    printf("[propcfg]   honest config: axes-geometry=%s limits-user=%s -> propertiesHonest=%s  %s\n",
           allAxesGeom ? "yes" : "no", allLimitsUser ? "yes" : "no", honest ? "yes" : "no",
           (honest && allAxesGeom && allLimitsUser) ? "PASS" : "FAIL");
    printf("[propcfg]   NEG-CTRL fabricated limit (engProv=GeometryDerived) flagged: %s  %s\n",
           fabFlagged ? "yes" : "no", fabFlagged ? "REJECTS(non-vacuous)" : "VACUOUS!");
    printf("[propcfg]   NEG-CTRL axis claimed user-supplied (must be geometry) flagged: %s  %s\n",
           badAxisFlagged ? "yes" : "no", badAxisFlagged ? "REJECTS(non-vacuous)" : "VACUOUS!");
    printf("[propcfg] %s\n", pass ? "ALL PASS (provenance honest: geometry gives axes, user gives limits; fabricated values flagged)"
                                   : "FAILURES PRESENT");
    std::fflush(stdout);
    return pass;
}

} // namespace krs::rcfg
