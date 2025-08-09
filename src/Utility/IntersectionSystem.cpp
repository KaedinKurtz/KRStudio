#include "IntersectionSystem.hpp"
#include "components.hpp"
#include "Scene.hpp"
#include "ViewportWidget.hpp"
#include "Camera.hpp"

#include <glm/gtx/transform.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <vector>
#include <optional>
#include <limits>
#include <QDebug>

namespace IntersectionSystem
{
    // ========================================================================
    // --- Outline Calculation Logic (unchanged) ---
    // ========================================================================

    static bool lineSegmentPlaneIntersection(const glm::vec3& p1, const glm::vec3& p2, const glm::vec4& plane, glm::vec3& outIntersectionPoint)
    {
        glm::vec3 lineDir = p2 - p1;
        float dotLineNormal = glm::dot(glm::vec3(plane), lineDir);
        if (std::abs(dotLineNormal) < 1e-6f) return false;
        float t = (-plane.w - glm::dot(glm::vec3(plane), p1)) / dotLineNormal;
        if (t >= 0.0f && t <= 1.0f) {
            outIntersectionPoint = p1 + t * lineDir;
            return true;
        }
        return false;
    }

    static std::vector<glm::vec2> calculateConvexHull(std::vector<glm::vec2>& points)
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

    // Returns all outlines for enabled grids vs “Test Cube” AABBs (as in your file)
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
                    {-0.5f,-0.5f,-0.5f}, {0.5f,-0.5f,-0.5f}, {0.5f,0.5f,-0.5f}, {-0.5f,0.5f,-0.5f},
                    {-0.5f,-0.5f, 0.5f}, {0.5f,-0.5f, 0.5f}, {0.5f,0.5f, 0.5f}, {-0.5f,0.5f, 0.5f}
                };
                int edges[12][2] = { {0,1},{1,2},{2,3},{3,0}, {4,5},{5,6},{6,7},{7,4}, {0,4},{1,5},{2,6},{3,7} };

                std::vector<glm::vec3> intersectionPoints3D;
                intersectionPoints3D.reserve(12);
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
                    points2D.reserve(intersectionPoints3D.size());
                    for (const auto& p3d : intersectionPoints3D) {
                        glm::vec4 localPoint = worldToGrid * glm::vec4(p3d, 1.0f);
                        points2D.emplace_back(localPoint.x, localPoint.z);
                    }
                    std::vector<glm::vec2> hullPoints2D = calculateConvexHull(points2D);
                    std::vector<glm::vec3> finalOutlinePoints3D;
                    finalOutlinePoints3D.reserve(hullPoints2D.size());
                    for (const auto& p2d : hullPoints2D) {
                        finalOutlinePoints3D.emplace_back(glm::vec3(gridModelMatrix * glm::vec4(p2d.x, 0.0f, p2d.y, 1.0f)));
                    }
                    allOutlines.push_back(std::move(finalOutlinePoints3D));
                }
            }
        }
        return allOutlines;
    }

    // ========================================================================
    // --- CPU picking helpers (NEW) ---
    // ========================================================================

    struct CpuRay { glm::vec3 origin; glm::vec3 dir; };

    static CpuRay makeRayFromScreen(int px, int py, int vpW, int vpH, const Camera& cam)
    {
        // Screen -> NDC
        float x = (2.0f * float(px) / float(vpW)) - 1.0f;
        float y = 1.0f - (2.0f * float(py) / float(vpH)); // flip Y

        glm::mat4 P = cam.getProjectionMatrix(float(vpW) / float(vpH));
        glm::mat4 V = cam.getViewMatrix();
        glm::mat4 invVP = glm::inverse(P * V);

        glm::vec4 nearW = invVP * glm::vec4(x, y, -1.0f, 1.0f);
        glm::vec4 farW = invVP * glm::vec4(x, y, 1.0f, 1.0f);
        nearW /= nearW.w; farW /= farW.w;

        CpuRay r;
        r.origin = glm::vec3(nearW);
        r.dir = glm::normalize(glm::vec3(farW - nearW));
        return r;
    }

    // Robust slab test (no glm::compMax/Min dependency)
    static bool intersectRayAABB(const glm::vec3& ro, const glm::vec3& rd,
        const glm::vec3& bmin, const glm::vec3& bmax,
        float& tEnter, float& tExit)
    {
        glm::vec3 invD(1.0f / rd.x, 1.0f / rd.y, 1.0f / rd.z);

        glm::vec3 t0 = (bmin - ro) * invD;
        glm::vec3 t1 = (bmax - ro) * invD;

        glm::vec3 tminVec(std::min(t0.x, t1.x),
            std::min(t0.y, t1.y),
            std::min(t0.z, t1.z));

        glm::vec3 tmaxVec(std::max(t0.x, t1.x),
            std::max(t0.y, t1.y),
            std::max(t0.z, t1.z));

        tEnter = std::max(std::max(tminVec.x, tminVec.y), tminVec.z);
        tExit = std::min(std::min(tmaxVec.x, tmaxVec.y), tmaxVec.z);

        return (tExit >= tEnter) && (tExit >= 0.0f);
    }

    struct CpuPickHit {
        entt::entity entity{ entt::null };
        glm::vec3    worldPos{ 0 };
        float        worldT{ std::numeric_limits<float>::max() };
    };

    static std::optional<CpuPickHit>
        cpuPickAABB(Scene& scene, const Camera& cam, int px, int py, int vpW, int vpH)
    {
        CpuRay ray = makeRayFromScreen(px, py, vpW, vpH, cam);

        auto& reg = scene.getRegistry();
        auto view = reg.view<TransformComponent, RenderableMeshComponent>();

        CpuPickHit best;

        for (auto [e, xform, mesh] : view.each())
        {
            // Skip non-renderables if needed (optional)
            // if (!reg.all_of<RenderableTag>(e)) continue;

            const glm::mat4 M = reg.all_of<WorldTransformComponent>(e)
                ? reg.get<WorldTransformComponent>(e).matrix
                : xform.getTransform();

            glm::mat4 invM = glm::inverse(M);

            // Transform ray to local space
            glm::vec3 roL = glm::vec3(invM * glm::vec4(ray.origin, 1.0f));
            glm::vec3 rdL = glm::normalize(glm::vec3(invM * glm::vec4(ray.dir, 0.0f)));

            float t0, t1;
            if (!intersectRayAABB(roL, rdL, mesh.aabbMin, mesh.aabbMax, t0, t1))
                continue;

            float tLocal = (t0 < 0.0f) ? t1 : t0; // handle origin inside box

            glm::vec3 localHit = roL + rdL * tLocal;
            glm::vec3 worldHit = glm::vec3(M * glm::vec4(localHit, 1.0f));
            float      worldDist = glm::length(worldHit - ray.origin);

            if (worldDist < best.worldT) {
                best.entity = e;
                best.worldPos = worldHit;
                best.worldT = worldDist;
            }
        }

        if (best.entity != entt::null) return best;
        return std::nullopt;
    }

    // ========================================================================
    // --- Object Selection Logic (REWRITTEN: CPU AABB) ---
    // ========================================================================

    void selectObjectAt(Scene& scene, ViewportWidget& viewport, int mouseX, int mouseY)
    {
        auto& reg = scene.getRegistry();
        Camera& cam = viewport.getCamera();

        // nearest AABB hit
        auto hit = cpuPickAABB(scene, cam, mouseX, mouseY, viewport.width(), viewport.height());

        // clear previous selection
        for (auto eSel : reg.view<SelectedComponent>()) reg.remove<SelectedComponent>(eSel);

        if (hit && reg.valid(hit->entity)) {
            reg.emplace<SelectedComponent>(hit->entity);
            // Optional: also recenter orbit right here:
            // float keepDist = glm::length(cam.getPosition() - hit->worldPos);
            // cam.focusOn(hit->worldPos, keepDist);
        }
    }

    std::optional<glm::vec3> pickPoint(Scene& scene, ViewportWidget& vp, int mouseX, int mouseY)
    {
        Camera& cam = vp.getCamera();
        auto hit = cpuPickAABB(scene, cam, mouseX, mouseY, vp.width(), vp.height());
        if (hit) return hit->worldPos;
        return std::nullopt;
    }
}
