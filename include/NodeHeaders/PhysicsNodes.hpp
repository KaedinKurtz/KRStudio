#pragma once

#include "Node.hpp"
#include "NodeFactory.hpp"
#include "components.hpp" // For RigidBodyComponent
#include <entt/entt.hpp>
#include <glm/glm.hpp>

namespace NodeLibrary {

    // --- Data Structures ---

    struct CollisionData {
        entt::entity other_entity = entt::null;
        glm::vec3 contact_point{ 0.f };
        glm::vec3 contact_normal{ 0.f };
        float impulse_magnitude = 0.f;
    };

    // --- Free Functions (to be wrapped by nodes) ---

    glm::vec3 getLinearVelocity(entt::registry& registry, entt::entity entity);
    void applyForce(entt::registry& registry, entt::entity entity, const glm::vec3& force);
    void setLinearVelocity(entt::registry& registry, entt::entity entity, const glm::vec3& velocity);
    bool checkBeginOverlap(entt::registry& registry, entt::entity entity_a, entt::entity entity_b, CollisionData& out_data);


    // --- Node Classes ---

    class GetLinearVelocityNode : public Node {
    public:
        GetLinearVelocityNode();
        void compute() override;
    };

    class ApplyForceNode : public Node {
    public:
        ApplyForceNode();
        void compute() override;
    };

    class SetLinearVelocityNode : public Node {
    public:
        SetLinearVelocityNode();
        void compute() override;
    };

    class CheckBeginOverlapNode : public Node {
    public:
        CheckBeginOverlapNode();
        void compute() override;
    };

} // namespace NodeLibrary