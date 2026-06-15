// RaycastGate.cpp -- Phase 3 GATE 3.1: scene-click raycast accuracy. The hardened pick (ray-AABB cull
// + exact ray-TRIANGLE, krs::pick::pickMesh) must select the ANALYTICALLY-correct body for >=99% of a
// grid of ground-truth rays cast over a wall of spheres (nearest-along-ray, miss in the gaps). The
// NEG-CTRL is the old AABB-only pick: it over-selects at the bounding-box corners (where the ray
// clips the box but misses the sphere), so it is materially less accurate -- proving the ray-triangle
// refinement is what earns the >=99%.

#include "RayPick.hpp"
#include "components.hpp"

#include <entt/entt.hpp>
#include <glm/glm.hpp>

#include <cstdio>
#include <optional>
#include <vector>

namespace krs::pick {
namespace {

// low-res UV sphere centred at the origin, radius r (mesh approximates the analytic sphere).
void buildSphere(float r, int seg, std::vector<Vertex>& verts, std::vector<unsigned int>& idx)
{
    for (int i = 0; i <= seg; ++i) {
        const float v = float(i) / seg, phi = v * 3.14159265f;            // 0..pi
        for (int j = 0; j <= seg; ++j) {
            const float u = float(j) / seg, theta = u * 6.2831853f;       // 0..2pi
            Vertex vt;
            vt.position = glm::vec3(r * std::sin(phi) * std::cos(theta),
                                    r * std::cos(phi),
                                    r * std::sin(phi) * std::sin(theta));
            verts.push_back(vt);
        }
    }
    const int stride = seg + 1;
    for (int i = 0; i < seg; ++i)
        for (int j = 0; j < seg; ++j) {
            const unsigned a = i * stride + j, b = a + 1, c = a + stride, d = c + 1;
            idx.insert(idx.end(), { a, b, c, b, d, c });
        }
}

// analytic nearest-sphere a ray hits (ground truth), or entt::null.
entt::entity analyticPick(const std::vector<std::pair<entt::entity, glm::vec3>>& centers, float r,
                          const Ray& ray)
{
    entt::entity best = entt::null; float bestT = 3.0e38f;
    for (const auto& [e, c] : centers) {
        const glm::vec3 oc = ray.origin - c;
        const float b = glm::dot(oc, ray.dir);
        const float cc = glm::dot(oc, oc) - r * r;
        const float disc = b * b - cc;
        if (disc < 0.0f) continue;
        const float t = -b - std::sqrt(disc);
        if (t > 1e-4f && t < bestT) { bestT = t; best = e; }
    }
    return best;
}

// the OLD pick: ray-AABB only (nearest box-entry). Reproduces the coarse behaviour for the neg-ctrl.
entt::entity aabbPick(entt::registry& reg, const Ray& ray)
{
    entt::entity best = entt::null; float bestT = 3.0e38f;
    for (auto e : reg.view<TransformComponent, RenderableMeshComponent>()) {
        const auto& xf = reg.get<TransformComponent>(e);
        const auto& mesh = reg.get<RenderableMeshComponent>(e);
        glm::vec3 lo(1e30f), hi(-1e30f);
        for (const auto& v : mesh.vertices) { lo = glm::min(lo, v.position); hi = glm::max(hi, v.position); }
        const glm::mat4 invM = glm::inverse(xf.getTransform());
        const glm::vec3 roL = glm::vec3(invM * glm::vec4(ray.origin, 1.0f));
        const glm::vec3 rdL = glm::normalize(glm::vec3(invM * glm::vec4(ray.dir, 0.0f)));
        float t0, t1;
        if (!rayAABB(roL, rdL, lo, hi, t0, t1)) continue;
        const float t = (t0 > 0.0f) ? t0 : t1;
        if (t > 1e-4f && t < bestT) { bestT = t; best = e; }
    }
    return best;
}

} // namespace

bool runRaycastGate3_1()
{
    using std::printf;
    setvbuf(stdout, nullptr, _IONBF, 0);
    printf("[raycast] GATE 3.1 -- ray-triangle pick accuracy (>=99%%) vs the coarse AABB pick (neg-ctrl)\n");

    entt::registry reg;
    const float R = 0.32f;
    std::vector<Vertex> sv; std::vector<unsigned int> si; buildSphere(R, 16, sv, si);
    std::vector<std::pair<entt::entity, glm::vec3>> centers;
    // a 3x3 wall of spheres at z=0, spacing 1.0, plus one OCCLUDER sphere in front (z=+2) of the centre.
    for (int gy = -1; gy <= 1; ++gy)
        for (int gx = -1; gx <= 1; ++gx) {
            const glm::vec3 c(gx * 1.0f, gy * 1.0f, 0.0f);
            const entt::entity e = reg.create();
            reg.emplace<TransformComponent>(e, c, glm::quat(1, 0, 0, 0), glm::vec3(1.0f));
            auto& m = reg.emplace<RenderableMeshComponent>(e); m.vertices = sv; m.indices = si;
            centers.push_back({ e, c });
        }
    { const glm::vec3 c(0.0f, 0.0f, 2.0f);  // occluder in front of the centre sphere (nearer the camera at +z)
      const entt::entity e = reg.create();
      reg.emplace<TransformComponent>(e, c, glm::quat(1, 0, 0, 0), glm::vec3(1.0f));
      auto& m = reg.emplace<RenderableMeshComponent>(e); m.vertices = sv; m.indices = si;
      centers.push_back({ e, c }); }

    // cast a dense grid of rays from z=+6 toward the wall; ground truth = analytic nearest sphere.
    const glm::vec3 eye(0.0f, 0.0f, 6.0f);
    int total = 0, triCorrect = 0, aabbCorrect = 0, missTotal = 0, missTriOk = 0;
    const int G = 24;            // 49x49 ~ 2400 rays
    for (int iy = -G; iy <= G; ++iy)
        for (int ix = -G; ix <= G; ++ix) {
            const glm::vec3 target(ix * (1.6f / G), iy * (1.6f / G), 0.0f);  // sweep across [-1.6,1.6]^2
            Ray ray; ray.origin = eye; ray.dir = glm::normalize(target - eye);
            const entt::entity truth = analyticPick(centers, R, ray);
            // skip the thin silhouette band where the mesh tessellation legitimately disagrees with
            // the analytic sphere (a ray grazing within ~1 mesh-facet of the limb).
            {
                bool grazing = false;
                for (const auto& [e, c] : centers) {
                    const glm::vec3 oc = ray.origin - c; const float b = glm::dot(oc, ray.dir);
                    const float d2 = glm::dot(oc, oc) - b * b;             // perpendicular dist^2 to centre line
                    const float d = std::sqrt(std::max(0.0f, d2));
                    if (std::abs(d - R) < 0.02f) { grazing = true; break; }
                }
                if (grazing) continue;
            }
            ++total;
            const auto hit = pickMesh(reg, ray);
            const entt::entity got = hit ? hit->entity : entt::null;
            if (got == truth) ++triCorrect;
            if (aabbPick(reg, ray) == truth) ++aabbCorrect;
            if (truth == entt::null) { ++missTotal; if (got == entt::null) ++missTriOk; }
        }

    const double triAcc = total ? double(triCorrect) / total : 0.0;
    const double aabbAcc = total ? double(aabbCorrect) / total : 0.0;
    const double missAcc = missTotal ? double(missTriOk) / missTotal : 1.0;
    const bool accurate = triAcc >= 0.99 && total > 200;
    const bool missClean = missAcc >= 0.99;                 // gaps return no pick
    const bool negCtrlWorse = aabbAcc < triAcc - 0.05;      // the coarse AABB pick is materially worse
    const bool pass = accurate && missClean && negCtrlWorse;

    printf("[raycast]   ray-triangle pick: %d/%d correct = %.2f%% (bound>=99%%, %d rays) ; miss(gap)=%.2f%%  %s\n",
           triCorrect, total, triAcc * 100.0, total, missAcc * 100.0, (accurate && missClean) ? "PASS" : "FAIL");
    printf("[raycast]   NEG-CTRL (coarse AABB-only pick): %.2f%% correct (must be <%.2f%% -- AABB over-selects)  %s\n",
           aabbAcc * 100.0, (triAcc - 0.05) * 100.0, negCtrlWorse ? "REJECTS(non-vacuous)" : "VACUOUS!");
    printf("[raycast] %s\n", pass ? "ALL PASS (ray-triangle >=99%; AABB-only materially worse; gaps clean)" : "FAILURES PRESENT");
    std::fflush(stdout);
    return pass;
}

} // namespace krs::pick
