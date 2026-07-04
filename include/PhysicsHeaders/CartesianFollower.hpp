#pragma once
// ===========================================================================
// REDUNDANCY-AWARE CARTESIAN FOLLOWER -- sweep a robot's end-effector along a CartesianPath while
// (optionally) resolving a SECONDARY objective (twist a link, hold a posture, avoid limits) in the
// null space. This is the join of the two new cores: krs::path (the geometry of the goal) + krs::redun
// (the redundancy math). Output is a joint-space path (one config per path sample) that
// krs::traj::timeParameterize turns into an executable, actuator-limited trajectory.
//
// Frames: a CartesianPath yields WORLD poses; the SerialChain IK works in the CHAIN base frame, so
// each world pose is pre-multiplied by basePlacement^-1 before solving. IK is warm-started from the
// previous sample's config (continuity), so the follower tracks a smooth path without config jumps.
// ===========================================================================
#include <Eigen/Dense>
#include <vector>
#include "RobotDynamics.hpp"
#include "CartesianPath.hpp"
#include "Redundancy.hpp"

namespace krs::follow {

struct FollowResult {
    std::vector<Eigen::VectorXd> jointPath;   // one config per sample (feed to krs::traj)
    std::vector<double>          eeErr;       // per-sample EE tracking error (pos+rot), metres+rad
    double maxEeErr = 0.0;
    bool   ok = false;
    bool   anyUnreachable = false;            // a sample was beyond reach (IK clamped to the boundary)
};

// Follow `path` with `eeBody`'s pose, sampling `samples` points over s in [0,1], warm-starting IK
// from q0. If `secondary` is non-null, after each pose solve the redundancy resolver applies it in
// the null space (so e.g. a wrist twist is held constant along the sweep). basePlacement maps the
// chain frame to world (LiveRobot.model.basePlacement).
FollowResult followPath(const krs::dyn::SerialChain& chain, const Eigen::Matrix4d& basePlacement,
                        int eeBody, const krs::path::CartesianPath& path, const Eigen::VectorXd& q0,
                        int samples = 64, const krs::redun::Secondary* secondary = nullptr);

// Headless gate (KRS_FOLLOWER_SELFTEST): a redundant arm sweeps a Cartesian arc -- every sample's EE
// tracks the path within tolerance; with a joint-twist secondary the twist joint moves along the
// sweep while EE tracking is preserved; NEG-CTRL: a path far outside reach flags anyUnreachable.
bool runFollowerGate();

} // namespace krs::follow
