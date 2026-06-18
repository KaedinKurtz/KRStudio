#pragma once
// Shared DEFAULT robot for the IK + OMPL planner nodes (and their gates): the OMPL sprint's 3-revolute arm
// (base post + upper arm + fore-arm), its link capsules, joint limits, and a collision world (floor + one box
// obstacle in the workspace). Pure Eigen / no scene dependency -- it makes the nodes fully functional and
// gateable now; wiring the live FANUC chain into these nodes is a later integration step.
#include "RobotDynamics.hpp"
#include "PlanningWorld.hpp"

namespace krs::nodes {

// Build the arm into `chain`, its limits into `lim`, and the capsules+obstacles into `world`.
// Returns the end-effector body index. ee TIP is at eeTipLocal() in that body's frame.
inline int buildDefaultArm(krs::dyn::SerialChain& chain, krs::plan::JointLimits& lim,
                           krs::plan::CollisionWorld& world) {
    using namespace krs::dyn;
    const Eigen::Matrix3d eye = Eigen::Matrix3d::Identity();
    DynBody body; body.mass = 1.0; body.com.setZero(); body.inertiaCom = eye;

    DynJoint j1; j1.type = JType::Revolute; j1.parent = -1; j1.Rtree = eye;
    j1.ptree = Eigen::Vector3d(0, 0, 0);   j1.axis = Eigen::Vector3d(0, 0, 1); j1.qLower = -3.14159; j1.qUpper = 3.14159;
    const int b0 = chain.addBody(j1, body);
    DynJoint j2; j2.type = JType::Revolute; j2.parent = b0; j2.Rtree = eye;
    j2.ptree = Eigen::Vector3d(0, 0, 0.3); j2.axis = Eigen::Vector3d(0, 1, 0); j2.qLower = -1.5; j2.qUpper = 1.5;
    const int b1 = chain.addBody(j2, body);
    DynJoint j3; j3.type = JType::Revolute; j3.parent = b1; j3.Rtree = eye;
    j3.ptree = Eigen::Vector3d(0.5, 0, 0); j3.axis = Eigen::Vector3d(0, 1, 0); j3.qLower = -2.5; j3.qUpper = 2.5;
    const int b2 = chain.addBody(j3, body);

    world.capsules.clear();
    world.capsules.push_back({ b0, Eigen::Vector3d(0, 0, 0), Eigen::Vector3d(0, 0, 0.3), 0.06 });
    world.capsules.push_back({ b1, Eigen::Vector3d(0, 0, 0), Eigen::Vector3d(0.5, 0, 0), 0.05 });
    world.capsules.push_back({ b2, Eigen::Vector3d(0, 0, 0), Eigen::Vector3d(0.5, 0, 0), 0.05 });
    world.obstacles.clear();
    // floor BELOW the base-post foot: the post capsule (radius 0.06) bottoms at world z=0 -> its surface reaches
    // z=-0.06, so the plane must sit at z<=-0.06 or EVERY config penetrates it. -0.2 gives a clean 0.14 m margin.
    world.obstacles.push_back(krs::plan::Obstacle::halfSpace(Eigen::Vector3d(0, 0, 1), Eigen::Vector3d(0, 0, -0.2)));  // floor
    // a box obstacle in front of the arm at +Y so a direct joint-line through it collides (OMPL-PLAN-VALID).
    world.obstacles.push_back(krs::plan::Obstacle::box(Eigen::Vector3d(0.55, 0.0, 0.30),
                                                       Eigen::Vector3d(0.12, 0.18, 0.30)));
    world.selfCollision = true; world.tolerance = 1e-6;

    lim.qLower.resize(3); lim.qUpper.resize(3); lim.vMax.resize(3);
    lim.qLower << -3.14159, -1.5, -2.5;
    lim.qUpper <<  3.14159,  1.5,  2.5;
    lim.vMax   << 2.0, 2.0, 3.0;
    return b2;
}

inline Eigen::Vector3d eeTipLocal() { return Eigen::Vector3d(0.5, 0, 0); }

} // namespace krs::nodes
