// DriveByNameGate.cpp -- GATE DRIVE-BY-NAME (joint-primary addressing).
// A joint is addressable by its stable NAME and CAN nodeId via the ctx JointNameRegistry; resolution
// follows the NAME across edits (rename), not a positional index; and the ArticulationDriveNode drives
// the named DOF. This is the node-side of dissolving "drive DOF index N" into "drive joint <name>".
#include "NodeEditorGate.hpp"
#include "BridgeNodes.hpp"
#include "Scene.hpp"
#include "components.hpp"
#include "RobotModel.hpp"
#include "RobotBuilder.hpp"

#include <glm/glm.hpp>
#include <Eigen/Dense>
#include <cstdio>
#include <cmath>
#include <string>

namespace krs::nodes {

bool runDriveByNameGate()
{
    using std::printf;
    setvbuf(stdout, nullptr, _IONBF, 0);
    printf("[drivebyname] GATE DRIVE-BY-NAME -- joint addressable by name+nodeId; resolution follows the name across edits; node drives the named DOF\n");

    // A 3-DOF serial graph; addJoint mints canonical names J0/J1/J2 + nodeIds 0/1/2 + stable ids.
    krs::rbuild::RobotGraph g; g.base = 0;
    for (int i = 0; i < 4; ++i) {
        krs::rbuild::RBBody b; b.name = "L" + std::to_string(i);
        b.placement = Eigen::Matrix4d::Identity(); b.placement(0, 3) = double(i);
        g.bodies.push_back(b);
    }
    for (int i = 0; i < 3; ++i) {
        krs::rbuild::RBJoint j; j.parent = i; j.child = i + 1; j.type = krs::rbuild::JType::Revolute;
        j.axisDir = glm::vec3(0, 0, 1); j.orthonormalizeFrame(); g.addJoint(j);
    }

    Scene scene; auto& reg = scene.getRegistry();
    krs::robot::RobotRegistry& rr = reg.ctx().emplace<krs::robot::RobotRegistry>();
    krs::robot::LiveRobot& lr = rr.create(0);
    lr.model = g.toRobot();     // carries the names/nodeIds/ids minted above
    lr.rebuild();               // builds the chain + memberJoint (DOF -> model.joint index)
    const bool built = (lr.ndof() == 3);

    krs::robot::rebuildJointNameRegistry(reg);
    const krs::robot::JointNameRegistry* nr = reg.ctx().find<krs::robot::JointNameRegistry>();

    const bool nameOk = nr && nr->findByName("J0") && nr->findByName("J0")->dof == 0
                           && nr->findByName("J2") && nr->findByName("J2")->dof == 2;
    const bool nodeOk = nr && nr->findByNodeId(0) && nr->findByNodeId(0)->dof == 0
                           && nr->findByNodeId(2) && nr->findByNodeId(2)->dof == 2;

    // Rename J1 -> "elbow" in the live model; the registry must re-resolve "elbow" to DOF 1 and drop "J1"
    // (addressing follows the NAME, not a cached index -- the persistent-selection invariant).
    bool renameOk = false;
    if (built && int(lr.memberJoint.size()) >= 2) {
        lr.model.joints[lr.memberJoint[1]].name = "elbow";
        krs::robot::rebuildJointNameRegistry(reg);
        const krs::robot::JointNameRegistry* nr2 = reg.ctx().find<krs::robot::JointNameRegistry>();
        renameOk = nr2 && nr2->findByName("elbow") && nr2->findByName("elbow")->dof == 1
                       && !nr2->findByName("J1");
    }

    // The node: "Joint Name"=J2 must command bus DOF 2 (and only 2) -- the real drive path.
    bool nodeDriveOk = false;
    {
        if (auto* c = reg.ctx().find<ArticulationCommandComponent>()) { c->target.clear(); c->driven.clear(); }
        NodeLibrary::ArticulationDriveNode dN;
        dN.setScene(&scene);
        dN.setPortLiteral<float>("Angle", 0.42f);
        dN.setPortLiteral<std::string>("Joint Name", std::string("J2"));
        dN.compute();
        const ArticulationCommandComponent* cmd = reg.ctx().find<ArticulationCommandComponent>();
        nodeDriveOk = cmd && int(cmd->driven.size()) > 2 && cmd->driven[2] == 1
                          && std::abs(cmd->target[2] - 0.42f) < 1e-6f
                          && (cmd->driven.empty() || cmd->driven[0] == 0);
    }

    const bool pass = built && nameOk && nodeOk && renameOk && nodeDriveOk;
    printf("[drivebyname]   name->dof J0->0 J2->2=%s ; nodeId->dof 0->0 2->2=%s  %s\n",
           nameOk ? "yes" : "NO", nodeOk ? "yes" : "NO", (nameOk && nodeOk) ? "PASS" : "FAIL");
    printf("[drivebyname]   rename J1->'elbow' re-resolves to dof 1, old name gone=%s  %s\n",
           renameOk ? "yes" : "NO", renameOk ? "PASS" : "FAIL");
    printf("[drivebyname]   node Joint Name=J2 commands bus[2]=0.42 only=%s  %s\n",
           nodeDriveOk ? "yes" : "NO", nodeDriveOk ? "PASS" : "FAIL");
    printf("[drivebyname] %s\n", pass ? "ALL PASS (joints addressable by name+nodeId; resolution follows the name; node drives the named DOF)"
                                      : "FAILURES PRESENT");
    std::fflush(stdout);
    return pass;
}

} // namespace krs::nodes
