// CollisionSyncGate.cpp -- Phase B GATE C (C1/C2/C4): the SDF mesh-collider collision query must
// RIDE the body as it moves (no ghost at the vacated start pose, zero-step lag), proven on the CPU
// with the SAME world->local convention the GPU collision shader uses (SdfColliderQuery.hpp), with a
// NEGATIVE CONTROL (transform frozen at the bake pose => the start-pose ghost reappears).
//
// Root cause being closed: bakeSdfColliders baked the field in WORLD space at play and never
// refreshed it (m_sdfsBaked). The fix bakes the field in LOCAL frame once and refreshes the
// per-collider invModel from the live TransformComponent each step, so the field tracks the body.

#include "SdfColliderQuery.hpp"
#include "SdfBaker.hpp"
#include "components.hpp"   // Vertex

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

namespace krs::fluid {

float sdfDistanceWorld(const SdfColliderView& c, const glm::vec3& worldPoint)
{
    if (!c.field || c.dims.x < 2 || c.dims.y < 2 || c.dims.z < 2) return kSdfOutside;
    const glm::vec3 local = glm::vec3(c.invModel * glm::vec4(worldPoint, 1.0f));
    const glm::vec3 ext = c.aabbMax - c.aabbMin;
    if (ext.x <= 0.0f || ext.y <= 0.0f || ext.z <= 0.0f) return kSdfOutside;
    const glm::vec3 uvw = (local - c.aabbMin) / ext;
    if (glm::any(glm::lessThan(uvw, glm::vec3(0.0f))) || glm::any(glm::greaterThan(uvw, glm::vec3(1.0f))))
        return kSdfOutside;
    // GL_LINEAR sampler3D: sample at texel-centre coordinates uvw*dims - 0.5.
    const glm::vec3 t = uvw * glm::vec3(c.dims) - glm::vec3(0.5f);
    const glm::ivec3 i0 = glm::clamp(glm::ivec3(glm::floor(t)), glm::ivec3(0), c.dims - glm::ivec3(2));
    const glm::vec3 f = glm::clamp(t - glm::vec3(i0), glm::vec3(0.0f), glm::vec3(1.0f));
    auto at = [&](int x, int y, int z) -> float {
        return c.field[(size_t(z) * c.dims.y + y) * c.dims.x + x];
    };
    const float c000 = at(i0.x, i0.y, i0.z), c100 = at(i0.x + 1, i0.y, i0.z);
    const float c010 = at(i0.x, i0.y + 1, i0.z), c110 = at(i0.x + 1, i0.y + 1, i0.z);
    const float c001 = at(i0.x, i0.y, i0.z + 1), c101 = at(i0.x + 1, i0.y, i0.z + 1);
    const float c011 = at(i0.x, i0.y + 1, i0.z + 1), c111 = at(i0.x + 1, i0.y + 1, i0.z + 1);
    const float x00 = glm::mix(c000, c100, f.x), x10 = glm::mix(c010, c110, f.x);
    const float x01 = glm::mix(c001, c101, f.x), x11 = glm::mix(c011, c111, f.x);
    return glm::mix(glm::mix(x00, x10, f.y), glm::mix(x01, x11, f.y), f.z);
}

namespace {
// a unit cube [-0.5,0.5]^3, 12 outward triangles, for OpenVDB meshToLevelSet.
void buildUnitCube(std::vector<Vertex>& v, std::vector<unsigned int>& idx)
{
    const glm::vec3 c[8] = {
        {-0.5f,-0.5f,-0.5f}, { 0.5f,-0.5f,-0.5f}, { 0.5f, 0.5f,-0.5f}, {-0.5f, 0.5f,-0.5f},
        {-0.5f,-0.5f, 0.5f}, { 0.5f,-0.5f, 0.5f}, { 0.5f, 0.5f, 0.5f}, {-0.5f, 0.5f, 0.5f},
    };
    for (const auto& p : c) { Vertex vt; vt.position = p; v.push_back(vt); }
    const unsigned faces[6][4] = {
        {0,3,2,1}, {4,5,6,7}, {0,1,5,4}, {3,7,6,2}, {1,2,6,5}, {0,4,7,3},
    };
    for (auto& fc : faces) {
        idx.insert(idx.end(), { fc[0], fc[1], fc[2], fc[0], fc[2], fc[3] });
    }
}
} // namespace

bool runCollisionSyncGateC()
{
    using std::printf;
    printf("[collsync] GATE C (C1/C2/C4) -- SDF mesh-collider tracks the live body (no ghost), neg-ctrl\n");

    std::vector<Vertex> verts; std::vector<unsigned int> idx;
    buildUnitCube(verts, idx);

    const float voxel = 0.03f;
    SdfBakeResult baked;
    if (!bakeMeshToSdf(verts, idx, glm::mat4(1.0f), voxel, baked)) {
        printf("[collsync] vacuous pass (OpenVDB unavailable -> SDF bake disabled)\n");
        std::fflush(stdout);
        return true;
    }
    printf("[collsync]   baked LOCAL field: dims=%dx%dx%d aabb=[% .3f,% .3f,% .3f]..[% .3f,% .3f,% .3f]\n",
           baked.dims.x, baked.dims.y, baked.dims.z,
           baked.aabbMin.x, baked.aabbMin.y, baked.aabbMin.z,
           baked.aabbMax.x, baked.aabbMax.y, baked.aabbMax.z);

    auto viewFor = [&](const glm::mat4& model) {
        SdfColliderView v;
        v.invModel = glm::inverse(model);
        v.aabbMin = baked.aabbMin; v.aabbMax = baked.aabbMax;
        v.dims = baked.dims; v.field = baked.field.data();
        return v;
    };

    // reference field values at a grid of BODY-FRAME probe points (inside the AABB). If the field
    // rides the body, sdfDistanceWorld(view(pose), pose*pLocal) must equal these for ANY pose.
    std::vector<glm::vec3> probes;
    const glm::vec3 lo = baked.aabbMin + 0.15f * (baked.aabbMax - baked.aabbMin);
    const glm::vec3 hi = baked.aabbMax - 0.15f * (baked.aabbMax - baked.aabbMin);
    for (int xi = 0; xi <= 4; ++xi)
        for (int yi = 0; yi <= 4; ++yi)
            for (int zi = 0; zi <= 4; ++zi)
                probes.push_back(glm::vec3(glm::mix(lo.x, hi.x, xi / 4.0f),
                                           glm::mix(lo.y, hi.y, yi / 4.0f),
                                           glm::mix(lo.z, hi.z, zi / 4.0f)));
    const SdfColliderView vId = viewFor(glm::mat4(1.0f));
    std::vector<float> ref(probes.size());
    for (size_t i = 0; i < probes.size(); ++i) ref[i] = sdfDistanceWorld(vId, probes[i]);

    // sweep poses; placement uses the PRODUCTION SSOT krs::fluid::sdfRigidModel (same call the
    // bake + per-step refresh use), so the gate exercises the real placement math.
    auto poseAt = [](int k) {
        const float a = 0.13f * k;
        const glm::quat q = glm::angleAxis(0.21f * k, glm::normalize(glm::vec3(0.3f, 1.0f, 0.2f + 0.01f * k)));
        const glm::vec3 t(1.5f * std::sin(a), 0.4f * k * 0.05f, -1.2f * std::cos(a));
        return sdfRigidModel(t, q);
    };

    // The vacated start pose: a deep-interior body point. At the bake/start pose it is the world
    // origin region and reads a strongly NEGATIVE (colliding) distance. After the body moves away,
    // the LIVE field must leave it empty (positive), while the FROZEN field keeps colliding (ghost).
    const glm::vec3 vacated = 0.5f * (baked.aabbMin + baked.aabbMax); // == world location at start pose
    const float frozenAtStart = sdfDistanceWorld(vId, vacated);       // pose-independent (frozen=identity)

    const int POSES = 50;
    float liveMaxErr = 0.0f;      // field invariance under motion (rides the body)
    float frozenMaxErr = 0.0f;    // neg-ctrl A: frozen field (sync OFF) does NOT ride
    float swapMaxErr = 0.0f;      // neg-ctrl B: using `model` where `invModel` is expected does NOT ride
    float liveAtStartMin = kSdfOutside; // live distance at the vacated start pose (want > 0 = empty)
    for (int k = 1; k <= POSES; ++k) {
        const glm::mat4 M = poseAt(k);
        const SdfColliderView vLive = viewFor(M);
        SdfColliderView vSwap = vLive; vSwap.invModel = M; // model<->invModel swap (the wrong matrix)
        for (size_t i = 0; i < probes.size(); ++i) {
            const glm::vec3 world = glm::vec3(M * glm::vec4(probes[i], 1.0f)); // body-frame point, moved
            const float dLive = sdfDistanceWorld(vLive, world);
            liveMaxErr = std::max(liveMaxErr, std::abs(dLive - ref[i]));
            const float dFrozen = sdfDistanceWorld(vId, world); // frozen field, body-point moved away
            frozenMaxErr = std::max(frozenMaxErr, (dFrozen >= kSdfOutside * 0.5f)
                                                      ? std::abs(ref[i]) + 1.0f : std::abs(dFrozen - ref[i]));
            const float dSwap = sdfDistanceWorld(vSwap, world);
            swapMaxErr = std::max(swapMaxErr, (dSwap >= kSdfOutside * 0.5f)
                                                  ? std::abs(ref[i]) + 1.0f : std::abs(dSwap - ref[i]));
        }
        liveAtStartMin = std::min(liveAtStartMin, sdfDistanceWorld(vLive, vacated));
    }
    const float liveAtStartReport = std::min(liveAtStartMin, 9.999f); // kSdfOutside prints as ~10 (empty)

    // PASS: live field rides the body (invariance error tiny vs voxel band); the vacated start pose is
    // now empty (live > 0). TWO negative controls make this non-vacuous: (A) frozen field (sync OFF)
    // STILL collides at the start pose (deeply negative ghost) AND fails to ride; (B) swapping
    // model<->invModel (the ordering bug the reviewer flagged) FAILS to ride. So the gate is sensitive
    // to both "didn't refresh" and "wrong/ swapped transform".
    const float tol = 3.0f * voxel; // trilinear + rotation interpolation over one voxel band
    const bool ridesLive    = liveMaxErr < tol;
    const bool noLiveGhost  = liveAtStartMin > 0.0f;       // live leaves the start pose empty
    const bool negCtrlGhost = frozenAtStart < -0.1f;       // frozen STILL collides at start (the ghost)
    const bool negCtrlDrifts = frozenMaxErr > 5.0f * tol;  // frozen field does not ride
    const bool swapDrifts   = swapMaxErr > 5.0f * tol;     // model<->invModel swap does not ride
    const bool pass = ridesLive && noLiveGhost && negCtrlGhost && negCtrlDrifts && swapDrifts;

    printf("[collsync]   C2/C4 rides body (world->local via sdfRigidModel/invModel): liveMaxErr=%.5f m"
           " over %d poses x %zu probes (tol<%.4f)  %s\n",
           liveMaxErr, POSES, probes.size(), tol, ridesLive ? "PASS" : "FAIL");
    printf("[collsync]   C2 no-ghost: live sdf at vacated start pose = %.3f m (>0 = empty, not colliding)  %s\n",
           liveAtStartReport, noLiveGhost ? "PASS" : "FAIL");
    printf("[collsync]   NEG-CTRL A (sync OFF/frozen): start-pose sdf=%.3f m (<-0.1 = still collides = GHOST), "
           "driftErr=%.3f  %s\n",
           frozenAtStart, frozenMaxErr, (negCtrlGhost && negCtrlDrifts) ? "REJECTS" : "VACUOUS!");
    printf("[collsync]   NEG-CTRL B (model<->invModel swap): driftErr=%.3f (>>tol = doesn't ride)  %s\n",
           swapMaxErr, swapDrifts ? "REJECTS" : "VACUOUS!");
    printf("[collsync]   NOTE: gate proves the world->local CONVENTION + placement math; the uploadColliders\n"
           "[collsync]         call-site + GPU uniform binding are verified by code review + the fluid runtime smoke.\n");
    printf("[collsync] %s\n", pass ? "ALL PASS (C2 no-ghost + C4 zero-lag tracking + 2 neg-ctrls)" : "FAILURES PRESENT");
    std::fflush(stdout);
    return pass;
}

} // namespace krs::fluid
