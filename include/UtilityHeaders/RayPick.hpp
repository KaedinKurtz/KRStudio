#pragma once
// RayPick.hpp -- SINGLE SOURCE OF TRUTH for scene-click ray picking. The old viewport pick was
// ray-vs-AABB only (coarse: it selects a body whenever the ray clips its bounding box, even if the
// ray misses the actual surface -- and CAD AABBs are often degenerate/zero). This adds exact
// ray-TRIANGLE refinement (Moller-Trumbore) over the mesh, so the pick selects the body the ray
// truly hits (and the nearest one along the ray). Consumed by ViewportWidget AND the raycast gate
// (GATE 3.1), so the gate exercises the production pick.

#include <glm/glm.hpp>
#include <entt/entt.hpp>
#include "components.hpp"   // Vertex, TransformComponent, RenderableMeshComponent

#include <optional>
#include <cmath>

namespace krs::pick {

struct Ray { glm::vec3 origin{ 0.0f }; glm::vec3 dir{ 0.0f, 0.0f, -1.0f }; };
struct PickHit {
    entt::entity entity{ entt::null };
    glm::vec3 worldPos{ 0.0f };
    float t = 3.0e38f;     // world distance along the (normalised) ray
    int tri = -1;          // triangle index within the picked mesh
};

// Screen pixel -> world ray, from the view + projection matrices (no Camera dependency).
inline Ray makeRayFromScreen(const glm::mat4& proj, const glm::mat4& view, int px, int py, int w, int h)
{
    const float x = 2.0f * float(px) / float(w) - 1.0f;
    const float y = 1.0f - 2.0f * float(py) / float(h);     // NDC, Y up
    const glm::mat4 invVP = glm::inverse(proj * view);
    glm::vec4 n = invVP * glm::vec4(x, y, -1.0f, 1.0f);
    glm::vec4 f = invVP * glm::vec4(x, y, 1.0f, 1.0f);
    n /= n.w; f /= f.w;
    Ray r; r.origin = glm::vec3(n); r.dir = glm::normalize(glm::vec3(f - n));
    return r;
}

// Slab test; tEnter/tExit are ray params. Used only as a cheap cull before the triangle test.
inline bool rayAABB(const glm::vec3& ro, const glm::vec3& rd, const glm::vec3& bmin, const glm::vec3& bmax,
                    float& tEnter, float& tExit)
{
    const glm::vec3 inv = 1.0f / rd;
    const glm::vec3 t0 = (bmin - ro) * inv, t1 = (bmax - ro) * inv;
    const glm::vec3 lo = glm::min(t0, t1), hi = glm::max(t0, t1);
    tEnter = glm::max(glm::max(lo.x, lo.y), lo.z);
    tExit  = glm::min(glm::min(hi.x, hi.y), hi.z);
    return tExit >= tEnter && tExit >= 0.0f;
}

// Moller-Trumbore. dir need not be normalised; t is in dir units. Returns true (with t) on a
// front-or-back intersection inside the triangle; the caller decides front (t>0).
inline bool rayTriangle(const glm::vec3& ro, const glm::vec3& rd,
                        const glm::vec3& a, const glm::vec3& b, const glm::vec3& c, float& t)
{
    const glm::vec3 e1 = b - a, e2 = c - a;
    const glm::vec3 p = glm::cross(rd, e2);
    const float det = glm::dot(e1, p);
    if (std::abs(det) < 1e-12f) return false;            // ray parallel to the triangle
    const float invDet = 1.0f / det;
    const glm::vec3 tv = ro - a;
    const float u = glm::dot(tv, p) * invDet;
    if (u < -1e-6f || u > 1.0f + 1e-6f) return false;
    const glm::vec3 q = glm::cross(tv, e1);
    const float v = glm::dot(rd, q) * invDet;
    if (v < -1e-6f || u + v > 1.0f + 1e-6f) return false;
    t = glm::dot(e2, q) * invDet;
    return true;
}

// Nearest mesh-TRIANGLE hit along the ray across all renderable entities. Transforms the ray into
// each body's local frame, ray-triangle over its mesh, keeps the nearest WORLD distance.
inline std::optional<PickHit> pickMesh(entt::registry& reg, const Ray& ray)
{
    PickHit best;
    for (auto e : reg.view<TransformComponent, RenderableMeshComponent>()) {
        const auto& xf = reg.get<TransformComponent>(e);
        const auto& mesh = reg.get<RenderableMeshComponent>(e);
        if (mesh.indices.size() < 3 || mesh.vertices.empty()) continue;
        const glm::mat4 M = xf.getTransform();
        const glm::mat4 invM = glm::inverse(M);
        const glm::vec3 roL = glm::vec3(invM * glm::vec4(ray.origin, 1.0f));
        const glm::vec3 rdL = glm::normalize(glm::vec3(invM * glm::vec4(ray.dir, 0.0f)));
        for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
            const glm::vec3& a = mesh.vertices[mesh.indices[i]].position;
            const glm::vec3& b = mesh.vertices[mesh.indices[i + 1]].position;
            const glm::vec3& c = mesh.vertices[mesh.indices[i + 2]].position;
            float tL;
            if (!rayTriangle(roL, rdL, a, b, c, tL)) continue;
            if (tL <= 1e-6f) continue;                   // behind the ray origin (local)
            const glm::vec3 worldHit = glm::vec3(M * glm::vec4(roL + rdL * tL, 1.0f));
            const float worldT = glm::dot(worldHit - ray.origin, ray.dir);  // dir is world-normalised
            if (worldT > 1e-5f && worldT < best.t) {
                best.entity = e; best.worldPos = worldHit; best.t = worldT; best.tri = int(i / 3);
            }
        }
    }
    if (best.entity != entt::null) return best;
    return std::nullopt;
}

// GATE 3.1 -- raycast accuracy: pickMesh selects the analytically-correct body for >=99% of a grid of
// ground-truth rays over a wall of spheres (front-most on occlusion, miss in gaps). NEG-CTRL: the old
// ray-AABB-only pick is materially less accurate (it over-selects at the bounding-box corners), and a
// broken always-hit pick fails the miss cases. Gated by KRS_RAYCAST_SELFTEST + KRS_OVERNIGHT_BENCH.
bool runRaycastGate3_1();

// GATE F5 (KRS_DENSE_SELFTEST) -- dense-scene pick stress: >=20 bodies / >=100k triangles, batch of
// ground-truth rays, asserts >=99% correctness at scale and REPORTS picking latency (avg/max per pick).
bool runDenseSceneGateF5();

} // namespace krs::pick
