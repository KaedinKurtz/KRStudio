#include "PhysicsNodes.hpp"
#include <iostream> 
#include <memory> // Required for std::make_unique

namespace NodeLibrary {

    // --- Free Function Implementations (Placeholders) ---
    // (Implementations remain the same)
    glm::vec3 getLinearVelocity(entt::registry& registry, entt::entity entity) {
        if (registry.valid(entity) && registry.all_of<RigidBodyComponent>(entity)) {
            return registry.get<RigidBodyComponent>(entity).linearVelocity;
        }
        std::cerr << "PHYSICS: Tried to get linear velocity from invalid entity.\n";
        return glm::vec3(0.0f);
    }
    void applyForce(entt::registry& registry, entt::entity entity, const glm::vec3& force) {
        if (registry.valid(entity) && registry.all_of<RigidBodyComponent>(entity)) {
            registry.get<RigidBodyComponent>(entity).forceAccumulator += force;
            std::cout << "PHYSICS: Applied force (" << force.x << ", " << force.y << ", " << force.z << ") to entity.\n";
        }
        else {
            std::cerr << "PHYSICS: Tried to apply force to invalid entity.\n";
        }
    }
    void setLinearVelocity(entt::registry& registry, entt::entity entity, const glm::vec3& velocity) {
        if (registry.valid(entity) && registry.all_of<RigidBodyComponent>(entity)) {
            registry.get<RigidBodyComponent>(entity).linearVelocity = velocity;
            std::cout << "PHYSICS: Set linear velocity to (" << velocity.x << ", " << velocity.y << ", " << velocity.z << ") on entity.\n";
        }
        else {
            std::cerr << "PHYSICS: Tried to set linear velocity on invalid entity.\n";
        }
    }
    bool checkBeginOverlap(entt::registry& registry, entt::entity entity_a, entt::entity entity_b, CollisionData& out_data) {
        std::cout << "PHYSICS: Checking for overlap between entities.\n";
        return false;
    }

    // --- Node Implementations & Registrations ---

    // GetLinearVelocityNode
    GetLinearVelocityNode::GetLinearVelocityNode() {
	m_id = "physics_get_linear_velocity";
        // FIX: Use nested initializer {name, unit} for DataType
        m_ports.push_back({ "Registry", {"entt::registry*", "handle"}, Port::Direction::Input, this });
        m_ports.push_back({ "Entity", {"entt::entity", "handle"}, Port::Direction::Input, this });
        m_ports.push_back({ "Velocity", {"glm::vec3", "m/s"}, Port::Direction::Output, this });
    }

    void GetLinearVelocityNode::compute() {
        auto registry = getInput<entt::registry*>("Registry");
        auto entity = getInput<entt::entity>("Entity");
        if (registry && entity) {
            setOutput("Velocity", getLinearVelocity(**registry, *entity));
        }
    }

    namespace {
        struct GetLinearVelocityRegistrar {
            GetLinearVelocityRegistrar() {
                NodeDescriptor desc = { "Get Linear Velocity", "Physics/State", "Gets the linear velocity of a rigid body." };
                // FIX: Use std::make_unique
                NodeFactory::instance().registerNodeType("physics_get_linear_velocity", desc, []() { return std::make_unique<GetLinearVelocityNode>(); });
            }
        };
    }
    static GetLinearVelocityRegistrar g_getLinearVelocityRegistrar;

    // ApplyForceNode
    ApplyForceNode::ApplyForceNode() {
	m_id = "physics_apply_force";
        // FIX: Use nested initializer {name, unit} for DataType
        m_ports.push_back({ "Registry", {"entt::registry*", "handle"}, Port::Direction::Input, this });
        m_ports.push_back({ "Entity", {"entt::entity", "handle"}, Port::Direction::Input, this });
        m_ports.push_back({ "Force", {"glm::vec3", "newtons"}, Port::Direction::Input, this });
    }

    void ApplyForceNode::compute() {
        auto registry = getInput<entt::registry*>("Registry");
        auto entity = getInput<entt::entity>("Entity");
        auto force = getInput<glm::vec3>("Force");
        if (registry && entity && force) {
            applyForce(**registry, *entity, *force);
        }
    }

    namespace {
        struct ApplyForceRegistrar {
            ApplyForceRegistrar() {
                NodeDescriptor desc = { "Apply Force", "Physics/Actions", "Applies a force to the center of mass of a rigid body." };
                // FIX: Use std::make_unique
                NodeFactory::instance().registerNodeType("physics_apply_force", desc, []() { return std::make_unique<ApplyForceNode>(); });
            }
        };
    }
    static ApplyForceRegistrar g_applyForceRegistrar;

    // SetLinearVelocityNode
    SetLinearVelocityNode::SetLinearVelocityNode() {
	m_id = "physics_set_linear_velocity";
        // FIX: Use nested initializer {name, unit} for DataType
        m_ports.push_back({ "Registry", {"entt::registry*", "handle"}, Port::Direction::Input, this });
        m_ports.push_back({ "Entity", {"entt::entity", "handle"}, Port::Direction::Input, this });
        m_ports.push_back({ "Velocity", {"glm::vec3", "m/s"}, Port::Direction::Input, this });
    }

    void SetLinearVelocityNode::compute() {
        auto registry = getInput<entt::registry*>("Registry");
        auto entity = getInput<entt::entity>("Entity");
        auto velocity = getInput<glm::vec3>("Velocity");
        if (registry && entity && velocity) {
            setLinearVelocity(**registry, *entity, *velocity);
        }
    }

    namespace {
        struct SetLinearVelocityRegistrar {
            SetLinearVelocityRegistrar() {
                NodeDescriptor desc = { "Set Linear Velocity", "Physics/Actions", "Directly sets the linear velocity of a rigid body." };
                // FIX: Use std::make_unique
                NodeFactory::instance().registerNodeType("physics_set_linear_velocity", desc, []() { return std::make_unique<SetLinearVelocityNode>(); });
            }
        };
    }
    static SetLinearVelocityRegistrar g_setLinearVelocityRegistrar;

    // CheckBeginOverlapNode
    CheckBeginOverlapNode::CheckBeginOverlapNode() {
	m_id = "physics_check_overlap";
        // FIX: Use nested initializer {name, unit} for DataType
        m_ports.push_back({ "Registry", {"entt::registry*", "handle"}, Port::Direction::Input, this });
        m_ports.push_back({ "Entity A", {"entt::entity", "handle"}, Port::Direction::Input, this });
        m_ports.push_back({ "Entity B", {"entt::entity", "handle"}, Port::Direction::Input, this });
        m_ports.push_back({ "Has Overlap", {"bool", "boolean"}, Port::Direction::Output, this });
        m_ports.push_back({ "Collision Data", {"CollisionData", "data"}, Port::Direction::Output, this });
    }

    void CheckBeginOverlapNode::compute() {
        auto registry = getInput<entt::registry*>("Registry");
        auto entityA = getInput<entt::entity>("Entity A");
        auto entityB = getInput<entt::entity>("Entity B");
        if (registry && entityA && entityB) {
            CollisionData data;
            bool hasOverlap = checkBeginOverlap(**registry, *entityA, *entityB, data);
            setOutput("Has Overlap", hasOverlap);
            if (hasOverlap) {
                setOutput("Collision Data", data);
            }
        }
    }

    namespace {
        struct CheckBeginOverlapRegistrar {
            CheckBeginOverlapRegistrar() {
                NodeDescriptor desc = { "Check Overlap", "Physics/Events", "Checks if two entities are currently overlapping." };
                // FIX: Use std::make_unique
                NodeFactory::instance().registerNodeType("physics_check_overlap", desc, []() { return std::make_unique<CheckBeginOverlapNode>(); });
            }
        };
    }
    static CheckBeginOverlapRegistrar g_checkBeginOverlapRegistrar;



QWidget* CheckBeginOverlapNode::createCustomWidget()
{
    // TODO: Implement custom widget for "CheckBeginOverlapNode"
    return nullptr;
}


QWidget* SetLinearVelocityNode::createCustomWidget()
{
    // TODO: Implement custom widget for "SetLinearVelocityNode"
    return nullptr;
}


QWidget* ApplyForceNode::createCustomWidget()
{
    // TODO: Implement custom widget for "ApplyForceNode"
    return nullptr;
}


QWidget* GetLinearVelocityNode::createCustomWidget()
{
    // TODO: Implement custom widget for "GetLinearVelocityNode"
    return nullptr;
}
} // namespace NodeLibrary