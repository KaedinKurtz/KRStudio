#pragma once
#include <glm/glm.hpp>
#include <entt/fwd.hpp>

class FieldSolver {
public:
    // Calculates the total scalar potential at a point from all sources.
    float getPotentialAt(entt::registry& registry, glm::vec3 worldPos, const std::vector<entt::entity>& sources = {});

    // Calculates the total vector at a point from all sources.
    glm::vec3 getVectorAt(entt::registry& registry, glm::vec3 worldPos, const std::vector<entt::entity>& sources = {});

    // Calculates the gradient of the potential field at a point using finite differences.
    glm::vec3 getPotentialGradientAt(entt::registry& registry, glm::vec3 worldPos, const std::vector<entt::entity>& sources = {});
    
};