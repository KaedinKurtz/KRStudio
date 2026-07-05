#pragma once
// ===========================================================================
// krs::sel EDGE selection -- resolve + pick TRUE B-Rep edges (BRepEdgeComponent).
//
// CadImporter stores each body's edges body-LOCAL (analytic circle/line params +
// a pick polyline -- see BRepEdge.hpp). This header derives the WORLD Selection
// from the entity's CURRENT TransformComponent (the resolveFace pattern), and
// picks edges by RAY-PROXIMITY: an edge is infinitely thin, so the pick is the
// minimum distance from the ray to the edge's polyline segments, accepted within
// a caller-scaled world tolerance -- never a surface intersection.
//
// Selection conventions (documented in SelectionService.hpp):
//   EdgeCircle -> axisPos = world centre, axisDir = plane normal, radius = world
//                 radius, axisEnd0/1 = arc endpoints (equal when closed).
//   EdgeLine   -> axisEnd0/1 = endpoints, axisDir = direction, axisPos = midpoint.
//   hitPoint = nearest point on the edge to the pick ray; Selection.faceKey
//   carries BRepEdge.edgeKey (same mate-anchor contract as faceKey).
//   Kind::Other edges resolve honestly as EdgeLine over their curve endpoints
//   (the polyline is still what the pick tests).
//
// Gate: runEdgeSelectGate() (env hook KRS_EDGE_SELFTEST, wired in the main
// loop) -- headless, pure CPU, synthetic entities, real NEG-CTRLs.
// ===========================================================================
#include <glm/glm.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <entt/entt.hpp>
#include <algorithm>
#include <cmath>

#include "components.hpp"        // TransformComponent, RenderableMeshComponent
#include "BRepEdge.hpp"          // BRepEdge / BRepEdgeComponent / computeEdgeKey
#include "SelectionService.hpp"  // Selection, FeatureType, pickPreferCylinder
#include "RayPick.hpp"           // krs::pick::Ray / rayAABB

namespace krs::sel {

// Resolve a KNOWN (entity, edgeId) into a Selection with world analytic params from the entity's
// CURRENT transform -- no ray needed (the resolveFace re-derivation primitive, for edges). Positions
// go through M; directions through the inverse-transpose; the circle radius is scaled by the MEAN of
// the two linear-map scale factors PERPENDICULAR to the axis (the same non-uniform-scale convention
// Measure.cpp uses for cylinder diameters). valid=false (never a crash) on an invalid entity, a
// missing BRepEdgeComponent, or an out-of-range edgeId.
inline Selection resolveEdge(entt::registry& reg, entt::entity e, int edgeId) {
    Selection s;
    s.entity = e;
    if (!reg.valid(e) || !reg.all_of<BRepEdgeComponent>(e)) return s;
    const auto& edges = reg.get<BRepEdgeComponent>(e).edges;
    if (edgeId < 0 || edgeId >= int(edges.size())) return s;
    const BRepEdge& be = edges[std::size_t(edgeId)];

    glm::mat4 M(1.0f);
    if (const auto* xf = reg.try_get<TransformComponent>(e)) M = xf->getTransform();
    const glm::mat3 R = glm::mat3(glm::inverseTranspose(M));   // normals/directions (resolveFace convention)
    const glm::mat3 L(M);                                      // linear part (rotation*scale) for the radius

    s.valid = true;
    s.edgeId = edgeId;
    s.faceKey = be.edgeKey;                                    // edgeKey rides the faceKey anchor channel
    const glm::vec3 dLocal = (glm::dot(be.axisDir, be.axisDir) > 1e-12f)
                             ? glm::normalize(be.axisDir) : glm::vec3(0.0f, 0.0f, 1.0f);
    if (be.kind == BRepEdge::Circle) {
        s.type = FeatureType::EdgeCircle;
        s.axisPos = glm::vec3(M * glm::vec4(be.center, 1.0f)); // world centre
        s.axisDir = glm::normalize(R * dLocal);                // circle plane normal
        s.normal  = s.axisDir;
        // world radius = local radius x mean of the two perpendicular scale factors (Measure.cpp).
        glm::vec3 u = glm::cross(dLocal, glm::vec3(0, 0, 1));
        if (glm::dot(u, u) < 1e-8f) u = glm::cross(dLocal, glm::vec3(0, 1, 0));
        u = glm::normalize(u);
        const glm::vec3 v = glm::normalize(glm::cross(dLocal, u));
        s.radius = be.radius * 0.5f * (glm::length(L * u) + glm::length(L * v));
        s.axisEnd0 = glm::vec3(M * glm::vec4(be.p0, 1.0f));    // arc endpoints (equal when closed)
        s.axisEnd1 = glm::vec3(M * glm::vec4(be.p1, 1.0f));
        s.hitPoint = s.axisPos;                                // rayless resolve anchors at the centre
    } else {                                                   // Line (and Other: endpoints honestly)
        s.type = FeatureType::EdgeLine;
        s.axisEnd0 = glm::vec3(M * glm::vec4(be.p0, 1.0f));
        s.axisEnd1 = glm::vec3(M * glm::vec4(be.p1, 1.0f));
        const glm::vec3 w = s.axisEnd1 - s.axisEnd0;
        s.axisDir = (glm::dot(w, w) > 1e-16f) ? glm::normalize(w)   // true world dir (non-uniform-scale safe)
                                              : glm::normalize(R * dLocal);
        s.normal  = s.axisDir;
        s.axisPos = 0.5f * (s.axisEnd0 + s.axisEnd1);          // midpoint
        s.radius  = 0.0f;
        s.hitPoint = s.axisPos;                                // rayless resolve anchors at the midpoint
    }
    return s;
}

// Closest approach between a pick RAY (treated as an infinite line, then REJECTED when the closest
// point lies behind the origin) and one polyline SEGMENT a..b, all WORLD space. rd must be unit.
// Returns false when the closest ray point is behind the origin; else outputs the ray param, the
// closest point ON THE SEGMENT, and their distance.
inline bool raySegmentClosest(const glm::vec3& ro, const glm::vec3& rd,
                              const glm::vec3& a, const glm::vec3& b,
                              float& tRay, glm::vec3& onSeg, float& dist) {
    const glm::vec3 e = b - a, w0 = ro - a;
    const float B = glm::dot(rd, e), C = glm::dot(e, e);
    const float D = glm::dot(rd, w0), E = glm::dot(e, w0);
    float u;
    const float denom = C - B * B;                           // (rd unit) Gram determinant
    if (C < 1e-16f) u = 0.0f;                                // degenerate segment (a point)
    else if (denom > 1e-12f) {
        const float t = (B * E - D * C) / denom;             // unclamped line-line solution
        u = std::clamp((E + t * B) / C, 0.0f, 1.0f);         // clamp to the SEGMENT only
    } else u = std::clamp(E / C, 0.0f, 1.0f);                // parallel: nearest projection of the origin
    tRay = u * B - D;                                        // optimal ray param for the clamped u
    if (tRay < 0.0f) return false;                           // behind the ray origin -> no pick
    onSeg = a + u * e;
    dist = glm::length(ro + tRay * rd - onSeg);
    return true;
}

// EDGE pick: over every entity holding TransformComponent + BRepEdgeComponent, the minimum WORLD
// distance from the ray to any edge-polyline segment, accepted iff <= worldTol (the caller scales
// worldTol, e.g. by pick depth / pixel size). Distance ties (within 1e-6) break to the NEARER
// ray-t, so overlapping-in-screen-space edges resolve to the closest one along the ray. The AABB
// pre-cull mirrors pickPreferCylinder when the entity has a mesh (padded by the tolerance so a
// near-miss ray still reaches the segment test). Returns resolveEdge of the winner with hitPoint =
// the nearest point ON the edge; an invalid Selection on a miss.
inline Selection pickEdge(entt::registry& reg, const krs::pick::Ray& ray, float worldTol) {
    const float dirLen2 = glm::dot(ray.dir, ray.dir);
    if (dirLen2 < 1e-16f || !(worldTol >= 0.0f)) return Selection{};
    const glm::vec3 rd = ray.dir / std::sqrt(dirLen2);

    entt::entity bestE = entt::null;
    int bestId = -1;
    float bestD = 3.0e38f, bestT = 3.0e38f;
    glm::vec3 bestPt(0.0f);

    for (auto e : reg.view<TransformComponent, BRepEdgeComponent>()) {
        const auto& ec = reg.get<BRepEdgeComponent>(e);
        if (ec.edges.empty()) continue;
        const glm::mat4 M = reg.get<TransformComponent>(e).getTransform();
        // AABB pre-cull (mirror pickPreferCylinder): transform the ray to LOCAL and slab-test the
        // mesh box when one exists. Edges lie ON the surface, so the box is padded by the pick
        // tolerance mapped into local units (conservatively, via the largest inverse-scale column).
        if (const auto* mesh = reg.try_get<RenderableMeshComponent>(e)) {
            if (mesh->aabbMin != mesh->aabbMax) {
                const glm::mat4 invM = glm::inverse(M);
                const glm::vec3 roL = glm::vec3(invM * glm::vec4(ray.origin, 1.0f));
                const glm::vec3 rdL = glm::normalize(glm::vec3(invM * glm::vec4(ray.dir, 0.0f)));
                const glm::mat3 invL(invM);
                const float invScale = std::max(std::max(glm::length(invL[0]), glm::length(invL[1])),
                                                glm::length(invL[2]));
                const float pad = worldTol * invScale * 2.0f + 1e-4f;
                float tE, tX;
                if (!krs::pick::rayAABB(roL, rdL, mesh->aabbMin - pad, mesh->aabbMax + pad, tE, tX))
                    continue;
            }
        }
        for (int ei = 0; ei < int(ec.edges.size()); ++ei) {
            const auto& pl = ec.edges[std::size_t(ei)].polyline;
            if (pl.size() < 2) continue;
            glm::vec3 prev = glm::vec3(M * glm::vec4(pl[0], 1.0f));
            for (std::size_t k = 1; k < pl.size(); ++k) {
                const glm::vec3 cur = glm::vec3(M * glm::vec4(pl[k], 1.0f));
                float t, d; glm::vec3 q;
                if (raySegmentClosest(ray.origin, rd, prev, cur, t, q, d)) {
                    // strictly closer wins; a distance TIE breaks to the nearer ray-t
                    if (d < bestD - 1e-6f || (d < bestD + 1e-6f && t < bestT)) {
                        bestD = std::min(bestD, d); bestT = t; bestPt = q;
                        bestE = e; bestId = ei;
                    }
                }
                prev = cur;
            }
        }
    }
    if (bestId < 0 || bestD > worldTol) return Selection{};    // nothing within tolerance -> miss
    Selection s = resolveEdge(reg, bestE, bestId);
    if (s.valid) s.hitPoint = bestPt;                          // nearest point ON the edge (world)
    return s;
}

// EDGE-PREFERRED pick (the measure/constraint workflow aims at rims and seams): the nearest edge
// within worldTol wins; when NO edge is in reach, fall back to the surface-feature pick
// (pickPreferCylinder -- bores win over the flat face around them, then the plain nearest face).
inline Selection pickPreferEdge(entt::registry& reg, const krs::pick::Ray& ray, float worldTol) {
    Selection s = pickEdge(reg, ray, worldTol);
    if (s.valid) return s;
    return pickPreferCylinder(reg, ray);
}

// Headless self-test (env KRS_EDGE_SELFTEST, defined in src/Physics/EdgeSelectGate.cpp):
// world-exact circle resolve under scale+translation, 1 mm rim pick + 10 cm miss, line endpoints
// under transform, nearer-edge depth tie-break, edgeKey stability/position-channel/never-zero,
// NEG-CTRLs (bad edgeId, no BRepEdgeComponent, ray behind origin).
bool runEdgeSelectGate();

} // namespace krs::sel
