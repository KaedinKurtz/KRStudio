#pragma once
// ===========================================================================
// TRAJECTORY TIME-PARAMETERIZATION -- joint-space path -> executable trajectory.
//
// A geometric joint-space path (a sequence of waypoint configs, no timing) is
// not directly executable: a controller needs to know WHEN to be at each config
// and how fast. This module assigns times so that no joint ever exceeds its
// velocity or acceleration limit, then samples the result on a fixed clock (dt)
// into the position/velocity/acceleration triple a servo loop consumes.
//
// Method (start simple + correct, not full TOPP): per SEGMENT (waypoint pair) we
// run ONE trapezoidal velocity profile along the segment's straight-line chord.
// The segment's scalar path parameter s in [0,1] follows a trapezoid (accelerate
// to a cruise speed, coast, decelerate), and each joint moves proportionally:
// q(s) = q0 + s*(q1-q0). The segment's velocity/accel caps (sMax_v, sMax_a) are
// the TIGHTEST per-joint bound -- for joint i with delta d_i, s must respect
// |d_i|*s_dot <= vMax_i and |d_i|*s_ddot <= aMax_i, so the fastest-limited joint
// sets the whole segment's pace and every joint stays inside its box. Segments
// are chained through a full stop at each waypoint (bounded-accel ramp down to 0
// and back up), which keeps accel bounded across the junction without a spline
// solve. This is exactly the trapezoidal scaling MoveIt's IPTP uses as its
// default; it is conservative (stops at every waypoint) but provably feasible.
//
// FUTURE UPGRADE: TOPP-RA (time-optimal path parameterization by reachability
// analysis) removes the per-waypoint stop and finds the true time-optimal profile
// under the same v/a box, typically 20-40% faster. It is the intended successor;
// this module's TimedTrajectory output type is the contract TOPP-RA would keep.
//
// LIMIT SOURCE: the per-dof vMax / aMax are the .kactuator-derived joint limits
// (see docs/ACTUATOR_MODEL.md) -- each actuator's rated velocity and its
// effort/inertia-implied acceleration ceiling become the timing constraints here.
// This module treats them as given numbers; it does NOT re-derive them from the
// dynamics, so the same trajectory retimes trivially if the actuator model changes.
//
// JOINT-SPACE ONLY: input is a list of configs (Eigen::VectorXd). A Cartesian
// path is retimed by first converting it to joint configs (IK, done elsewhere)
// and handing the configs here -- so this module has NO dependency on any
// CartesianPath type (deliberate: no cross-module coupling in this wave).
//
// Headless, pure CPU/Eigen. No Qt/GL/entt/PhysX.
// ===========================================================================
#include <Eigen/Dense>
#include <vector>

namespace krs::traj {

// Per-dof kinematic limits for timing. vMax/aMax are strictly-positive nq-vectors
// (a zero/negative entry means "that joint cannot move" and is rejected by the
// planner rather than producing a divide-by-zero). Mirrors the shape of
// krs::plan::JointLimits (which carries only vMax) plus the acceleration ceiling.
struct TimingLimits {
    Eigen::VectorXd vMax;   // rad/s or m/s per dof   (> 0)
    Eigen::VectorXd aMax;   // rad/s^2 or m/s^2 per dof (> 0)
};

// A fully time-stamped trajectory: at t[k] the robot should be at q[k] moving at
// qd[k] with acceleration qdd[k]. Sizes of t,q,qd,qdd are all equal; samples are
// spaced dt apart (the last sample lands exactly on `duration`, so its interval
// may be shorter than dt). q.back() == the final input waypoint (bit-exact).
struct TimedTrajectory {
    std::vector<double>          t;
    std::vector<Eigen::VectorXd> q;
    std::vector<Eigen::VectorXd> qd;
    std::vector<Eigen::VectorXd> qdd;
    double duration = 0.0;
    bool   ok = false;          // false => degenerate input (empty / bad limits); trajectory is empty
};

// Time-parameterize `waypoints` under per-joint velocity+acceleration limits and
// sample at `dt`. Consecutive duplicate waypoints (zero motion) contribute zero
// time and are skipped. A single waypoint yields a 1-sample stationary trajectory.
//
// Robustness: any vMax_i<=0 or aMax_i<=0 (for a dof that actually moves) makes the
// problem infeasible -> returns ok=false with an empty trajectory (NEVER divides by
// zero). dt<=0 is clamped up to a small positive value.
TimedTrajectory timeParameterize(const std::vector<Eigen::VectorXd>& waypoints,
                                 const TimingLimits& limits,
                                 double dt = 0.01);

// Convenience: retime a joint path that was DERIVED from a Cartesian path (the
// caller already ran IK on the Cartesian samples). Identical to timeParameterize;
// exists only to document the intended call-site and to keep the Cartesian retime
// entry-point in THIS (joint-space) module without importing any CartesianPath type.
inline TimedTrajectory retimeCartesianDerivedPath(const std::vector<Eigen::VectorXd>& jointPath,
                                                  const TimingLimits& limits,
                                                  double dt = 0.01) {
    return timeParameterize(jointPath, limits, dt);
}

// Headless gate (KRS_TRAJECTORY_SELFTEST): the sampled trajectory respects vMax
// and aMax on every joint (finite-differenced), reaches the final waypoint, has a
// duration that DECREASES monotonically as the limits are scaled up, and a lone
// 2-waypoint move shows a trapezoidal (rise-cruise-fall / triangular) speed
// profile. NEG-CTRL: zero vMax is rejected (ok=false) with no divide-by-zero.
bool runTrajectoryTimingGate();

} // namespace krs::traj
