#pragma once
// ===========================================================================
// MOTION RETARGETING -- fit a robot onto a reference skeleton clip (krs::retarget).
//
// A SkeletonClip (mocap / Blender animation) is a REFERENCE TRAJECTORY. Retargeting maps a skeleton
// joint's motion onto a robot link, per frame, via IK -- producing a joint-space trajectory the
// robot can (roughly) follow: the imitation SEED that RL then refines (DeepMimic/AMP; see
// docs/POLICY_AND_LEARNING.md). The DoF mismatch (a human arm vs an N-DoF robot) is absorbed by the
// robot's own IK + optional null-space secondaries (elbow correspondence, posture).
//
// Skeleton space is not robot space: `skelToBase` (a rigid+scale transform) maps the skeleton's
// world into the robot's BASE frame, and `scale` rescales lengths (mocap is often centimetres). IK
// is warm-started frame-to-frame for a continuous, jump-free trajectory.
// ===========================================================================
#include <Eigen/Dense>
#include <vector>
#include "RobotDynamics.hpp"
#include "SkeletonClip.hpp"
#include "Redundancy.hpp"

namespace krs::retarget {

struct RetargetResult {
    std::vector<Eigen::VectorXd> jointFrames;   // robot config per skeleton frame (the seed trajectory)
    std::vector<double>          posErr;        // per-frame EE position error to the (scaled) target
    double maxPosErr = 0.0;
    bool   ok = false;
    bool   anyUnreachable = false;
};

// Position retargeting: map skeleton joint `eeSkelJoint`'s world position onto robot `eeBody`'s
// origin every frame. `skelToBase` * (scale * skelWorldPos) is the target in the robot base frame;
// orientation is left free (position-dominant IK). An optional `secondary` (e.g. an elbow-to-point
// correspondence, or a posture) is resolved in the null space after each position solve.
RetargetResult retargetPositions(const krs::dyn::SerialChain& chain, const Eigen::Matrix4d& basePlacement,
                                 const krs::anim::SkeletonClip& clip, int eeSkelJoint, int eeBody,
                                 const Eigen::Matrix4d& skelToBase, double scale,
                                 const Eigen::VectorXd& q0,
                                 const krs::redun::Secondary* secondary = nullptr);

// Headless gate (KRS_RETARGET_SELFTEST): a hand-built skeleton whose end joint traces a reachable
// line is retargeted onto a robot EE; every frame's EE reaches the scaled target within tolerance
// and the trajectory is continuous (no frame-to-frame config jumps); NEG-CTRL: a skeleton scaled
// far out of reach flags anyUnreachable.
bool runRetargetGate();

} // namespace krs::retarget
