#include "FieldSolver.hpp"
#include "components.hpp"
#include <entt/entt.hpp>

float FieldSolver::getPotentialAt(entt::registry& registry, glm::vec3 worldPos) {
    float totalPotential = 0.0f;
    auto view = registry.view<const FieldSourceTag, const TransformComponent, const PotentialSourceComponent>();

    for (auto entity : view) {
        const auto& transform = view.get<const TransformComponent>(entity);
        const auto& source = view.get<const PotentialSourceComponent>(entity);

        float distSq = glm::distance2(transform.translation, worldPos);
        if (distSq > 1e-6) { // Avoid division by zero
            totalPotential += source.strength / distSq;
        }
    }
    return totalPotential;
}

glm::vec3 FieldSolver::getVectorAt(entt::registry& registry, glm::vec3 worldPos) {
    glm::vec3 totalVector(0.0f);
    auto view = registry.view<const FieldSourceTag, const TransformComponent, const VectorSourceComponent>();

    for (auto entity : view) {
        const auto& source = view.get<const VectorSourceComponent>(entity);
        // For this simple example, the vector field is constant everywhere.
        // A more complex implementation would change based on worldPos.
        totalVector += glm::normalize(source.direction) * source.strength;
    }
    return totalVector;
}

glm::vec3 FieldSolver::getPotentialGradientAt(entt::registry& registry, glm::vec3 worldPos) {
    // Use finite differences to approximate the gradient
    float epsilon = 0.01f;

    float pot_x1 = getPotentialAt(registry, worldPos + glm::vec3(epsilon, 0, 0));
    float pot_x0 = getPotentialAt(registry, worldPos - glm::vec3(epsilon, 0, 0));

    float pot_y1 = getPotentialAt(registry, worldPos + glm::vec3(0, epsilon, 0));
    float pot_y0 = getPotentialAt(registry, worldPos - glm::vec3(0, epsilon, 0));

    float pot_z1 = getPotentialAt(registry, worldPos + glm::vec3(0, 0, epsilon));
    float pot_z0 = getPotentialAt(registry, worldPos - glm::vec3(0, 0, epsilon));

    float dx = (pot_x1 - pot_x0) / (2.0f * epsilon);
    float dy = (pot_y1 - pot_y0) / (2.0f * epsilon);
    float dz = (pot_z1 - pot_z0) / (2.0f * epsilon);

    return glm::vec3(dx, dy, dz);
}