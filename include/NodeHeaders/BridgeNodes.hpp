#pragma once
// BridgeNodes.hpp -- Phase 5: the nodes that actually bridge the visual graph to the live backend.
// The existing Physics nodes already mutate the ECS (setLinearVelocity writes the component) but
// nothing FED them the live registry -- their "Registry" input was never sourced. SceneContextNode
// is that missing source: it emits the injected Scene's entt::registry* so downstream nodes operate
// on the real world. SetJointAngleNode writes a canonical JointComponent.currentPosition, i.e. a
// graph edit that drives the robot.

#include <QWidget>
#include "Node.hpp"
#include "NodeFactory.hpp"

namespace NodeLibrary {

// Source node: outputs the live ECS registry pointer from the injected Scene (Phase 5 bridge root).
class SceneContextNode : public Node {
public:
    SceneContextNode();
    void compute() override;
    bool needsExecutionControls() const override { return false; }
};

// Action node: sets a revolute joint's canonical angle (JointComponent.currentPosition) on the
// target entity, i.e. a node-graph command that moves the robot.
class SetJointAngleNode : public Node {
public:
    SetJointAngleNode();
    void compute() override;
};

// Kinematics node: the ENGINE consumer of JointComponent.currentPosition. Reads the joint angle from
// the Joint Entity and writes the moving Link Entity's TransformComponent.translation via the shared
// revolute FK -- so a graph that drives the joint produces a real link-transform change in the ECS.
class RevoluteLinkFkNode : public Node {
public:
    RevoluteLinkFkNode();
    void compute() override;
};

} // namespace NodeLibrary

namespace krs::nodes {
// Phase 5 GATE ND (gated by KRS_NODE_SELFTEST): builds real node graphs, injects a Scene, evaluates
// compute(), and asserts the live ECS / canonical robot changed -- with disconnected-node and
// wrong-type negative controls. Returns true on PASS. Requires no GL context.
bool runNodeGraphGateND();
}
