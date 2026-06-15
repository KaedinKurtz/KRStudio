#include "BridgeNodes.hpp"
#include "Scene.hpp"
#include "components.hpp"
#include <entt/entt.hpp>
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

} // namespace NodeLibrary
