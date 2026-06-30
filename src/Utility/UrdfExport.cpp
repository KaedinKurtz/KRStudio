// UrdfExport.cpp -- export a joint-graph COMPONENT to URDF by the joint-primary rule the user described:
// "pick a base link, derive the kinematic chain from the tree-search of joints." There is no "robot"
// object -- we spanning-tree the joint graph from the chosen base and emit one <link> per body and one
// <joint> per tree edge (name/type/origin/axis/limits straight from the RBJoint). A free-floating
// component (no world anchor) is exported rooted at its chosen base, which is a valid URDF root.
#include "RobotBuilder.hpp"
#include <sstream>
#include <iomanip>
#include <set>

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
        // URDF joint origin = the parent->child rest transform (parent^-1 * child placement).
        const Eigen::Matrix4d rel = g.bodies[pb].placement.inverse() * g.bodies[body].placement;
        const Eigen::Vector3d t = rel.block<3, 1>(0, 3);
        const Eigen::Matrix3d R = rel.block<3, 3>(0, 0);
        const double roll  = std::atan2(R(2, 1), R(2, 2));
        const double pitch = std::atan2(-R(2, 0), std::sqrt(R(2, 1) * R(2, 1) + R(2, 2) * R(2, 2)));
        const double yaw   = std::atan2(R(1, 0), R(0, 0));
        // axis expressed in the parent (joint) frame.
        const Eigen::Vector3d ax = (g.bodies[pb].placement.block<3, 3>(0, 0).transpose()
                                    * Eigen::Vector3d(j.axisDir.x, j.axisDir.y, j.axisDir.z)).normalized();
        o << "  <joint name=\"" << jn << "\" type=\"" << ty << "\">\n";
        o << "    <parent link=\"" << linkName(g, pb) << "\"/>\n";
        o << "    <child link=\"" << linkName(g, body) << "\"/>\n";
        o << "    <origin xyz=\"" << vec3(t.x(), t.y(), t.z()) << "\" rpy=\"" << vec3(roll, pitch, yaw) << "\"/>\n";
        if (ty != "fixed") o << "    <axis xyz=\"" << vec3(ax.x(), ax.y(), ax.z()) << "\"/>\n";
        if (ty == "revolute" || ty == "prismatic")
            o << "    <limit lower=\"" << j.limits.lower << "\" upper=\"" << j.limits.upper
              << "\" effort=\"" << j.limits.effort << "\" velocity=\"" << j.limits.velocity << "\"/>\n";
        o << "  </joint>\n";
    }
    o << "</robot>\n";
    return o.str();
}

} // namespace krs::rbuild
