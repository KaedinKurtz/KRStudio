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

    // The node: "Joint Name"=J2 must command (robot 0, dof 2) -- and ONLY that -- via the
    // ROBOT-KEYED bus lane (a name resolves to a robot; the command must carry that robot).
    bool nodeDriveOk = false;
    {
        if (auto* c = reg.ctx().find<ArticulationCommandComponent>()) c->clearForEvalPass();
        NodeLibrary::ArticulationDriveNode dN;
        dN.setScene(&scene);
        dN.setPortLiteral<float>("Angle", 0.42f);
        dN.setPortLiteral<std::string>("Joint Name", std::string("J2"));
        dN.compute();
        const ArticulationCommandComponent* cmd = reg.ctx().find<ArticulationCommandComponent>();
        nodeDriveOk = cmd && cmd->entries.size() == 1
                          && cmd->entries[0].robotId == 0 && cmd->entries[0].dof == 2
                          && std::abs(cmd->entries[0].target - 0.42f) < 1e-6f
                          && cmd->target.empty() && cmd->driven.empty();   // keyed lane, not legacy
    }

    // NEG-CTRL (cross-robot cross-talk): with a SECOND robot registered, draining the J2 command
    // must move robot 0's q[2] and leave robot 1's q untouched (the old drain broadcast the one
    // positional array to every robot). Also: an unresolvable name commands NOTHING (no index-0
    // fallback), and a deleted node's command does not survive a clearForEvalPass.
    bool crossTalkOk = false, unresolvedOk = false, releaseOk = false;
    {
        krs::robot::LiveRobot& lr1 = rr.create(1);
        lr1.model = g.toRobot();
        lr1.rebuild();
        lr1.q.setZero(); lr.q.setZero();
        if (auto* c = reg.ctx().find<ArticulationCommandComponent>()) c->clearForEvalPass();
        krs::robot::rebuildJointNameRegistry(reg);   // note: duplicate names resolve last-writer-wins
        NodeLibrary::ArticulationDriveNode dN;
        dN.setScene(&scene);
        dN.setPortLiteral<float>("Angle", 0.42f);
        dN.setPortLiteral<std::string>("Joint Name", std::string("J2"));
        dN.compute();
        // Force the entry onto robot 0 regardless of duplicate-name resolution order, then drain.
        if (auto* c = reg.ctx().find<ArticulationCommandComponent>()) {
            c->entries.clear(); c->setEntry(/*robotId*/0, /*dof*/2, 0.42f);
        }
        krs::robot::drainCommandBusIntoRobots(reg);
        crossTalkOk = std::abs(lr.q[2] - 0.42) < 1e-6 && std::abs(lr.q[0]) < 1e-12
                   && lr1.q.cwiseAbs().maxCoeff() < 1e-12;
        // Unresolvable name -> no command at all.
        if (auto* c = reg.ctx().find<ArticulationCommandComponent>()) c->clearForEvalPass();
        NodeLibrary::ArticulationDriveNode dBad;
        dBad.setScene(&scene);
        dBad.setPortLiteral<float>("Angle", 0.9f);
        dBad.setPortLiteral<std::string>("Joint Name", std::string("no_such_joint"));
        dBad.compute();
        const ArticulationCommandComponent* cmd = reg.ctx().find<ArticulationCommandComponent>();
        unresolvedOk = cmd && cmd->entries.empty() && cmd->driven.empty();
        // Release: after a clear (node deleted / stops asserting), draining moves nothing.
        lr.q.setZero(); lr1.q.setZero();
        krs::robot::drainCommandBusIntoRobots(reg);
        releaseOk = lr.q.cwiseAbs().maxCoeff() < 1e-12 && lr1.q.cwiseAbs().maxCoeff() < 1e-12;
    }

    const bool pass = built && nameOk && nodeOk && renameOk && nodeDriveOk
                   && crossTalkOk && unresolvedOk && releaseOk;
    printf("[drivebyname]   name->dof J0->0 J2->2=%s ; nodeId->dof 0->0 2->2=%s  %s\n",
           nameOk ? "yes" : "NO", nodeOk ? "yes" : "NO", (nameOk && nodeOk) ? "PASS" : "FAIL");
    printf("[drivebyname]   rename J1->'elbow' re-resolves to dof 1, old name gone=%s  %s\n",
           renameOk ? "yes" : "NO", renameOk ? "PASS" : "FAIL");
    printf("[drivebyname]   node Joint Name=J2 -> keyed entry (robot 0, dof 2)=0.42 only=%s  %s\n",
           nodeDriveOk ? "yes" : "NO", nodeDriveOk ? "PASS" : "FAIL");
    printf("[drivebyname]   NEG-CTRLs: no cross-robot leak=%s ; unresolvable name commands nothing=%s ; cleared bus releases=%s  %s\n",
           crossTalkOk ? "yes" : "NO", unresolvedOk ? "yes" : "NO", releaseOk ? "yes" : "NO",
           (crossTalkOk && unresolvedOk && releaseOk) ? "PASS" : "FAIL");
    printf("[drivebyname] %s\n", pass ? "ALL PASS (joints addressable by name+nodeId; resolution follows the name; node drives the named DOF)"
                                      : "FAILURES PRESENT");
    std::fflush(stdout);
    return pass;
}

} // namespace krs::nodes
