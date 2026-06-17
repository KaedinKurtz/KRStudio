#pragma once
// ===========================================================================
// OMPL sprint, Phase 1 — sampling-based motion planner over a SerialChain
// configuration space (krs::plan::MotionPlanner). OMPL is fully encapsulated in
// MotionPlanner.cpp; this header is OMPL-free (only Eigen + the collision world).
// ===========================================================================
#include <Eigen/Dense>
#include <vector>
#include "RobotDynamics.hpp"
#include "PlanningWorld.hpp"

namespace krs::plan {

enum class PlannerKind { RRTConnect, RRTstar };

struct PlanRequest {
    Eigen::VectorXd start;
    Eigen::VectorXd goal;
    PlannerKind kind = PlannerKind::RRTConnect;
    std::uint32_t seed = 1u;          // RNG seed (determinism)
    unsigned maxIterations = 30000;   // deterministic iteration cap (NOT wall-clock); reachable
                                      // plans terminate early on exact solution, so this only
                                      // bounds the unreachable/worst case (and the tree size)
    unsigned denseWaypoints = 256;    // path is interpolated to >= this many states
    double validityResolution = 0.01; // fraction of space extent for motion checking
    double goalThreshold = 1e-6;      // exact-goal tolerance
};

struct PlanResult {
    bool solved = false;              // exact solution found
    std::vector<Eigen::VectorXd> waypoints;   // dense, collision-checked path (start..goal)
    double planTimeSec = 0.0;
    double pathLength = 0.0;          // joint-space L2 length
    unsigned iterations = 0;          // planner iterations consumed
};

// Stateless wrapper: holds references to the config-space model, the collision
// world and the joint limits; builds a fresh OMPL setup per plan() call (so there
// is no hidden planner state and the header stays OMPL-free).
class MotionPlanner {
public:
    MotionPlanner(const krs::dyn::SerialChain& chain,
                  const CollisionWorld& world,
                  const JointLimits& limits)
        : chain_(chain), world_(world), limits_(limits) {}

    PlanResult plan(const PlanRequest& req) const;

private:
    const krs::dyn::SerialChain& chain_;
    const CollisionWorld& world_;
    const JointLimits& limits_;
};

// PLAN gates (env KRS_PLANNING_SELFTEST; folded into KRS_OVERNIGHT_BENCH):
// COLLISION-FREE / LIMITS / CONNECTIVITY / DETERMINISM, each with a non-vacuous
// negative control. Prints "[plan ...]" lines with measured numbers; returns
// true iff every sub-gate passes.
bool runPlanningGate();

// EXECUTE gates (env KRS_EXECUTE_SELFTEST; folded into KRS_OVERNIGHT_BENCH):
// execute a PLANNED path through the computed-torque controller (krs::ctrl) and
// verify EXECUTE-TRACK (achieved follows commanded under gravity; soft PD lags),
// EXECUTE-COLLISION-FREE (achieved trajectory collision-free; colliding ref
// collides) and EXECUTE-LIMITS. Returns true iff every sub-gate passes.
bool runExecuteGate();

// E2E gate (env KRS_E2E_SELFTEST; folded into KRS_OVERNIGHT_BENCH): a robot is
// DEFINED via the chain data model -> PLANNED -> EXECUTED. Every stage's number
// is asserted, and severing any stage (define/plan/execute) localizes the break
// to that stage (upstream stages stay healthy). Returns true iff E2E + all three
// sever-localizations behave as expected.
bool runE2EGate();

} // namespace krs::plan
