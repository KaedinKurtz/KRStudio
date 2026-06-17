// ===========================================================================
// OMPL sprint, Phase 1 — krs::plan::MotionPlanner implementation.
//
// Plans over a krs::dyn::SerialChain configuration space with OMPL. The state
// space is RealVectorStateSpace(nq) bounded by the per-dof joint position limits;
// the state-validity checker is FK + the analytic CollisionWorld query. Both the
// RNG seed AND a deterministic iteration-count termination condition are fixed,
// so a successful plan is bit-reproducible (PLAN-DETERMINISM) and an unreachable
// goal returns FAILURE after a fixed number of iterations (PLAN-CONNECTIVITY
// negative control) rather than running on a wall-clock timer.
// ===========================================================================
#include "MotionPlanner.hpp"

#include <chrono>

#include <ompl/base/SpaceInformation.h>
#include <ompl/base/StateSampler.h>
#include <ompl/base/spaces/RealVectorStateSpace.h>
#include <ompl/base/PlannerTerminationCondition.h>
#include <ompl/base/objectives/PathLengthOptimizationObjective.h>
#include <ompl/geometric/SimpleSetup.h>
#include <ompl/geometric/PathGeometric.h>
#include <ompl/geometric/planners/rrt/RRTConnect.h>
#include <ompl/geometric/planners/rrt/RRTstar.h>
#include <ompl/util/RandomNumbers.h>

namespace ob = ompl::base;
namespace og = ompl::geometric;

namespace krs::plan {

// A RealVectorStateSampler with an explicit LOCAL seed. OMPL's global
// RNG::setSeed only takes effect before the first RNG is created process-wide,
// so it cannot make two plans in the SAME process reproducible. Seeding the
// sampler's own RNG instead makes RRTConnect fully deterministic per seed
// (RRTConnect's only randomness source is uniform state sampling), independent
// of the global seed generator -> PLAN-DETERMINISM holds run-to-run in-process.
namespace {
class SeededRealVectorSampler : public ompl::base::RealVectorStateSampler {
public:
    SeededRealVectorSampler(const ompl::base::StateSpace* space, std::uint_fast32_t seed)
        : ompl::base::RealVectorStateSampler(space) { rng_.setLocalSeed(seed); }
};
} // namespace

// Map the movable dofs (1 per non-fixed joint) to their position/velocity limits,
// in dof-column order (chain.dofOf(body)). The RealVector bounds come from these.
static void gatherDofBounds(const krs::dyn::SerialChain& chain,
                            const JointLimits& lim, ob::RealVectorBounds& bounds) {
    const int nq = chain.nq();
    for (int b = 0; b < chain.nbody(); ++b) {
        const int d = chain.dofOf(b);
        if (d < 0) continue;
        bounds.setLow(d, lim.qLower[d]);
        bounds.setHigh(d, lim.qUpper[d]);
    }
    (void)nq;
}

PlanResult MotionPlanner::plan(const PlanRequest& req) const {
    PlanResult out;
    const int nq = chain_.nq();
    if (int(req.start.size()) != nq || int(req.goal.size()) != nq) return out;

    // Determinism: seed BEFORE the planner is constructed (planners build their
    // RNGs at construction time, drawing the global seed sequence).
    ompl::RNG::setSeed(req.seed);

    auto space = std::make_shared<ob::RealVectorStateSpace>(nq);
    ob::RealVectorBounds bounds(nq);
    gatherDofBounds(chain_, limits_, bounds);
    space->setBounds(bounds);

    // Deterministic, per-seed state sampling (see SeededRealVectorSampler above).
    const std::uint_fast32_t seed = req.seed;
    space->setStateSamplerAllocator([seed](const ob::StateSpace* s) -> ob::StateSamplerPtr {
        return std::make_shared<SeededRealVectorSampler>(s, seed);
    });

    og::SimpleSetup ss(space);

    // State validity = FK + analytic collision query < tolerance.
    const krs::dyn::SerialChain& chain = chain_;
    const CollisionWorld& world = world_;
    ss.setStateValidityChecker([&chain, &world, nq](const ob::State* s) -> bool {
        const auto* rv = s->as<ob::RealVectorStateSpace::StateType>();
        Eigen::VectorXd q(nq);
        for (int i = 0; i < nq; ++i) q[i] = rv->values[i];
        return world.valid(chain, q);
    });
    // Dense motion-segment checking (fraction of the space's maximum extent).
    ss.getSpaceInformation()->setStateValidityCheckingResolution(req.validityResolution);

    ob::ScopedState<ob::RealVectorStateSpace> start(space), goal(space);
    for (int i = 0; i < nq; ++i) { start[i] = req.start[i]; goal[i] = req.goal[i]; }
    ss.setStartAndGoalStates(start, goal, req.goalThreshold);

    if (req.kind == PlannerKind::RRTstar) {
        ss.setOptimizationObjective(
            std::make_shared<ob::PathLengthOptimizationObjective>(ss.getSpaceInformation()));
        ss.setPlanner(std::make_shared<og::RRTstar>(ss.getSpaceInformation()));
    } else {
        ss.setPlanner(std::make_shared<og::RRTConnect>(ss.getSpaceInformation()));
    }
    ss.setup();

    // Deterministic termination: a counter that returns true after a fixed number
    // of planner checks (single-arg PlannerTerminationCondition evaluates the fn
    // once per planner iteration, no wall-clock thread) -> reproducible AND
    // guaranteed to terminate. RRTConnect also ORs in exactSoln so it stops the
    // moment a solution is found (same iteration every run); an unreachable goal
    // exhausts the counter and returns FAILURE. RRT* runs the full iteration
    // budget to optimize, then returns its best.
    auto counter = std::make_shared<unsigned>(0u);
    const unsigned maxIters = req.maxIterations;
    ob::PlannerTerminationCondition iterPtc(
        [counter, maxIters]() { return ++(*counter) > maxIters; });

    const auto t0 = std::chrono::steady_clock::now();
    ob::PlannerStatus status =
        (req.kind == PlannerKind::RRTstar)
            ? ss.solve(iterPtc)
            : ss.solve(ob::plannerOrTerminationCondition(
                  ob::exactSolnPlannerTerminationCondition(ss.getProblemDefinition()), iterPtc));
    const auto t1 = std::chrono::steady_clock::now();
    out.planTimeSec = std::chrono::duration<double>(t1 - t0).count();
    out.iterations = *counter;

    if (status != ob::PlannerStatus::EXACT_SOLUTION || !ss.haveExactSolutionPath())
        return out;   // solved stays false -> FAILURE (not a fabricated path)

    og::PathGeometric path = ss.getSolutionPath();
    path.interpolate(req.denseWaypoints);
    const size_t n = path.getStateCount();
    out.waypoints.reserve(n);
    for (size_t k = 0; k < n; ++k) {
        const auto* rv = path.getState(k)->as<ob::RealVectorStateSpace::StateType>();
        Eigen::VectorXd q(nq);
        for (int i = 0; i < nq; ++i) q[i] = rv->values[i];
        out.waypoints.push_back(std::move(q));
    }
    out.solved = true;
    out.pathLength = pathLength(out.waypoints);
    return out;
}

} // namespace krs::plan
