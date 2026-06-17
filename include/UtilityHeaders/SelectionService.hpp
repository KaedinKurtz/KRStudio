#pragma once
// ===========================================================================
// OMPL sprint, Phase 3 — sub-feature SELECTION BACKEND (krs::sel).
//
// A ray resolves to a SPECIFIC B-Rep face and its EXACT analytic parameters,
// read from the OCCT-derived BRepFaceComponent (NOT a mesh/RANSAC fit), plus a
// selection-indicator GEOMETRY computed as DATA (a ring on a cylinder cross-
// section, a square outline on a plane) — rendering is DEFERRED to a supervised
// UI session, this is the gateable backend only.
//
// The backend is OCCT-free: it reads the ECS components that CadImporter (OCCT)
// populated (RenderableMeshComponent.triFace -> BRepFaceComponent.faces). The
// production pick (krs::pick::pickMesh) is reused, so the gate exercises the
// real selection path. Analytic params are transformed to WORLD via the entity
// TransformComponent (reduces to identity for an origin-placed part).
// ===========================================================================
#include <glm/glm.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <entt/entt.hpp>
#include <vector>
#include <cmath>

#include "components.hpp"     // TransformComponent, RenderableMeshComponent, BRepFace(Component)
#include "RayPick.hpp"        // krs::pick::Ray / pickMesh

namespace krs::sel {

enum class FeatureType { None = -1, Plane = 0, Cylinder = 1, Cone = 2, Sphere = 3, Other = 4 };

struct Selection {
    bool valid = false;                    // a B-Rep face was resolved
    entt::entity entity = entt::null;
    int faceId = -1;
    FeatureType type = FeatureType::None;
    glm::vec3 hitPoint{ 0.0f };            // ray-surface intersection (world)
    // analytic feature params in WORLD frame (from the B-Rep, never a mesh fit):
    glm::vec3 axisPos{ 0.0f };             // a point on the axis (cyl/cone) or centre (sphere)
    glm::vec3 axisDir{ 0.0f, 0.0f, 1.0f }; // unit axis direction (cyl/cone)
    glm::vec3 normal{ 0.0f, 0.0f, 1.0f };  // surface normal (plane)
    float radius = 0.0f;                   // metres
};

// Resolve a world-space ray to the specific B-Rep face it hits + its exact
// analytic parameters. Returns an invalid Selection on a miss or a non-B-Rep
// body (e.g. a raw mesh with no BRepFaceComponent).
inline Selection pick(entt::registry& reg, const krs::pick::Ray& ray) {
    Selection s;
    const auto hit = krs::pick::pickMesh(reg, ray);
    if (!hit) return s;                                  // miss -> no selection
    s.entity = hit->entity;
    s.hitPoint = hit->worldPos;
    if (!reg.all_of<RenderableMeshComponent, BRepFaceComponent>(hit->entity)) return s;
    const auto& mesh = reg.get<RenderableMeshComponent>(hit->entity);
    const auto& brep = reg.get<BRepFaceComponent>(hit->entity);
    if (hit->tri < 0 || hit->tri >= int(mesh.triFace.size())) return s;
    const int fid = mesh.triFace[hit->tri];
    if (fid < 0 || fid >= int(brep.faces.size())) return s;
    const BRepFace& bf = brep.faces[fid];

    glm::mat4 M(1.0f);
    if (const auto* xf = reg.try_get<TransformComponent>(hit->entity)) M = xf->getTransform();
    const glm::mat3 R = glm::mat3(glm::inverseTranspose(M));   // normals/directions

    s.valid = true;
    s.faceId = fid;
    s.type = static_cast<FeatureType>(bf.type);
    s.radius = bf.radius;                                 // scale-free B-Rep radius (part placed unit-scale)
    s.axisPos = glm::vec3(M * glm::vec4(bf.axisPos, 1.0f));
    s.axisDir = glm::normalize(R * bf.axisDir);
    s.normal = glm::normalize(R * bf.normal);
    return s;
}

// Selection-indicator GEOMETRY as DATA (no rendering). For a cylinder/cone the
// indicator is a ring on the cross-section through the hit point (centre =
// projection of the hit onto the axis line); for a plane it is a square outline
// on the surface around the hit; otherwise a single point at the hit.
struct IndicatorGeometry {
    FeatureType type = FeatureType::None;
    glm::vec3 center{ 0.0f };
    glm::vec3 normal{ 0.0f, 0.0f, 1.0f };  // ring plane normal (cyl axis) or surface normal (plane)
    float radius = 0.0f;
    std::vector<glm::vec3> points;         // world-space ring / outline vertices
};

inline IndicatorGeometry indicator(const Selection& sel, int segments = 32, float planeHalf = 0.01f) {
    IndicatorGeometry g;
    if (!sel.valid) return g;
    g.type = sel.type;
    if (sel.type == FeatureType::Cylinder || sel.type == FeatureType::Cone) {
        // centre = projection of the hit point onto the axis line.
        const glm::vec3 d = sel.axisDir;
        g.center = sel.axisPos + glm::dot(sel.hitPoint - sel.axisPos, d) * d;
        g.normal = d;
        g.radius = sel.radius;
        // an orthonormal basis (u,v) spanning the cross-section plane.
        glm::vec3 u = glm::cross(d, glm::vec3(0, 0, 1));
        if (glm::dot(u, u) < 1e-8f) u = glm::cross(d, glm::vec3(0, 1, 0));
        u = glm::normalize(u);
        const glm::vec3 v = glm::normalize(glm::cross(d, u));
        for (int i = 0; i < segments; ++i) {
            const float a = 6.2831853f * float(i) / float(segments);
            g.points.push_back(g.center + sel.radius * (std::cos(a) * u + std::sin(a) * v));
        }
    } else if (sel.type == FeatureType::Plane) {
        g.center = sel.hitPoint;
        g.normal = sel.normal;
        glm::vec3 u = glm::cross(sel.normal, glm::vec3(0, 0, 1));
        if (glm::dot(u, u) < 1e-8f) u = glm::cross(sel.normal, glm::vec3(0, 1, 0));
        u = glm::normalize(u);
        const glm::vec3 v = glm::normalize(glm::cross(sel.normal, u));
        const glm::vec3 corners[4] = { u + v, -u + v, -u - v, u - v };
        for (const auto& c : corners) g.points.push_back(g.center + planeHalf * c);
    } else {
        g.center = sel.hitPoint;
        g.points.push_back(sel.hitPoint);
    }
    return g;
}

} // namespace krs::sel
