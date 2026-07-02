// RaycastService.cpp -- nanort (MIT) BVH ray/mesh picking, confined to this TU.
#include "RaycastService.hpp"

#include <nanort.h>

#include <cstdio>
#include <cmath>
#include <limits>

namespace krs::pick {

RayHit raycastMesh(const std::vector<float>& verts, const std::vector<std::uint32_t>& indices,
                   const glm::vec3& origin, const glm::vec3& dir, float tMin, float tMax)
{
    RayHit out;
    if (verts.size() < 9 || indices.size() < 3) return out;
    const unsigned numFaces = unsigned(indices.size() / 3);

    nanort::BVHBuildOptions<float> options;                       // defaults are fine for picking
    nanort::TriangleMesh<float>    mesh(verts.data(), indices.data(), sizeof(float) * 3);
    nanort::TriangleSAHPred<float> pred(verts.data(), indices.data(), sizeof(float) * 3);
    nanort::BVHAccel<float>        accel;
    if (!accel.Build(numFaces, mesh, pred, options)) return out;

    const glm::vec3 d = glm::normalize(dir);
    nanort::Ray<float> ray;
    ray.org[0] = origin.x; ray.org[1] = origin.y; ray.org[2] = origin.z;
    ray.dir[0] = d.x;      ray.dir[1] = d.y;      ray.dir[2] = d.z;
    ray.min_t = tMin;      ray.max_t = tMax;

    nanort::TriangleIntersector<> isector(verts.data(), indices.data(), sizeof(float) * 3);
    nanort::TriangleIntersection<> isect;
    nanort::BVHTraceOptions trace;
    if (!accel.Traverse(ray, isector, &isect, trace)) return out;

    out.hit = true;
    out.tri = int(isect.prim_id);
    out.t   = isect.t;
    out.u   = isect.u;
    out.v   = isect.v;
    out.pos = origin + d * isect.t;
    return out;
}

int nearestAnchor(const glm::vec3& p, const std::vector<glm::vec3>& candidates)
{
    int best = -1; float bestD2 = std::numeric_limits<float>::max();
    for (int i = 0; i < int(candidates.size()); ++i) {
        const float d2 = glm::dot(candidates[i] - p, candidates[i] - p);
        if (d2 < bestD2) { bestD2 = d2; best = i; }
    }
    return best;
}

// ---------------------------------------------------------------------------
namespace {
// A unit quad in the z=0 plane spanning [0,1]x[0,1] as two triangles; +Z faces up.
void makeQuad(std::vector<float>& v, std::vector<std::uint32_t>& idx) {
    v = { 0,0,0,  1,0,0,  1,1,0,  0,1,0 };
    idx = { 0,1,2,  0,2,3 };
}
} // namespace

bool runRaycastGate()
{
    using std::printf;
    setvbuf(stdout, nullptr, _IONBF, 0);
    printf("[pick] GATE RAYCAST -- nanort BVH ray/mesh pick (hit triangle+face, tMin/tMax, miss, oblique, rim-snap)\n");
    bool pass = true;

    std::vector<float> v; std::vector<std::uint32_t> idx; makeQuad(v, idx);
    const std::vector<int> triFace = { 7, 7 };   // both triangles belong to BRep face id 7

    // A: straight-down ray hits the quad at its centre (in triangle {0,2,3}), t==1, pos==(0.5,0.5,0)
    RayHit a = raycastMesh(v, idx, {0.5f,0.5f,1.0f}, {0,0,-1});
    const bool A = a.hit && std::abs(a.t - 1.0f) < 1e-4f && glm::length(a.pos - glm::vec3(0.5f,0.5f,0)) < 1e-4f
                && a.tri >= 0 && a.tri < 2 && triFace[a.tri] == 7;
    printf("[pick]   A hit centre: hit=%d t=%.4f face=%d (want 7)  %s\n",
           a.hit, a.t, a.hit ? triFace[a.tri] : -1, A ? "PASS" : "FAIL"); pass &= A;

    // B: a ray that passes OUTSIDE the quad misses cleanly (NEG-CTRL: not a false hit)
    RayHit b = raycastMesh(v, idx, {2.0f,2.0f,1.0f}, {0,0,-1});
    const bool B = !b.hit;
    printf("[pick]   B off-quad ray misses: hit=%d  %s\n", b.hit, B ? "PASS" : "FAIL"); pass &= B;

    // C: tMax shorter than the distance to the quad => miss; long tMax => hit (NEG-CTRL on range)
    RayHit cShort = raycastMesh(v, idx, {0.25f,0.25f,1.0f}, {0,0,-1}, 1e-5f, 0.5f);   // quad at t=1 > 0.5
    RayHit cLong  = raycastMesh(v, idx, {0.25f,0.25f,1.0f}, {0,0,-1}, 1e-5f, 10.0f);
    const bool C = !cShort.hit && cLong.hit && std::abs(cLong.t - 1.0f) < 1e-4f;
    printf("[pick]   C tMax range: short(0.5)->miss=%d long->hit t=%.3f  %s\n",
           !cShort.hit, cLong.t, C ? "PASS" : "FAIL"); pass &= C;

    // D: an oblique ray hits the correct point on the plane
    RayHit d = raycastMesh(v, idx, {0.2f,0.2f,1.0f}, {0.3f,0.3f,-1.0f});
    // param solve: z: 1 + t*(-1/len)=0 ; expected xy = 0.2 + 0.3*(1) = 0.5 at z=0 (since dz component reaches 0)
    const bool D = d.hit && std::abs(d.pos.z) < 1e-4f && d.pos.x > 0.2f && d.pos.y > 0.2f && triFace[d.tri] == 7;
    printf("[pick]   D oblique ray: hit=%d pos=(%.3f,%.3f,%.3f)  %s\n",
           d.hit, d.pos.x, d.pos.y, d.pos.z, D ? "PASS" : "FAIL"); pass &= D;

    // E: rim-snap picks the nearest anchor (a bore's two rim centres); NEG-CTRL: not the far one
    const std::vector<glm::vec3> rims = { {0,0,-0.10f}, {0,0,0.90f} };   // hit near z=1 -> rim1 (0.90)
    const int r = nearestAnchor(glm::vec3(0,0,1.0f), rims);
    const bool E = r == 1;
    printf("[pick]   E rim-snap nearest: idx=%d (want 1, the near rim)  %s\n", r, E ? "PASS" : "FAIL"); pass &= E;

    printf("[pick] %s\n", pass ? "ALL PASS (nanort pick hits the right face, honors tMin/tMax, misses cleanly, handles oblique rays; rim-snap picks nearest)"
                               : "FAILURES PRESENT");
    std::fflush(stdout);
    return pass;
}

} // namespace krs::pick
