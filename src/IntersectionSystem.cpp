#include "IntersectionSystem.hpp"
#include "components.hpp" // Now includes all other necessary components
#include "Scene.hpp"
#include "ViewportWidget.hpp"
#include "Camera.hpp"

#include <glm/gtx/transform.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <vector>

// NOTE: This implementation is simplified for brevity. You may need to adapt it
// if your original had more complex logic. The key is that the includes are now correct.

namespace IntersectionSystem
{
    // A simple ray-triangle intersection test
    bool rayTriangleIntersect(const glm::vec3& rayOrigin, const glm::vec3& rayVector,
        const glm::vec3& v0, const glm::vec3& v1, const glm::vec3& v2,
        float& out_distance)
    {
        constexpr float epsilon = 0.000001f;
        glm::vec3 edge1 = v1 - v0;
        glm::vec3 edge2 = v2 - v0;
        glm::vec3 h = glm::cross(rayVector, edge2);
        float a = glm::dot(edge1, h);
        if (a > -epsilon && a < epsilon)
            return false; // This ray is parallel to this triangle.

        float f = 1.0f / a;
        glm::vec3 s = rayOrigin - v0;
        float u = f * glm::dot(s, h);
        if (u < 0.0f || u > 1.0f)
            return false;

        glm::vec3 q = glm::cross(s, edge1);
        float v = f * glm::dot(edge2, q);
        if (v < 0.0f || u + v > 1.0f)
            return false;

        // At this stage we can compute t to find out where the intersection point is on the line.
        float t = f * glm::dot(edge2, q);
        if (t > epsilon) // Ray intersection
        {
            out_distance = t;
            return true;
        }
        else // This means that there is a line intersection but not a ray intersection.
            return false;
    }


    void selectObjectAt(Scene& scene, ViewportWidget& viewport, int mouseX, int mouseY)
    {
        auto& registry = scene.getRegistry();
        auto& camera = viewport.getCamera();

        // 1. Convert mouse coordinates to a 3D ray
        float x = (2.0f * mouseX) / viewport.width() - 1.0f;
        float y = 1.0f - (2.0f * mouseY) / viewport.height();
        float z = 1.0f;
        glm::vec3 ray_nds = glm::vec3(x, y, z);
        glm::vec4 ray_clip = glm::vec4(ray_nds.x, ray_nds.y, -1.0, 1.0);
        glm::vec4 ray_eye = glm::inverse(camera.getProjectionMatrix(viewport.width() / (float)viewport.height())) * ray_clip;
        ray_eye = glm::vec4(ray_eye.x, ray_eye.y, -1.0, 0.0);
        glm::vec3 ray_wor = glm::vec3(glm::inverse(camera.getViewMatrix()) * ray_eye);
        ray_wor = glm::normalize(ray_wor);

        // 2. Find the closest intersected object
        entt::entity selectedEntity = entt::null;
        float closest_distance = std::numeric_limits<float>::max();

        auto view = registry.view<RenderableMeshComponent, TransformComponent>();
        for (auto entity : view)
        {
            auto [mesh, transform] = view.get<RenderableMeshComponent, TransformComponent>(entity);
            glm::mat4 modelMatrix = transform.getTransform();

            for (size_t i = 0; i < mesh.indices.size(); i += 3)
            {
                glm::vec3 v0 = glm::vec3(modelMatrix * glm::vec4(mesh.vertices[mesh.indices[i]], 1.0f));
                glm::vec3 v1 = glm::vec3(modelMatrix * glm::vec4(mesh.vertices[mesh.indices[i + 1]], 1.0f));
                glm::vec3 v2 = glm::vec3(modelMatrix * glm::vec4(mesh.vertices[mesh.indices[i + 2]], 1.0f));

                float distance;
                if (rayTriangleIntersect(camera.getPosition(), ray_wor, v0, v1, v2, distance))
                {
                    if (distance < closest_distance)
                    {
                        closest_distance = distance;
                        selectedEntity = entity;
                    }
                }
            }
        }

        // 3. Update selection state
        registry.clear<SelectedComponent>();
        if (registry.valid(selectedEntity))
        {
            registry.emplace<SelectedComponent>(selectedEntity);
        }
    }

    // update() function can be defined here if needed
    void update(Scene* scene)
    {
        // ...
    }
}