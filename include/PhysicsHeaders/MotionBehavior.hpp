#pragma once
// ===========================================================================
// MOTION BEHAVIOR -- bind a timed motion into a behavior-tree Action, so a POLICY executes MOTIONS.
//
// This is the bridge that turns the policy substrate (krs::policy behavior tree) into robot control:
// a "move" is a BT Action leaf that plays a krs::traj::TimedTrajectory over control ticks -- Running
// until the motion completes, Success at the end -- so "move to A, wait for the sensor, if force>X
// retract" is a real, resumable tree of motions. The Action integrates dt, samples the trajectory at
// the current time, and invokes a caller-bound `apply(q)` (e.g. LiveRobot::setCommandedQ), publishing
// progress to the blackboard so downstream nodes can react.
// ===========================================================================
#include <Eigen/Dense>
#include <functional>
#include <string>
#include "BehaviorTree.hpp"
#include "TrajectoryTiming.hpp"

namespace krs::motion {

// Build a behavior-tree Action functor that plays `traj`: each tick advances an internal clock by
// dt, samples q(t) (linear interp between trajectory samples), calls apply(q), writes `progressKey`
// (0..1) to the blackboard, and returns Running until t >= duration then Success. `apply` may be
// empty (progress-only). The returned Fn owns its own clock (shared state), so the SAME functor can
// be wrapped in a krs::policy::Action and re-entered across ticks; a fresh Action (rebuild) restarts.
krs::policy::Action::Fn playTrajectory(const krs::traj::TimedTrajectory& traj,
                                       std::function<void(const Eigen::VectorXd&)> apply,
                                       const std::string& progressKey = "motion.progress");

// Headless gate (KRS_MOTIONBEHAVIOR_SELFTEST): a Sequence[ play(trajA), Wait, play(trajB) ] driven by
// dt ticks executes both motions IN ORDER (trajA fully applied before trajB starts), the tree
// completes (Success), the driven state ends at trajB's final config, and blackboard progress
// reaches 1.0; NEG-CTRL: a Condition-gated motion does not run while its condition is false.
bool runMotionBehaviorGate();

} // namespace krs::motion
