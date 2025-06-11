#include "IntersectionSystem.hpp"
#include "components.hpp"
#include <glm/gtx/transform.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <vector>

namespace IntersectionSystem
{
    // Helper function to find the intersection point between a line segment and a plane.
    bool lineSegmentPlaneIntersection(const glm::vec3& p1, const glm::vec3& p2, const glm::vec4& plane, glm::vec3& outIntersectionPoint)
    {
        glm::vec3 lineDir = p2 - p1;
        float dotLineNormal = glm::dot(glm::vec3(plane), lineDir);
        if (std::abs(dotLineNormal) < 1e-6) return false;
        float t = (-plane.w - glm::dot(glm::vec3(plane), p1)) / dotLineNormal;
        if (t >= 0.0f && t <= 1.0f) {
            outIntersectionPoint = p1 + t * lineDir;
            return true;
        }
        return false;
    }

    // Monotone Chain algorithm to find the convex hull of a set of 2D points.
    std::vector<glm::vec2> calculateConvexHull(std::vector<glm::vec2>& points)
    {
        if (points.size() <= 3) return points;

        std::sort(points.begin(), points.end(), [](const glm::vec2& a, const glm::vec2& b) {
            return a.x < b.x || (a.x == b.x && a.y < b.y);
            });

        std::vector<glm::vec2> hull;
        auto cross_product = [](const glm::vec2& O, const glm::vec2& A, const glm::vec2& B) {
            return (A.x - O.x) * (B.y - O.y) - (A.y - O.y) * (B.x - O.x);
            };

        for (const auto& p : points) {
            while (hull.size() >= 2 && cross_product(hull[hull.size() - 2], hull.back(), p) <= 0) {
                hull.pop_back();
            }
            hull.push_back(p);
        }

        size_t lower_hull_size = hull.size();
        for (int i = points.size() - 2; i >= 0; --i) {
            const auto& p = points[i];
            while (hull.size() > lower_hull_size && cross_product(hull[hull.size() - 2], hull.back(), p) <= 0) {
                hull.pop_back();
            }
            hull.push_back(p);
        }
        hull.pop_back();
        return hull;
    }


    void update(Scene* scene)
    {
        auto& registry = scene->getRegistry();
        auto sliceableView = registry.view<TransformComponent, IntersectionComponent>();
        auto gridView = registry.view<TransformComponent, GridComponent>();

        for (auto entity : sliceableView) {
            auto& intersection = sliceableView.get<IntersectionComponent>(entity);
            intersection.result = {};
        }

        for (auto gridEntity : gridView)
        {
            auto& gridTransform = gridView.get<TransformComponent>(gridEntity);
            auto& grid = gridView.get<GridComponent>(gridEntity);
            if (!grid.showIntersections) continue;

            glm::mat4 gridModelMatrix = gridTransform.getTransform();
            glm::vec3 worldPlaneNormal = glm::normalize(glm::vec3(gridModelMatrix * glm::vec4(0.0f, 1.0f, 0.0f, 0.0f)));
            glm::vec3 worldPlanePoint = glm::vec3(gridModelMatrix * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
            glm::vec4 worldPlane(worldPlaneNormal, -glm::dot(worldPlaneNormal, worldPlanePoint));
            glm::mat4 worldToGrid = glm::inverse(gridModelMatrix);

            for (auto meshEntity : sliceableView)
            {
                auto& intersectionComp = sliceableView.get<IntersectionComponent>(meshEntity);
                if (intersectionComp.result.isIntersecting) continue;

                auto& meshTransform = sliceableView.get<TransformComponent>(meshEntity);
                glm::mat4 meshModelMatrix = meshTransform.getTransform();

                glm::vec3 p[8] = {
                    glm::vec3(-0.5,-0.5,-0.5), glm::vec3(0.5,-0.5,-0.5), glm::vec3(0.5,0.5,-0.5), glm::vec3(-0.5,0.5,-0.5),
                    glm::vec3(-0.5,-0.5, 0.5), glm::vec3(0.5,-0.5, 0.5), glm::vec3(0.5,0.5, 0.5), glm::vec3(-0.5,0.5, 0.5)
                };
                int edges[12][2] = { {0,1},{1,2},{2,3},{3,0}, {4,5},{5,6},{6,7},{7,4}, {0,4},{1,5},{2,6},{3,7} };

                std::vector<glm::vec3> intersectionPoints3D;
                for (int i = 0; i < 12; ++i) {
                    glm::vec3 p1 = glm::vec3(meshModelMatrix * glm::vec4(p[edges[i][0]], 1.0));
                    glm::vec3 p2 = glm::vec3(meshModelMatrix * glm::vec4(p[edges[i][1]], 1.0));
                    glm::vec3 intersectionPoint;
                    if (lineSegmentPlaneIntersection(p1, p2, worldPlane, intersectionPoint)) {
                        intersectionPoints3D.push_back(intersectionPoint);
                    }
                }

                if (intersectionPoints3D.size() > 2)
                {
                    std::vector<glm::vec2> points2D;
                    for (const auto& p3d : intersectionPoints3D) {
                        // --- THIS IS THE FIX ---
                        // Transform the 3D world point into the grid's local space.
                        glm::vec4 localPoint = worldToGrid * glm::vec4(p3d, 1.0f);
                        // Create the 2D point using the local X and Z coordinates, since the grid plane is XZ.
                        points2D.push_back(glm::vec2(localPoint.x, localPoint.z));
                    }

                    std::vector<glm::vec2> hullPoints2D = calculateConvexHull(points2D);

                    std::vector<glm::vec3> finalOutlinePoints3D;
                    for (const auto& p2d : hullPoints2D) {
                        // When converting back, use the 2D point's y for the 3D point's Z coordinate.
                        finalOutlinePoints3D.push_back(glm::vec3(gridModelMatrix * glm::vec4(p2d.x, 0.0f, p2d.y, 1.0f)));
                    }

                    intersectionComp.result.isIntersecting = true;
                    intersectionComp.result.intersectingGrid = gridEntity;
                    intersectionComp.result.worldOutlinePoints3D = finalOutlinePoints3D;
                }
            }
        }
    }
}
