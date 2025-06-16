#pragma once

#include <vector>
#include <glm/glm.hpp>
#include <entt/entt.hpp>

// Forward declarations
class Scene;
class ViewportWidget;

namespace IntersectionSystem
{
    struct IntersectionResult
    {
        bool isIntersecting = false;
        entt::entity intersectingGrid = entt::null;
        std::vector<glm::vec3> worldOutlinePoints3D;

    };

    // REFACTOR: The component now lives in the same header as the system that uses it.
    // This breaks the circular dependency.
    struct IntersectionComponent {
        IntersectionResult result;
    };


    // The public interface for the system
    void update(Scene* scene);
    void selectObjectAt(Scene& scene, ViewportWidget& viewport, int mouseX, int mouseY);
}