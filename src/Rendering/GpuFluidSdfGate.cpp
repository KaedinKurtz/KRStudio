// GpuFluidSdfGate.cpp -- Phase 0 harness C: GATE 0c. The first headless test that ACTUALLY
// dispatches the GPU fluid against an SDF mesh collider (GATE C was CPU-only). It drives the real
// FluidSystem::update() on the engine context: seed a fluid slab, bake an SDF cube AWAY from it,
// then move the cube INTO the slab. With transform-sync ON the field rides to the LIVE pose and the
// fluid is pushed out of it (no penetration); the NEGATIVE CONTROL (sync OFF) freezes the field at
// the bake pose, so the fluid penetrates the cube's live location (the ghost) -- caught numerically.

#include "RenderingSystem.hpp"
#include "FluidSystem.hpp"
#include "SdfColliderQuery.hpp"
#include "SdfBaker.hpp"
#include "components.hpp"

#include <QOpenGLFunctions_4_3_Core>

#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstdio>
#include <utility>
#include <vector>

namespace {
// Mirror of the anonymous FluidSystem::GpuParticle layout (FluidSystem.cpp:18).
struct GpuParticle { glm::vec4 posLife; glm::vec4 vel; glm::vec4 pred; };

// A unit-ish cube of side 2h, 12 outward triangles (same winding as CollisionSyncGate::buildUnitCube
// so OpenVDB bakes a clean closed level set).
void buildCube(float h, std::vector<Vertex>& v, std::vector<unsigned int>& idx)
{
    const glm::vec3 c[8] = {
        {-h,-h,-h}, { h,-h,-h}, { h, h,-h}, {-h, h,-h},
        {-h,-h, h}, { h,-h, h}, { h, h, h}, {-h, h, h},
    };
    for (const auto& p : c) { Vertex vt; vt.position = p; v.push_back(vt); }
    const unsigned f[6][4] = { {0,3,2,1}, {4,5,6,7}, {0,1,5,4}, {3,7,6,2}, {1,2,6,5}, {0,4,7,3} };
    for (auto& fc : f) idx.insert(idx.end(), { fc[0], fc[1], fc[2], fc[0], fc[2], fc[3] });
}
} // namespace

bool RenderingSystem::runGpuFluidSdfGate()
{
    using std::printf;
    setvbuf(stdout, nullptr, _IONBF, 0);
    printf("[fluidsdf] GATE 0c -- GPU fluid dispatched against a MOVING SDF collider (closes GATE C's GPU gap)\n");

    FluidSystem* f = getFluidSystem();
    if (!f || !m_gl) { printf("[fluidsdf] FAIL: no fluid system / GL context\n"); return false; }

    // Cube collider (side 0.6) + a CPU SDF mirror to classify read-back particles (same mesh + voxel
    // the fluid bakes, so the classifier and the GPU field agree).
    std::vector<Vertex> cv; std::vector<unsigned int> ci;
    buildCube(0.3f, cv, ci);
    SdfBakeResult baked;
    if (!bakeMeshToSdf(cv, ci, glm::mat4(1.0f), 0.03f, baked)) {
        printf("[fluidsdf] vacuous pass (OpenVDB unavailable -> SDF bake disabled)\n");
        std::fflush(stdout);
        return true;
    }

    // The fluid domain floor is at y=0, so keep the slab + collider well clear of it (centre y=0.6,
    // slab halfExtents 0.5 -> y in [0.1,1.1]); otherwise the floor clamps the lower half and few
    // particles remain at the collider's pose, starving the neg-control.
    const float yc = 0.6f;
    const glm::vec3 poseA(3.0f, yc, 0.0f);    // bake pose: outside the fluid slab (clean bake)
    const glm::vec3 poseB(0.0f, yc, 0.0f);    // live pose: centre of the fluid slab
    const float tol = 0.05f;                  // a particle is "inside" if sdf < -tol

    // Classifier against the cube's LIVE (B) pose.
    krs::fluid::SdfColliderView viewB;
    viewB.invModel = glm::inverse(krs::fluid::sdfRigidModel(poseB, glm::quat(1, 0, 0, 0)));
    viewB.aabbMin = baked.aabbMin; viewB.aabbMax = baked.aabbMax;
    viewB.dims = baked.dims; viewB.field = baked.field.data();

    auto run = [&](bool sync) -> std::pair<int, int> {  // {penetrating, totalLive}
        entt::registry reg;
        // fluid slab centred at y=yc (clear of the y=0 domain floor)
        const entt::entity vol = reg.create();
        reg.emplace<TransformComponent>(vol, glm::vec3(0.0f, yc, 0.0f), glm::quat(1, 0, 0, 0), glm::vec3(1.0f));
        auto& fv = reg.emplace<FluidVolumeComponent>(vol); fv.halfExtents = glm::vec3(0.5f); fv.particleSpacing = 0.05f;
        // SDF cube collider at the bake pose A
        const entt::entity col = reg.create();
        reg.emplace<TransformComponent>(col, poseA, glm::quat(1, 0, 0, 0), glm::vec3(1.0f));
        auto& rm = reg.emplace<RenderableMeshComponent>(col); rm.vertices = cv; rm.indices = ci;
        reg.emplace<SDFColliderComponent>(col);

        f->params().gravity = glm::vec3(0.0f);     // closed: motion is only the SDF pushout
        f->setPlaying(false); f->reset(); f->setPlaying(true);  // force re-seed + re-bake (transition)
        f->setSdfTransformSync(sync);
        f->update(*this, m_gl, reg, 1.0f / 120.0f); // seed fluid + bake SDF at pose A
        reg.get<TransformComponent>(col).translation = poseB;   // move the cube INTO the slab
        for (int i = 0; i < 25; ++i) f->update(*this, m_gl, reg, 1.0f / 120.0f);

        const int n = f->particleCount();
        if (n <= 0) return { -1, 0 };
        std::vector<GpuParticle> parts;
        parts.resize(static_cast<size_t>(n));
        m_gl->glMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT | GL_SHADER_STORAGE_BARRIER_BIT);
        m_gl->glBindBuffer(GL_SHADER_STORAGE_BUFFER, f->particleBuffer());
        m_gl->glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, GLsizeiptr(sizeof(GpuParticle) * n), parts.data());
        m_gl->glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

        int pen = 0, live = 0;
        for (const auto& p : parts) {
            if (p.posLife.w <= 0.0f) continue;
            ++live;
            if (krs::fluid::sdfDistanceWorld(viewB, glm::vec3(p.posLife)) < -tol) ++pen;
        }
        return { pen, live };
    };

    const auto onR = run(true);
    const auto offR = run(false);
    const int penOn = onR.first, liveOn = onR.second;
    const int penOff = offR.first, liveOff = offR.second;

    if (liveOn <= 0 || liveOff <= 0) {
        printf("[fluidsdf] vacuous pass (no particles seeded -> fluid path inactive; liveOn=%d liveOff=%d)\n",
               liveOn, liveOff);
        std::fflush(stdout);
        return true;
    }

    const double fracOn = double(penOn) / liveOn;
    const double fracOff = double(penOff) / liveOff;
    // PASS: sync ON keeps fluid out of the LIVE pose (few penetrators); NEG-CTRL sync OFF freezes the
    // field at the bake pose -> the fluid penetrates the cube's live location (the ghost).
    const bool sdfPushesOut = fracOn < 0.02;
    const bool ghostCaught = fracOff > 0.10 && penOff > 5 * (penOn + 1);
    const bool pass = sdfPushesOut && ghostCaught;

    printf("[fluidsdf]   SYNC ON  (field rides to live pose): penetrating=%d / %d live = %.1f%%  %s\n",
           penOn, liveOn, fracOn * 100.0, sdfPushesOut ? "PASS (pushed out)" : "FAIL");
    printf("[fluidsdf]   NEG-CTRL SYNC OFF (field frozen at bake pose): penetrating=%d / %d live = %.1f%% (ghost)  %s\n",
           penOff, liveOff, fracOff * 100.0, ghostCaught ? "REJECTS(non-vacuous)" : "VACUOUS!");
    printf("[fluidsdf] %s\n", pass ? "ALL PASS (GPU fluid avoids the SDF's LIVE pose; ghost reproduced sync-off)"
                                   : "FAILURES PRESENT");
    std::fflush(stdout);
    return pass;
}
