#pragma once
// RobotRef -- the shared "Robot reference" handle passed between the Robot node and its IK/OMPL consumers.
//
// It bundles everything IK + the planner need from a robot: the kinematic SerialChain (built from the OWNED
// member joints only -- the base placement is a rigid world transform, NOT a DOF), the joint limits, a collision
// world for planning, the end-effector body index, and the rigid base placement. It is held by std::shared_ptr
// so that one chain instance is shared by all consumers and stays ALIVE for the planner's const-ref lifetime
// (MotionPlanner binds chain/world/limits by const ref).
#include "RobotModel.hpp"     // krs::robot::Robot, toChain
#include "PlanningWorld.hpp"  // krs::plan::JointLimits / CollisionWorld / LinkCapsule / Obstacle
#include <Eigen/Dense>
#include <memory>

namespace krs {

// A reference to a named frame on the robot (which link/body is being positioned) -- the IK target frame.
struct FrameRef { int body = 0; std::string name = "ee"; };

struct RobotRef {
    std::shared_ptr<krs::dyn::SerialChain> chain;          // member-joint chain (the planner's config space)
    krs::plan::JointLimits   limits;                       // size == nq (member DOFs)
    krs::plan::CollisionWorld world;                       // capsules + obstacles for collision-checked planning
    int ee = 0;                                            // end-effector body index in `chain`
    Eigen::Matrix4d basePlacement = Eigen::Matrix4d::Identity();  // rigid floor placement (NOT a DOF)
    int dof = 0;                                           // == chain->nq() == member-joint count
};

// The canonical default robot (mirrors the OMPL-sprint ROBOT-CHAIN gate's makeRobot): 3 member joints
// (yaw-Z, pitch-Y at z=0.3, pitch-Y at x=0.5) + ONE non-member joint + a non-identity base + a flange mount.
inline krs::robot::Robot buildDefaultRobot() {
    using krs::robot::Joint; using krs::robot::Robot;
    auto v3 = [](double a, double b, double c) { return Eigen::Vector3d(a, b, c); };
    Robot r; r.name = "default_arm"; r.nLinks = 4;
    Joint j1; j1.member = true; j1.axis = v3(0, 0, 1); j1.ptree = v3(0, 0, 0);   j1.qLower = -3.14159; j1.qUpper = 3.14159; j1.nodeId = 11;
    Joint j2; j2.member = true; j2.axis = v3(0, 1, 0); j2.ptree = v3(0, 0, 0.3); j2.qLower = -1.5; j2.qUpper = 1.5; j2.nodeId = 12;
    Joint j3; j3.member = true; j3.axis = v3(0, 1, 0); j3.ptree = v3(0.5, 0, 0); j3.qLower = -2.5; j3.qUpper = 2.5; j3.nodeId = 13;
    Joint jX; jX.member = false; jX.axis = v3(1, 0, 0); jX.ptree = v3(0.5, 0, 0);   // EXCLUDED from the chain
    r.joints = { j1, j2, j3, jX };
    Eigen::Matrix4d B = Eigen::Matrix4d::Identity();
    const double a = 0.5; Eigen::Matrix3d Rz; Rz << std::cos(a), -std::sin(a), 0, std::sin(a), std::cos(a), 0, 0, 0, 1;
    B.block<3, 3>(0, 0) = Rz; B.block<3, 1>(0, 3) = v3(1, 2, 3);
    r.basePlacement = B;
    r.mount.type = "flange-A"; r.mount.link = 2; r.mount.framePos = v3(0.5, 0, 0); r.mount.frameDir = v3(1, 0, 0);
    return r;
}

// Limits over the member joints (member order), matching chainLimits in the ROBOT-CHAIN gate.
inline krs::plan::JointLimits robotLimits(const krs::robot::Robot& r) {
    krs::plan::JointLimits lim; int n = 0; for (const auto& j : r.joints) if (j.member) ++n;
    lim.qLower.resize(n); lim.qUpper.resize(n); lim.vMax.resize(n);
    int i = 0; for (const auto& j : r.joints) if (j.member) { lim.qLower[i] = j.qLower; lim.qUpper[i] = j.qUpper; lim.vMax[i] = j.vMax; ++i; }
    return lim;
}

// Assemble a RobotRef from a Robot: the member-joint chain, limits, a collision world (link capsules + floor +
// a box obstacle in front of the arm -- same workspace the OMPL node uses), the ee body index, and the base.
inline RobotRef makeRobotRef(const krs::robot::Robot& r) {
    RobotRef ref;
    ref.chain = std::make_shared<krs::dyn::SerialChain>(r.toChain(false));
    ref.limits = robotLimits(r);
    ref.dof = ref.chain->nq();
    ref.ee = std::max(0, ref.chain->nbody() - 1);          // last member body = the mount link
    ref.basePlacement = r.basePlacement;
    auto v3 = [](double a, double b, double c) { return Eigen::Vector3d(a, b, c); };
    krs::plan::CollisionWorld& w = ref.world;
    w.capsules.clear();
    w.capsules.push_back({ 0, v3(0, 0, 0), v3(0, 0, 0.3), 0.06 });
    if (ref.chain->nbody() > 1) w.capsules.push_back({ 1, v3(0, 0, 0), v3(0.5, 0, 0), 0.05 });
    if (ref.chain->nbody() > 2) w.capsules.push_back({ 2, v3(0, 0, 0), v3(0.5, 0, 0), 0.05 });
    w.obstacles.clear();
    w.obstacles.push_back(krs::plan::Obstacle::halfSpace(v3(0, 0, 1), v3(0, 0, -0.2)));      // floor (clears the base post)
    w.obstacles.push_back(krs::plan::Obstacle::box(v3(0.55, 0.0, 0.30), v3(0.12, 0.18, 0.30)));
    w.selfCollision = true; w.tolerance = 1e-6;
    return ref;
}

} // namespace krs
