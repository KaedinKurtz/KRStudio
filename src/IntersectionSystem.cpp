#include "IntersectionSystem.hpp"
#include "components.hpp"
#include "Scene.hpp"
#include "ViewportWidget.hpp"
#include "Camera.hpp"

#include <glm/gtx/transform.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <vector>
#include <QDebug>

namespace IntersectionSystem
{
    // ========================================================================
    // --- Outline Calculation Logic ---
    // ========================================================================

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
        for (int i = int(points.size()) - 2; i >= 0; --i) {
            const auto& p = points[i];
            while (hull.size() > lower_hull_size && cross_product(hull[hull.size() - 2], hull.back(), p) <= 0) {
                hull.pop_back();
            }
            hull.push_back(p);
        }
        hull.pop_back();
        return hull;
    }

    // THE FIX: This function now RETURNS the calculated outlines directly.
    std::vector<std::vector<glm::vec3>> update(Scene* scene)
    {
        std::vector<std::vector<glm::vec3>> allOutlines;
        auto& registry = scene->getRegistry();

        auto sliceableView = registry.view<TransformComponent, RenderableMeshComponent, TagComponent>();
        auto gridView = registry.view<TransformComponent, GridComponent>();

        for (auto gridEntity : gridView)
        {
            auto& grid = gridView.get<GridComponent>(gridEntity);
            if (!grid.showIntersections) continue;

            glm::mat4 gridModelMatrix = gridView.get<TransformComponent>(gridEntity).getTransform();
            glm::vec3 worldPlaneNormal = glm::normalize(glm::vec3(gridModelMatrix * glm::vec4(0.0f, 1.0f, 0.0f, 0.0f)));
            glm::vec3 worldPlanePoint = glm::vec3(gridModelMatrix * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
            glm::vec4 worldPlane(worldPlaneNormal, -glm::dot(worldPlaneNormal, worldPlanePoint));
            glm::mat4 worldToGrid = glm::inverse(gridModelMatrix);

            for (auto meshEntity : sliceableView)
            {
                if (registry.get<TagComponent>(meshEntity).tag != "Test Cube") continue;

                glm::mat4 meshModelMatrix = sliceableView.get<TransformComponent>(meshEntity).getTransform();
                glm::vec3 p[8] = {
                    glm::vec3(-0.5f,-0.5f,-0.5f), glm::vec3(0.5f,-0.5f,-0.5f), glm::vec3(0.5f,0.5f,-0.5f), glm::vec3(-0.5f,0.5f,-0.5f),
                    glm::vec3(-0.5f,-0.5f, 0.5f), glm::vec3(0.5f,-0.5f, 0.5f), glm::vec3(0.5f,0.5f, 0.5f), glm::vec3(-0.5f,0.5f, 0.5f)
                };
                int edges[12][2] = { {0,1},{1,2},{2,3},{3,0}, {4,5},{5,6},{6,7},{7,4}, {0,4},{1,5},{2,6},{3,7} };

                std::vector<glm::vec3> intersectionPoints3D;
                for (int i = 0; i < 12; ++i) {
                    glm::vec3 p1 = glm::vec3(meshModelMatrix * glm::vec4(p[edges[i][0]], 1.0f));
                    glm::vec3 p2 = glm::vec3(meshModelMatrix * glm::vec4(p[edges[i][1]], 1.0f));
                    glm::vec3 intersectionPoint;
                    if (lineSegmentPlaneIntersection(p1, p2, worldPlane, intersectionPoint)) {
                        intersectionPoints3D.push_back(intersectionPoint);
                    }
                }

                if (intersectionPoints3D.size() > 2) {
                    std::vector<glm::vec2> points2D;
                    for (const auto& p3d : intersectionPoints3D) {
                        glm::vec4 localPoint = worldToGrid * glm::vec4(p3d, 1.0f);
                        points2D.push_back(glm::vec2(localPoint.x, localPoint.z));
                    }
                    std::vector<glm::vec2> hullPoints2D = calculateConvexHull(points2D);
                    std::vector<glm::vec3> finalOutlinePoints3D;
                    for (const auto& p2d : hullPoints2D) {
                        finalOutlinePoints3D.push_back(glm::vec3(gridModelMatrix * glm::vec4(p2d.x, 0.0f, p2d.y, 1.0f)));
                    }
                    allOutlines.push_back(finalOutlinePoints3D);
                }
            }
        }
        return allOutlines;
    }


    // ========================================================================
    // --- Object Selection Logic (Unchanged) ---
    // ========================================================================

    inline bool rayTriangleIntersect(const glm::vec3& rayOrigin,
        glm::vec3       rayDir,
        const glm::vec3& v0,
        const glm::vec3& v1,
        const glm::vec3& v2,
        float& tHit,
        glm::vec3& pHit)
    {
        rayDir = glm::normalize(rayDir);               // 1) length-independent t

        constexpr float kEps = 1e-6f;
        glm::vec3  edge1 = v1 - v0;
        glm::vec3  edge2 = v2 - v0;
        glm::vec3  pvec = glm::cross(rayDir, edge2);
        float det = glm::dot(edge1, pvec);

        if (std::abs(det) < kEps) return false;        // ray parallel to tri
        float invDet = 1.0f / det;

        glm::vec3 tvec = rayOrigin - v0;
        float u = glm::dot(tvec, pvec) * invDet;
        if (u < 0.0f || u > 1.0f) return false;

        glm::vec3 qvec = glm::cross(tvec, edge1);
        float v = glm::dot(rayDir, qvec) * invDet;
        if (v < 0.0f || u + v > 1.0f) return false;

        float t = glm::dot(edge2, qvec) * invDet;
        if (t < kEps) return false;                    // hit behind origin

        tHit = t;
        pHit = rayOrigin + rayDir * t;
        return true;
    }

    // thin wrapper so existing code compiles unchanged
    inline bool rayTriangleIntersect(const glm::vec3& rayOrigin,
        const glm::vec3& rayDir,
        const glm::vec3& v0,
        const glm::vec3& v1,
        const glm::vec3& v2,
        float& tHit)
    {
        glm::vec3 dummy;
        return rayTriangleIntersect(rayOrigin, rayDir, v0, v1, v2, tHit, dummy);
    }

    void selectObjectAt(Scene& scene, ViewportWidget& viewport, int mouseX, int mouseY)
    {
        auto& registry = scene.getRegistry();
        auto& camera = viewport.getCamera();
        float x = (2.0f * mouseX) / viewport.width() - 1.0f;
        float y = 1.0f - (2.0f * mouseY) / viewport.height();
        float z = 1.0f;
        glm::vec3 ray_nds = glm::vec3(x, y, z);
        glm::vec4 ray_clip(ray_nds.x, ray_nds.y, -1.0f, 1.0f);
        glm::vec4 ray_eye = glm::inverse(camera.getProjectionMatrix(viewport.width() / static_cast<float>(viewport.height()))) * ray_clip;
        ray_eye = glm::vec4(ray_eye.x, ray_eye.y, -1.0f, 0.0f);
        glm::vec3 ray_wor = glm::normalize(glm::vec3(glm::inverse(camera.getViewMatrix()) * ray_eye));

        entt::entity selectedEntity = entt::null;
        float closest_distance = std::numeric_limits<float>::max();
        auto view = registry.view<RenderableMeshComponent, TransformComponent>();
        for (auto entity : view)
        {
            auto [mesh, transform] = view.get<RenderableMeshComponent, TransformComponent>(entity);
            glm::mat4 modelMatrix = transform.getTransform();
            for (size_t i = 0; i < mesh.indices.size(); i += 3)
            {
                const glm::vec3& p0 = mesh.vertices[mesh.indices[i]].position;
                const glm::vec3& p1 = mesh.vertices[mesh.indices[i + 1]].position;
                const glm::vec3& p2 = mesh.vertices[mesh.indices[i + 2]].position;
                glm::vec3 v0 = glm::vec3(modelMatrix * glm::vec4(p0, 1.0f));
                glm::vec3 v1 = glm::vec3(modelMatrix * glm::vec4(p1, 1.0f));
                glm::vec3 v2 = glm::vec3(modelMatrix * glm::vec4(p2, 1.0f));
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
        registry.clear<SelectedComponent>();
        if (registry.valid(selectedEntity))
        {
            registry.emplace<SelectedComponent>(selectedEntity);
        }
    }
    std::optional<glm::vec3> pickPoint(Scene& scene, ViewportWidget& vp, int mouseX, int mouseY)
    {
        auto& reg = scene.getRegistry();
        Camera& cam = vp.getCamera();

        // ----- build world-space ray ------------------------------------------------
        float nx = (2.0f * mouseX) / vp.width() - 1.0f;
        float ny = -(2.0f * mouseY) / vp.height() + 1.0f;
        glm::vec4 rayClip(nx, ny, -1.0f, 1.0f);

        glm::mat4 invProj = glm::inverse(
            cam.getProjectionMatrix(vp.width() / float(vp.height())));
        glm::vec4 rayEye = invProj * rayClip;
        rayEye.z = -1.0f;  rayEye.w = 0.0f;

        glm::vec3 rayDir = glm::normalize(glm::vec3(
            glm::inverse(cam.getViewMatrix()) * rayEye));

        // ----- traverse scene ------------------------------------------------------
        float bestT = std::numeric_limits<float>::max();
        glm::vec3 bestPt;

        auto view = reg.view<RenderableMeshComponent, TransformComponent>();
        for (auto e : view) {
            auto [mesh, xf] = view.get<RenderableMeshComponent, TransformComponent>(e);
            glm::mat4 M = reg.all_of<WorldTransformComponent>(e)
                ? reg.get<WorldTransformComponent>(e).matrix
                : xf.getTransform();
            for (size_t i = 0; i < mesh.indices.size(); i += 3) {
                glm::vec3 v0 = glm::vec3(M * glm::vec4(mesh.vertices[mesh.indices[i]].position, 1));
                glm::vec3 v1 = glm::vec3(M * glm::vec4(mesh.vertices[mesh.indices[i + 1]].position, 1));
                glm::vec3 v2 = glm::vec3(M * glm::vec4(mesh.vertices[mesh.indices[i + 2]].position, 1));
                float t; glm::vec3 p;
                if (rayTriangleIntersect(cam.getPosition(), rayDir, v0, v1, v2, t, p)
                    && t < bestT) {
                    bestT = t; bestPt = p;
                }
            }
        }
        if (bestT < std::numeric_limits<float>::max())
            return bestPt;                        // hit found
        return std::nullopt;                      // nothing under cursor
    }
}
