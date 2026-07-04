#pragma once
// ===========================================================================
// KINEMATIC REDUNDANCY RESOLUTION -- task-priority null-space projection.
//
// A redundant arm (nq > task DoF, e.g. an 8-DoF arm for a 6-DoF end-effector pose) has a family of
// configurations reaching the SAME end-effector pose. This module moves through that family to
// satisfy a SECONDARY objective -- twist a link, place an elbow, avoid limits, stay near a posture
// -- WITHOUT disturbing the primary end-effector task:
//
//     dq = alpha * N(q) * g(q),   N = I - J+ J   (null-space projector of the EE Jacobian)
//
// N annihilates any EE motion to first order; a per-step DLS correction snaps out the small 2nd-order
// drift so the EE stays put over finite secondary motion. This is the "keep the end-effector still
// and twist the wrist" redundancy that traditional per-joint controls can't express directly.
// ===========================================================================
#include <Eigen/Dense>
#include <functional>
#include "RobotDynamics.hpp"

namespace krs::redun {

// A secondary objective = a joint-space gradient g(q) (nq-vector) the resolver follows in the null
// space of the EE task. Builders below produce common ones; sum them for combined objectives.
using Secondary = std::function<Eigen::VectorXd(const Eigen::VectorXd&)>;

// Null-space projector N = I - J+ J for the 6-DoF task Jacobian of `eeBody` at q (damped pseudo-
// inverse, so N is well-conditioned near singularities). rank(N) == nq - (task rank).
Eigen::MatrixXd nullspaceProjector(const krs::dyn::SerialChain& chain, const Eigen::VectorXd& q,
                                   int eeBody, double lambda = 1e-3);

struct RedundancyResult {
    Eigen::VectorXd q;           // resolved configuration
    double eeDrift = 0.0;        // max EE pose error vs the held target (position + small-angle)
    double secondaryProgress = 0.0;  // change in the secondary metric (objective-defined)
    bool ok = false;
};

// Follow `secondary` in the null space of eeBody's pose task, holding the EE at its INITIAL pose
// (bodyPose(q0, eeBody)), over `steps` increments; each step projects the gradient through N and
// then runs a short DLS correction back onto the held EE pose. Joint limits (DynJoint qLower/qUpper)
// are respected. Returns the resolved q with the EE held to eeTol.
RedundancyResult resolveInNullspace(const krs::dyn::SerialChain& chain, const Eigen::VectorXd& q0,
                                    int eeBody, const Secondary& secondary,
                                    int steps = 60, double stepScale = 0.05,
                                    double eeLambda = 0.02, double eeTol = 1e-4);

// ---- secondary-objective builders ------------------------------------------------------------
// Push joint `j` toward `target` (the "twist this link" objective).
Secondary jointBias(int j, double target, double weight = 1.0);
// Pull the whole configuration toward a reference posture (elbow-out preference, etc.).
Secondary posture(const Eigen::VectorXd& qref, double weight = 1.0);
// Steer away from joint limits toward mid-range (uses each DynJoint's qLower/qUpper).
Secondary limitAvoidance(const krs::dyn::SerialChain& chain, double weight = 1.0);
// Move an intermediate `linkBody`'s origin toward a world point (place the elbow).
Secondary linkToPoint(const krs::dyn::SerialChain& chain, int linkBody,
                      const Eigen::Vector3d& worldTarget, double weight = 1.0);

// Headless gate (KRS_REDUNDANCY_SELFTEST): on a 7-DoF redundant arm, a joint-twist secondary moves
// the redundant DoF significantly while the EE pose stays within eeTol; the null space has the right
// dimension (nq - 6); NEG-CTRL: a 6-DoF arm has ~no null space (a twist request barely moves).
bool runRedundancyGate();

} // namespace krs::redun
