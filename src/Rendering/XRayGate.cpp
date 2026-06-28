// XRayGate.cpp -- Phase 6 GATE 6: occluded-body x-ray selection. A pick ray fired through N stacked
// coaxial bodies must surface EVERY body it pierces (krs::pick::pickMeshAll -- one hit per entity,
// sorted strictly near->far), and the cycled feature pick (krs::sel::pickCycled) must walk the bodies
// front -> ... -> back -> wrap-to-front as the cycle index advances. This is exactly what lets a click
// reach a bore OCCLUDED behind the component in front of it. NEG-CTRL: the plain nearest pick
// (krs::pick::pickMesh) can ONLY ever return the front-most body, so it can never select an occluded
// one -- proving the cycle is non-vacuous (without it, occluded selection is impossible).

#include "RayPick.hpp"
#include "SelectionService.hpp"
#include "components.hpp"

#include <entt/entt.hpp>
#include <glm/glm.hpp>

#include <cstdio>
#include <vector>

namespace krs::pick {
namespace {

// axis-aligned unit cube centred at the origin (12 triangles) -- a solid the ray pierces front+back.
void buildCube(float h, std::vector<Vertex>& verts, std::vector<unsigned int>& idx)
{
    const glm::vec3 p[8] = {
        {-h,-h,-h}, { h,-h,-h}, { h, h,-h}, {-h, h,-h},
        {-h,-h, h}, { h,-h, h}, { h, h, h}, {-h, h, h},
    };
    const int faces[6][4] = {
        {0,1,2,3}, {4,5,6,7}, {0,1,5,4}, {2,3,7,6}, {1,2,6,5}, {0,3,7,4},
    };
    for (const auto& f : faces) {
        const unsigned base = unsigned(verts.size());
        for (int k = 0; k < 4; ++k) { Vertex v; v.position = p[f[k]]; verts.push_back(v); }
        idx.insert(idx.end(), { base, base + 1, base + 2, base, base + 2, base + 3 });
    }
}

} // namespace

bool runXRayGate6()
{
    using std::printf;
    setvbuf(stdout, nullptr, _IONBF, 0);
    printf("[xray] GATE 6 -- occluded-body x-ray cycle (pickMeshAll surfaces all N; pickCycled walks depth)\n");

    entt::registry reg;
    std::vector<Vertex> cv; std::vector<unsigned int> ci; buildCube(0.4f, cv, ci);

    // N cubes stacked along -z at z = 0, -2, -4 (front->back as seen from the +z eye). All share x=y=0,
    // so ONE ray down -z pierces every one of them.
    const int N = 3;
    std::vector<entt::entity> order;                       // expected near->far entity order
    for (int i = 0; i < N; ++i) {
        const glm::vec3 c(0.0f, 0.0f, -2.0f * float(i));
        const entt::entity e = reg.create();
        reg.emplace<TransformComponent>(e, c, glm::quat(1, 0, 0, 0), glm::vec3(1.0f));
        auto& m = reg.emplace<RenderableMeshComponent>(e); m.vertices = cv; m.indices = ci;
        order.push_back(e);                                // i=0 is nearest the eye
    }

    Ray ray; ray.origin = glm::vec3(0.0f, 0.0f, 6.0f); ray.dir = glm::vec3(0.0f, 0.0f, -1.0f);

    // 1) pickMeshAll surfaces all N, one per entity, sorted strictly near->far in the expected order.
    const auto hits = pickMeshAll(reg, ray);
    bool allSurfaced = (int(hits.size()) == N);
    bool sorted = true, orderOk = true;
    for (int i = 0; i < int(hits.size()); ++i) {
        if (i > 0 && !(hits[i].t > hits[i - 1].t)) sorted = false;
        if (i < N && hits[i].entity != order[i]) orderOk = false;
    }
    printf("[xray]   pickMeshAll: %d/%d bodies surfaced, sorted near->far=%s, order matches stack=%s  %s\n",
           int(hits.size()), N, sorted ? "yes" : "NO", orderOk ? "yes" : "NO",
           (allSurfaced && sorted && orderOk) ? "PASS" : "FAIL");

    // 2) front-most (idx 0) matches the plain nearest pick -- no regression for a normal single click.
    const auto near = pickMesh(reg, ray);
    const bool frontMatches = near && !hits.empty() && near->entity == hits.front().entity;
    printf("[xray]   pickMeshAll[0] == pickMesh (nearest) : %s\n", frontMatches ? "PASS" : "FAIL");

    // 3) pickCycled walks front->mid->back then WRAPS to front (idx N == idx 0).
    int cnt = 0;
    bool cycleOk = true;
    for (int i = 0; i <= N; ++i) {                         // 0..N inclusive: N must wrap to 0
        const krs::sel::Selection s = krs::sel::pickCycled(reg, ray, i, &cnt);
        const entt::entity expect = order[i % N];
        if (cnt != N || s.entity != expect) cycleOk = false;
    }
    printf("[xray]   pickCycled walks depth front->back->wrap (hitCount=%d) : %s\n", cnt, cycleOk ? "PASS" : "FAIL");

    // 4) NEG-CTRL: the plain nearest pick can NEVER reach an occluded body -- it always returns the
    // front-most regardless of how many times it is called. So without the cycle, occluded selection
    // is impossible (the cycle is non-vacuous).
    bool negCtrlStuck = true;
    for (int i = 0; i < N; ++i) {
        const auto h = pickMesh(reg, ray);
        if (!h || h->entity != order.front()) { negCtrlStuck = false; break; }
    }
    const bool occludedReachable = cycleOk && N >= 2 &&
        krs::sel::pickCycled(reg, ray, 1).entity == order[1] &&
        krs::sel::pickCycled(reg, ray, 1).entity != order.front();
    printf("[xray]   NEG-CTRL: plain pickMesh stuck on front body=%s; cycle DOES reach occluded body=%s  %s\n",
           negCtrlStuck ? "yes" : "no", occludedReachable ? "yes" : "no",
           (negCtrlStuck && occludedReachable) ? "REJECTS(non-vacuous)" : "VACUOUS!");

    const bool pass = allSurfaced && sorted && orderOk && frontMatches && cycleOk &&
                      negCtrlStuck && occludedReachable;
    printf("[xray] %s\n", pass ? "ALL PASS (all bodies surfaced; cycle walks depth + wraps; occluded reachable)"
                               : "FAILURES PRESENT");
    std::fflush(stdout);
    return pass;
}

} // namespace krs::pick
