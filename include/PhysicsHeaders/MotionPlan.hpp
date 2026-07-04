#pragma once
// ===========================================================================
// MOTION PLAN -- the cinematics pipeline capstone: CartesianPath -> redundancy-aware follower ->
// trajectory timing = an executable, actuator-limited motion. This is where the three new modules
// compose into the thing a user actually runs: "sweep the EE along this path (holding this
// secondary) as fast as the actuators allow, and give me the timed joint trajectory to execute."
//
// A LiveRobot adapter is one line: pass lr.chain, lr.model.basePlacement, lr.chain.nbody()-1,
// TimingLimits from the model's per-joint vMax + an accel ceiling, and lr.q as the seed; step the
// result into the robot with setCommandedQ at each timestamp.
// ===========================================================================
#include <Eigen/Dense>
#include "RobotDynamics.hpp"
#include "CartesianPath.hpp"
#include "CartesianFollower.hpp"
#include "TrajectoryTiming.hpp"
#include "Redundancy.hpp"

namespace krs::motion {

struct MotionPlan {
    krs::follow::FollowResult follow;    // the geometric joint path + EE tracking report
    krs::traj::TimedTrajectory timed;    // the time-parameterized, limit-respecting trajectory
    bool ok = false;
};

// Plan a Cartesian motion: follow `path` with `eeBody` (optional null-space `secondary`), then time
// the resulting joint path under `limits`, sampled at `dt`. `samples` path points feed the follower.
MotionPlan planCartesianMotion(const krs::dyn::SerialChain& chain, const Eigen::Matrix4d& basePlacement,
                               int eeBody, const krs::path::CartesianPath& path,
                               const Eigen::VectorXd& q0, const krs::traj::TimingLimits& limits,
                               int samples = 48, double dt = 0.01,
                               const krs::redun::Secondary* secondary = nullptr);

// Headless gate (KRS_MOTION_SELFTEST): a robot sweeps a reachable arc -> the plan's follower tracks
// the EE, the timed trajectory respects the joint velocity/accel limits and reaches the follower's
// final config, and the timed path's final EE lands on the Cartesian path's end. NEG-CTRL: an
// out-of-reach path flags the follower unreachable.
bool runMotionPlanGate();

} // namespace krs::motion
