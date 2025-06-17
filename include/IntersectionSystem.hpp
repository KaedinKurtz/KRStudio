#pragma once

#include <glm/glm.hpp>
#include <vector>
#include <optional>

// Forward declarations
class Scene;
class ViewportWidget;

namespace IntersectionSystem
{
    // Calculates and returns all intersection outlines for a given scene state.
    std::vector<std::vector<glm::vec3>> update(Scene* scene);

    // Handles object selection via mouse-clicking.
    void selectObjectAt(Scene& scene, ViewportWidget& viewport, int mouseX, int mouseY);

    std::optional<glm::vec3> pickPoint(Scene& scene,
        ViewportWidget& vp,
        int mouseX, int mouseY);
}
