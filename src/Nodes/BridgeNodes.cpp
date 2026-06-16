#include "BridgeNodes.hpp"
#include "Scene.hpp"
#include "components.hpp"
#include "RevoluteFK.hpp"   // shared krs::kin::revoluteApply (one FK definition)
#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <memory>

namespace NodeLibrary {

// ---- SceneContextNode: emit the live registry pointer from the injected Scene ----
SceneContextNode::SceneContextNode() {
    m_id = "world_scene_context";
    m_ports.push_back({ "Registry", {"entt::registry*", "handle"}, Port::Direction::Output, this });
}

void SceneContextNode::compute() {
    if (m_scene) {
        setOutput<entt::registry*>("Registry", &m_scene->getRegistry());
    }
}

namespace {
    struct SceneContextRegistrar {
        SceneContextRegistrar() {
            NodeDescriptor desc = { "Scene Context", "World",
                "Source: the live ECS registry of the active scene (feeds Physics nodes)." };
            NodeFactory::instance().registerNodeType("world_scene_context", desc,
                []() { return std::make_unique<SceneContextNode>(); });
        }
    };
    static SceneContextRegistrar g_sceneContextRegistrar;
}

// ---- SetJointAngleNode: write the canonical joint angle (graph -> robot) ----
SetJointAngleNode::SetJointAngleNode() {
    m_id = "physics_set_joint_angle";
    m_ports.push_back({ "Registry", {"entt::registry*", "handle"}, Port::Direction::Input, this });
    m_ports.push_back({ "Entity",   {"entt::entity", "handle"},    Port::Direction::Input, this });
    m_ports.push_back({ "Angle",    {"float", "radians"},          Port::Direction::Input, this });
}

void SetJointAngleNode::compute() {
    auto registry = getInput<entt::registry*>("Registry");
    auto entity   = getInput<entt::entity>("Entity");
    auto angle    = getInput<float>("Angle");
    if (registry && entity && angle) {
        entt::registry& reg = **registry;
        if (reg.valid(*entity) && reg.all_of<JointComponent>(*entity)) {
            reg.get<JointComponent>(*entity).currentPosition = double(*angle);
        }
    }
}

namespace {
    struct SetJointAngleRegistrar {
        SetJointAngleRegistrar() {
            NodeDescriptor desc = { "Set Joint Angle", "Physics/Actions",
                "Sets a revolute joint's canonical angle (drives the robot)." };
            NodeFactory::instance().registerNodeType("physics_set_joint_angle", desc,
                []() { return std::make_unique<SetJointAngleNode>(); });
        }
    };
    static SetJointAngleRegistrar g_setJointAngleRegistrar;
}

// ---- RevoluteLinkFkNode: engine FK -- joint angle (ECS) -> link transform (ECS) ----
RevoluteLinkFkNode::RevoluteLinkFkNode() {
    m_id = "kinematics_revolute_link_fk";
    m_ports.push_back({ "Registry",     {"entt::registry*", "handle"}, Port::Direction::Input, this });
    m_ports.push_back({ "Joint Entity", {"entt::entity", "handle"},    Port::Direction::Input, this });
    m_ports.push_back({ "Link Entity",  {"entt::entity", "handle"},    Port::Direction::Input, this });
    m_ports.push_back({ "Origin",       {"glm::vec3", "m"},            Port::Direction::Input, this });
    m_ports.push_back({ "Axis",         {"glm::vec3", "unitless"},     Port::Direction::Input, this });
    m_ports.push_back({ "Rest Point",   {"glm::vec3", "m"},            Port::Direction::Input, this });
}

void RevoluteLinkFkNode::compute() {
    auto registry = getInput<entt::registry*>("Registry");
    auto jointE   = getInput<entt::entity>("Joint Entity");
    auto linkE    = getInput<entt::entity>("Link Entity");
    auto origin   = getInput<glm::vec3>("Origin");
    auto axis     = getInput<glm::vec3>("Axis");
    auto rest     = getInput<glm::vec3>("Rest Point");
    if (registry && jointE && linkE && origin && axis && rest) {
        entt::registry& reg = **registry;
        if (reg.valid(*jointE) && reg.all_of<JointComponent>(*jointE) &&
            reg.valid(*linkE)  && reg.all_of<TransformComponent>(*linkE)) {
            const float q = float(reg.get<JointComponent>(*jointE).currentPosition);
            reg.get<TransformComponent>(*linkE).translation = krs::kin::revoluteApply(*origin, *axis, *rest, q);
        }
    }
}

namespace {
    struct RevoluteLinkFkRegistrar {
        RevoluteLinkFkRegistrar() {
            NodeDescriptor desc = { "Revolute Link FK", "Physics/Kinematics",
                "Forward kinematics: drives a link's transform from a revolute joint's angle." };
            NodeFactory::instance().registerNodeType("kinematics_revolute_link_fk", desc,
                []() { return std::make_unique<RevoluteLinkFkNode>(); });
        }
    };
    static RevoluteLinkFkRegistrar g_revoluteLinkFkRegistrar;
}

// ---- ArticulationDriveNode: node graph -> live joint motion (the SINGLE writer) ----
ArticulationDriveNode::ArticulationDriveNode() {
    m_id = "physics_articulation_drive";
    m_ports.push_back({ "Angle", {"float", "radians"}, Port::Direction::Input, this });
    m_ports.push_back({ "Joint", {"int", "index"},     Port::Direction::Input, this });
}

void ArticulationDriveNode::compute() {
    if (!m_scene) return;
    auto angle = getInput<float>("Angle");
    if (!angle) return;                                // disconnected -> commands nothing (joint at rest)
    const int joint = getInput<int>("Joint").value_or(0);
    if (joint < 0) return;
    auto& reg = m_scene->getRegistry();
    ArticulationCommandComponent* cmd = reg.ctx().find<ArticulationCommandComponent>();
    if (!cmd) cmd = &reg.ctx().emplace<ArticulationCommandComponent>();
    if (int(cmd->target.size()) <= joint) { cmd->target.resize(joint + 1, 0.0f); cmd->driven.resize(joint + 1, 0); }
    cmd->target[joint] = *angle;
    cmd->driven[joint] = 1;
}

namespace {
    struct ArticulationDriveRegistrar {
        ArticulationDriveRegistrar() {
            NodeDescriptor desc = { "Drive Joint", "Physics/Actions",
                "Commands a live articulation DOF (Joint index) to Angle -- the node graph's joint driver." };
            NodeFactory::instance().registerNodeType("physics_articulation_drive", desc,
                []() { return std::make_unique<ArticulationDriveNode>(); });
        }
    };
    static ArticulationDriveRegistrar g_articulationDriveRegistrar;
}

} // namespace NodeLibrary
