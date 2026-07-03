// UrdfExport.cpp -- export a joint-graph COMPONENT to URDF by the joint-primary rule the user described:
// "pick a base link, derive the kinematic chain from the tree-search of joints." There is no "robot"
// object -- we spanning-tree the joint graph from the chosen base and emit one <link> per body and one
// <joint> per tree edge. A free-floating component (no world anchor) is exported rooted at its chosen
// base, which is a valid URDF root.
//
// FRAME CONVENTION (URDF spec): the <axis> is expressed in the JOINT frame, which equals the CHILD
// link frame at zero configuration -- NOT the parent frame. And rotation happens about the axis
// THROUGH the joint-frame origin, so the origin must sit ON the bore (RBJoint.axisPos), not on the
// child link's CAD origin (the off-pivot bug toRobot() fixes internally with hasAxisPoint). We
// therefore define each exported link frame as [R_child_placement, borePoint] and chain origins
// between those frames: FK at q=0 reproduces every joint's world frame exactly, and each revolute
// rotates about its physical bore -- matching the in-app kinematics.
#include "RobotBuilder.hpp"
#include <sstream>
#include <iomanip>
#include <set>
#include <vector>

namespace krs::rbuild {

namespace {
std::string vec3(double x, double y, double z) {
    std::ostringstream o; o << std::setprecision(9) << x << " " << y << " " << z; return o.str();
}
std::string linkName(const RobotGraph& g, int b) {
    return (b >= 0 && b < int(g.bodies.size()) && !g.bodies[b].name.empty())
               ? g.bodies[b].name : ("link" + std::to_string(b));
}
} // namespace

// Export the connected component containing `baseBody` (spanning tree rooted there) as a URDF string.
std::string exportGraphToUrdf(const RobotGraph& g, int baseBody, const std::string& robotName) {
    std::ostringstream o;
    o << "<?xml version=\"1.0\"?>\n<robot name=\"" << robotName << "\">\n";
    if (baseBody < 0 || baseBody >= int(g.bodies.size())) { o << "</robot>\n"; return o.str(); }

    // links: every body in the component (members reachable from the chosen base).
    const std::set<int> comp = g.membersFrom(baseBody);
    for (int b : comp) o << "  <link name=\"" << linkName(g, b) << "\"/>\n";
    // Honesty note: bodies connected only through AMBIGUOUS joints are not in the component and are
    // NOT exported -- say so instead of silently dropping them.
    if (int(comp.size()) < int(g.bodies.size()))
        o << "  <!-- " << (int(g.bodies.size()) - int(comp.size()))
          << " body(ies) outside this component (unjointed or ambiguous-only) were not exported -->\n";

    // Per-body EXPORT frame (world): base = its placement; a jointed body = its joint frame
    // [R_child, borePoint]. Chain origins between these frames.
    std::vector<Eigen::Matrix4d> exportFrame(g.bodies.size(), Eigen::Matrix4d::Identity());
    exportFrame[baseBody] = g.bodies[baseBody].placement;

    // joints: one per spanning-tree edge (each non-base body has exactly one parent joint).
    const RobotGraph::ChainOrder co = g.chainOrderFrom(baseBody);
    for (size_t k = 1; k < co.order.size(); ++k) {
        const int body = co.order[k];
        const int pj = co.parentJoint[body];
        const int pb = co.parentBody[body];
        if (pj < 0 || pb < 0 || pj >= int(g.joints.size())) continue;
        const RBJoint& j = g.joints[pj];
        const std::string jn = j.name.empty() ? ("joint" + std::to_string(pj)) : j.name;
        const std::string ty = (j.type == JType::Prismatic) ? "prismatic"
                             : (j.type == JType::Fixed)     ? "fixed"
                             : (j.limits.enabled            ? "revolute" : "continuous");

        // JOINT frame (world): child placement's ORIENTATION, origin ON the bore axis (axisPos) --
        // so the exported revolute rotates about the physical bore exactly like the live robot.
        // axisPos (0,0,0) is the "unset" sentinel (same rule as toRobot) -> child CAD origin.
        const Eigen::Matrix4d childPlace = g.bodies[body].placement;
        Eigen::Matrix4d jointW = childPlace;
        if (j.type != JType::Fixed && glm::length(j.axisPos) > 1e-9f)
            jointW.block<3, 1>(0, 3) = Eigen::Vector3d(j.axisPos.x, j.axisPos.y, j.axisPos.z);
        exportFrame[body] = jointW;

        // URDF joint origin = parent EXPORT frame -> joint frame.
        const Eigen::Matrix4d rel = exportFrame[pb].inverse() * jointW;
        const Eigen::Vector3d t = rel.block<3, 1>(0, 3);
        const Eigen::Matrix3d R = rel.block<3, 3>(0, 0);
        const double roll  = std::atan2(R(2, 1), R(2, 2));
        const double pitch = std::atan2(-R(2, 0), std::sqrt(R(2, 1) * R(2, 1) + R(2, 2) * R(2, 2)));
        const double yaw   = std::atan2(R(1, 0), R(0, 0));
        // <axis> is expressed in the JOINT (child) frame -- URDF spec. The old export used the
        // PARENT frame, which is wrong whenever consecutive links carry different CAD orientations.
        const Eigen::Vector3d ax = (jointW.block<3, 3>(0, 0).transpose()
                                    * Eigen::Vector3d(j.axisDir.x, j.axisDir.y, j.axisDir.z)).normalized();
        o << "  <joint name=\"" << jn << "\" type=\"" << ty << "\">\n";
        o << "    <parent link=\"" << linkName(g, pb) << "\"/>\n";
        o << "    <child link=\"" << linkName(g, body) << "\"/>\n";
        o << "    <origin xyz=\"" << vec3(t.x(), t.y(), t.z()) << "\" rpy=\"" << vec3(roll, pitch, yaw) << "\"/>\n";
        if (ty != "fixed") o << "    <axis xyz=\"" << vec3(ax.x(), ax.y(), ax.z()) << "\"/>\n";
        if (ty == "revolute" || ty == "prismatic") {
            // URDF REQUIRES effort/velocity on limited joints, and 0 does not mean "unlimited" --
            // ros2_control treats effort=0 as an unactuatable joint. Our 0 sentinel means
            // "unspecified": emit the SAME defaults the live model enforces (Joint effortMax=100,
            // vMax=2), so exported behavior matches in-app behavior.
            const double lo = (j.type == JType::Prismatic && !j.limits.enabled) ? -1e6 : j.limits.lower;
            const double hi = (j.type == JType::Prismatic && !j.limits.enabled) ?  1e6 : j.limits.upper;
            const double effort   = (j.limits.effort   > 0.0) ? j.limits.effort   : 100.0;
            const double velocity = (j.limits.velocity > 0.0) ? j.limits.velocity : 2.0;
            o << "    <limit lower=\"" << lo << "\" upper=\"" << hi
              << "\" effort=\"" << effort << "\" velocity=\"" << velocity << "\"/>\n";
        }
        o << "  </joint>\n";
    }
    o << "</robot>\n";
    return o.str();
}

} // namespace krs::rbuild
