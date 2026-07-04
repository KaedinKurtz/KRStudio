#include "BridgeNodes.hpp"
#include "Scene.hpp"
#include "components.hpp"
#include "RobotModel.hpp"   // krs::robot::JointNameRegistry + rebuildJointNameRegistry (drive-by-name)
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
    // Joint-primary addressing: drive by stable NAME or CAN nodeId (a string accepts either). When
    // set it WINS over the legacy positional "Joint" index, and survives reorder/re-derive.
    m_ports.push_back({ "Joint Name", {"string", "name"}, Port::Direction::Input, this });
}

void ArticulationDriveNode::compute() {
    if (!m_scene) return;
    auto angle = getInput<float>("Angle");
    if (!angle) return;                                // disconnected -> commands nothing (joint at rest)
    auto& reg = m_scene->getRegistry();
    // Resolve a name/nodeId to the live (robotId, dof) if given -- the ROBOT-KEYED lane, so driving
    // "J2" of robot 0 can never write robot 1's DOF 2 (the multi-robot cross-talk fix).
    if (auto nameIn = getInput<std::string>("Joint Name"); nameIn && !nameIn->empty()) {
        krs::robot::rebuildJointNameRegistry(reg);
        if (const auto* nr = reg.ctx().find<krs::robot::JointNameRegistry>()) {
            if (const auto* jr = nr->resolve(*nameIn)) {
                ArticulationCommandComponent* cmd = reg.ctx().find<ArticulationCommandComponent>();
                if (!cmd) cmd = &reg.ctx().emplace<ArticulationCommandComponent>();
                cmd->setEntry(jr->robotId, jr->dof, *angle);
                return;
            }
        }
        return;   // a named joint that fails to resolve commands NOTHING (never index 0 by accident)
    }
    // Legacy positional index -> the un-keyed lane (drained into the drive-owning robot only).
    const int joint = getInput<int>("Joint").value_or(0);
    if (joint < 0) return;
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

// ---- ConfigDriveNode: a WHOLE joint_config -> per-DOF robot-keyed bus entries (Process&Skills P0) ----
// The missing consumer: ik_target / the OMPL planner emit a joint_config (a bare Eigen::VectorXd --
// positional, NO joint names) as port DATA, but nothing drove the bus with it. This node fans a config
// out as setEntry(robotId, i, q[i]) on the ROBOT-KEYED lane (index i == DOF i of THAT robot, the same
// convention LiveRobot::q uses), re-asserted every eval pass -- so a planned/IK pose actually moves a
// robot, without cross-talk, and releases to manual control when disconnected.
namespace {
    class ConfigDriveNode : public Node {
    public:
        ConfigDriveNode() {
            m_id = "physics_config_drive";
            m_ports.push_back({ "Config", { "joint_config", "handle" }, Port::Direction::Input, this });
            m_ports.push_back({ "Robot",  { "int", "id" },              Port::Direction::Input, this });
            setPortLiteral<int>("Robot", 0);
        }
        void compute() override {
            if (!m_scene) return;
            auto cfg = getInput<Eigen::VectorXd>("Config");
            if (!cfg || cfg->size() == 0) return;      // disconnected/empty -> commands nothing (releases)
            const int robotId = getInput<int>("Robot").value_or(0);
            auto& reg = m_scene->getRegistry();
            ArticulationCommandComponent* cmd = reg.ctx().find<ArticulationCommandComponent>();
            if (!cmd) cmd = &reg.ctx().emplace<ArticulationCommandComponent>();
            for (int i = 0; i < cfg->size(); ++i)
                cmd->setEntry(robotId, i, float((*cfg)[i]));
        }
    };
    struct ConfigDriveRegistrar {
        ConfigDriveRegistrar() {
            NodeDescriptor desc = { "Drive Config", "Physics/Actions",
                "Drives a whole joint_config onto ONE robot's DOFs via the robot-keyed command bus (index i -> DOF i)." };
            NodeFactory::instance().registerNodeType("physics_config_drive", desc,
                []() { return std::make_unique<ConfigDriveNode>(); });
        }
    };
    static ConfigDriveRegistrar g_configDriveRegistrar;
}

} // namespace NodeLibrary
