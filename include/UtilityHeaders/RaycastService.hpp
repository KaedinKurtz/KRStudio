#pragma once
// RaycastService -- nanort-backed BVH ray/mesh picking for mate-connector placement (Phase B).
// nanort (MIT, single header) is confined to RaycastService.cpp; this header exposes a plain API so no
// other TU pulls the kernel. Coordinates are whatever frame the verts are in (world-baked meshes -> world).
#include <glm/glm.hpp>
#include <vector>
#include <cstdint>

namespace krs::pick {

struct RayHit {
    bool      hit = false;
    int       tri = -1;             // triangle index: the hit face spans indices[3*tri .. 3*tri+2]
    float     t   = 0.0f;           // ray parameter at the hit (distance along a normalized dir)
    float     u   = 0.0f, v = 0.0f; // barycentric coords of the hit within the triangle
    glm::vec3 pos{ 0.0f };          // hit point = origin + normalize(dir)*t
};

// Nearest ray/triangle hit against a mesh (flat float xyz verts + uint triangle indices) via a nanort BVH.
// dir need not be normalized. Returns {hit=false} on miss / degenerate input.
RayHit raycastMesh(const std::vector<float>& verts, const std::vector<std::uint32_t>& indices,
                   const glm::vec3& origin, const glm::vec3& dir,
                   float tMin = 1e-5f, float tMax = 1e30f);

// Snap a point to the nearest of a set of candidate anchors (e.g. a bore's two rim centres axisEnd0/1),
// so a picked connector lands on a rim instead of mid-wall. Returns the index of the nearest, -1 if empty.
int nearestAnchor(const glm::vec3& p, const std::vector<glm::vec3>& candidates);

// GATE (KRS_PICK_SELFTEST): ray/mesh pick hits the right triangle+face, respects tMin/tMax, misses cleanly,
// handles oblique rays, and the rim-snap picks the nearest anchor. Non-vacuous negative controls. In .cpp.
bool runRaycastGate();

} // namespace krs::pick
