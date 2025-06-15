#pragma once

#include "Scene.hpp"
#include <vector>
#include <glm/glm.hpp>

// --- FIX ---
// Use the full EnTT entity header so that entt::entity and entt::null are
// defined wherever this file is included.
#include <entt/entity/entity.hpp>

namespace IntersectionSystem
{
    struct IntersectionResult
    {
        bool isIntersecting = false;
        entt::entity intersectingGrid = entt::null;
        std::vector<glm::vec2> intersectionPoints2D;
        std::vector<glm::vec2> convexHull2D;
        glm::vec2 majorAxisStart_2D{ 0.0f }, majorAxisEnd_2D{ 0.0f };
        glm::vec2 minorAxisStart_2D{ 0.0f }, minorAxisEnd_2D{ 0.0f };
        float majorDiameter = 0.0f;
        float minorDiameter = 0.0f;
        std::vector<glm::vec3> worldOutlinePoints3D;
        glm::vec3 worldMajorAxisStart3D{ 0.0f }, worldMajorAxisEnd3D{ 0.0f };
        glm::vec3 worldMinorAxisStart3D{ 0.0f }, worldMinorAxisEnd3D{ 0.0f };
    };

    void update(Scene* scene);
}
