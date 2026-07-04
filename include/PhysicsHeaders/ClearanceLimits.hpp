#pragma once
// ===========================================================================
// CLEARANCE-BASED JOINT LIMIT DISCOVERY -- turn self-intersection analysis into MECHANICAL joint
// limits for a requested SURFACE CLEARANCE. Given the robot's capsule model + a clearance target
// (e.g. 1 cm), find, per DOF, the range of motion over which the robot never comes within that
// distance of hitting itself.
//
// Two modes, one algorithm (a 1-D clearance scan per DOF, others held at a SEED config):
//   STATIC  (seed = home) -> a conservative box written back as qLower/qUpper: the safe single-joint
//            range from home. "Generate joint limits" in the panel.
//   DYNAMIC (seed = q_current) -> the safe interval AROUND the current pose given where the rest of
//            the arm is right now. State-aware, widens/narrows as the arm moves -> the "live joint
//            limits as writable parameters" a node feeds, so you get the most usable workspace.
//
// The per-DOF-hold-others scan is exact for single-joint motion from the seed; it does not chase the
// full high-dimensional C-space free manifold (that can't be a box), which is exactly why the
// DYNAMIC mode exists -- re-run it as the configuration changes and each joint's live safe range
// tracks the real mechanical clearance envelope.
// ===========================================================================
#include <Eigen/Dense>
#include <vector>
#include "PlanningWorld.hpp"       // krs::plan::CollisionWorld / JointLimits, krs::dyn::SerialChain

namespace krs::climits {

struct ClearanceLimits {
    Eigen::VectorXd qLower, qUpper;    // discovered safe interval per DOF (radians / metres)
    Eigen::VectorXd seed;              // the config the scan was seeded from
    double clearance = 0.0;            // the surface-clearance target used
    bool ok = false;
    int dof = 0;
    bool seedSafe = true;              // false => the seed itself violates clearance (limits collapse to seed)
};

// Core scan: for each DOF, walk outward from seed[d] (others held at seed) until the surface
// clearance drops below `clearance` or the mechanical range end, refining the boundary by
// bisection. `mech` bounds the search to the joint's real mechanical travel.
ClearanceLimits computeClearanceInterval(const krs::dyn::SerialChain& chain,
                                         const krs::plan::CollisionWorld& world,
                                         double clearance,
                                         const Eigen::VectorXd& seed,
                                         const krs::plan::JointLimits& mech,
                                         int samplesPerDof = 240);

// STATIC box from home (seed = 0).
ClearanceLimits computeStaticLimits(const krs::dyn::SerialChain& chain,
                                    const krs::plan::CollisionWorld& world,
                                    double clearance, const krs::plan::JointLimits& mech,
                                    int samplesPerDof = 240);

// DYNAMIC interval around the current config (seed = qCurrent).
ClearanceLimits computeDynamicLimits(const krs::dyn::SerialChain& chain,
                                     const krs::plan::CollisionWorld& world,
                                     double clearance, const Eigen::VectorXd& qCurrent,
                                     const krs::plan::JointLimits& mech,
                                     int samplesPerDof = 240);

// Headless gate (KRS_CLEARANCE_SELFTEST): on an arm that self-collides in part of its range, the
// discovered box is collision-free at the clearance target and strictly inside the mechanical
// range where it must be; a larger clearance shrinks the box (monotone); dynamic limits around a
// deep-inside pose are wider than from a near-collision pose; NEG-CTRL: a seed already in collision
// collapses. Returns true iff all pass.
bool runClearanceGate();

} // namespace krs::climits
