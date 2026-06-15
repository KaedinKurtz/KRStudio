// DenseSceneGate.cpp -- Phase 3 GATE F5: dense-scene pick stress. Builds >=20 bodies / >=100k triangles
// and fires a large batch of ground-truth rays through the PRODUCTION pick (krs::pick::pickMesh), then
// REPORTS picking latency (avg + max + total per pick) while asserting correctness is maintained at
// scale. pickMesh is brute-force O(all triangles) (no BVH), so the reported latency honestly
// characterises the current pick at scale. NEG-CTRL: accuracy must hold at >=99% -- a pick that
// silently degraded (returned null/garbage) under the triangle load would fail the accuracy bound.

#include "RayPick.hpp"
#include "components.hpp"

#include <entt/entt.hpp>
#include <glm/glm.hpp>

#include <chrono>
#include <cstdio>
#include <vector>
#include <algorithm>

namespace krs::pick {
namespace {

void buildSphereDense(float r, int seg, std::vector<Vertex>& verts, std::vector<unsigned int>& idx)
{
    for (int i = 0; i <= seg; ++i) {
        const float v = float(i) / seg, phi = v * 3.14159265f;
        for (int j = 0; j <= seg; ++j) {
            const float u = float(j) / seg, theta = u * 6.2831853f;
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

entt::entity analyticNearest(const std::vector<std::pair<entt::entity, glm::vec3>>& centers, float r,
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

} // namespace

bool runDenseSceneGateF5()
{
    using std::printf;
    using clk = std::chrono::high_resolution_clock;
    setvbuf(stdout, nullptr, _IONBF, 0);
    printf("[dense] GATE F5 -- dense-scene pick stress (>=20 bodies / >=100k tris) + picking latency\n");

    entt::registry reg;
    const float R = 0.30f;
    const int seg = 48;                                   // ~4608 tris/sphere
    std::vector<Vertex> sv; std::vector<unsigned int> si; buildSphereDense(R, seg, sv, si);
    const long trisPerBody = long(si.size() / 3);

    std::vector<std::pair<entt::entity, glm::vec3>> centers;
    const int NX = 5, NY = 5;                             // 25 bodies (>=20)
    for (int gy = 0; gy < NY; ++gy)
        for (int gx = 0; gx < NX; ++gx) {
            const glm::vec3 c((gx - 2) * 0.8f, (gy - 2) * 0.8f, (gx % 2 ? 0.3f : -0.3f)); // staggered z -> occlusion
            const entt::entity e = reg.create();
            reg.emplace<TransformComponent>(e, c, glm::quat(1, 0, 0, 0), glm::vec3(1.0f));
            auto& m = reg.emplace<RenderableMeshComponent>(e); m.vertices = sv; m.indices = si;
            centers.push_back({ e, c });
        }
    const int bodies = NX * NY;
    const long totalTris = long(bodies) * trisPerBody;

    const glm::vec3 eye(0.0f, 0.0f, 6.0f);
    int total = 0, correct = 0;
    double sumMs = 0.0, maxMs = 0.0;
    const int G = 25;                                     // (2G+1)^2 = 2601 rays
    for (int iy = -G; iy <= G; ++iy)
        for (int ix = -G; ix <= G; ++ix) {
            const glm::vec3 target(ix * (2.2f / G), iy * (2.2f / G), 0.0f);
            Ray ray; ray.origin = eye; ray.dir = glm::normalize(target - eye);
            const entt::entity truth = analyticNearest(centers, R, ray);
            // skip the silhouette band where mesh tessellation legitimately disagrees with the analytic sphere
            bool grazing = false;
            for (const auto& [e, c] : centers) {
                const glm::vec3 oc = ray.origin - c; const float b = glm::dot(oc, ray.dir);
                const float d = std::sqrt(std::max(0.0f, glm::dot(oc, oc) - b * b));
                if (std::abs(d - R) < 0.02f) { grazing = true; break; }
            }
            if (grazing) continue;
            ++total;
            const auto t0 = clk::now();
            const auto hit = pickMesh(reg, ray);
            const auto t1 = clk::now();
            const double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
            sumMs += ms; maxMs = std::max(maxMs, ms);
            const entt::entity got = hit ? hit->entity : entt::null;
            if (got == truth) ++correct;
        }

    const double acc = total ? double(correct) / total : 0.0;
    const double avgMs = total ? sumMs / total : 0.0;
    const bool scaleOk = bodies >= 20 && totalTris >= 100000;
    const bool accurate = acc >= 0.99 && total > 1000;
    const bool latencyOk = avgMs < 50.0;                  // generous: catches a pathological O(n^2) regression
    const bool pass = scaleOk && accurate && latencyOk;

    printf("[dense]   scale: %d bodies, %ld triangles (>=20 / >=100k)  %s\n",
           bodies, totalTris, scaleOk ? "PASS" : "FAIL");
    printf("[dense]   accuracy under load: %d/%d = %.2f%% (>=99%%, %d rays)  %s\n",
           correct, total, acc * 100.0, total, accurate ? "PASS" : "FAIL");
    printf("[dense]   picking latency: avg=%.3f ms, max=%.3f ms per pick (brute O(tris), no BVH)  %s\n",
           avgMs, maxMs, latencyOk ? "PASS" : "FAIL");
    printf("[dense] %s\n", pass ? "ALL PASS (>=20 bodies / >=100k tris; >=99% correct at scale; latency reported)"
                                : "FAILURES PRESENT");
    std::fflush(stdout);
    return pass;
}

} // namespace krs::pick
