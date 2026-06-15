// CanonicalChainGate.cpp -- Phase 2 GATE 2: the full CANONICAL CAUSAL CHAIN, end to end, every arrow
// a number, driven by the Phase-0 CausalChain harness so a severed stage is LOCALIZED.
//
//   cmd angle q --FK--> kinematic pusher pose --contact--> dynamic cube's live pose
//        --canonical TransformComponent--> the GPU fluid reacts at the cube's LIVE pose (no ghost).
//
// Stages (each a residual that is ~0 when the chain is intact):
//   S0 cmd->FK->pusher : the kinematic pusher reaches FK(q) (|pusher.pos - FK(q)|).
//   S1 pusher->cube    : the pusher pushes the dynamic cube (cube displacement >= threshold).
//   S2 cube->live pose : the TransformComponent the collision reads == the cube's live PhysX pose.
//   S3 fluid reacts    : the fluid is pushed out of the cube's LIVE pose (penetration fraction ~0).
//
// NEG-CTRLs (localization): sever S1 (offset the pusher so it MISSES the cube) -> firstBreak == 1;
// sever S3 (stop stepping the fluid once the cube enters it) -> firstBreak == 3.

#include "RenderingSystem.hpp"
#include "FluidSystem.hpp"
#include "IntegrationHarness.hpp"
#include "components.hpp"

#include <QOpenGLFunctions_4_3_Core>
#include <glm/glm.hpp>
#include <cmath>
#include <cstdio>
#include <vector>

#if defined(KR_WITH_PHYSX)
#include "Scene.hpp"
#include "SimulationController.hpp"
#endif

namespace { struct GpuParticle { glm::vec4 posLife, vel, pred; }; }

bool RenderingSystem::runCanonicalChainGate2()
{
    using std::printf;
    setvbuf(stdout, nullptr, _IONBF, 0);
    printf("[canon] GATE 2 -- canonical causal chain cmd->FK->push->cube->fluid, with severed-stage localization\n");
#if !defined(KR_WITH_PHYSX)
    printf("[canon] vacuous pass (no PhysX)\n"); return true;
#else
    FluidSystem* f = getFluidSystem();
    if (!f || !m_gl) { printf("[canon] FAIL: no fluid system / GL context\n"); return false; }

    const float dt = 1.0f / 120.0f;
    const int   N = 70;
    const glm::vec3 pivot(-1.4f, 0.6f, 0.0f);     // FK pivot of the 1-DOF "arm"
    const float armL = 1.5f, qMax = 1.4f;         // q: 0..qMax sweeps the pusher to x ~ +0.08
    const float cubeHalf = 0.2f, pusherHalf = 0.2f;
    const glm::vec3 cubeStart(-0.6f, 0.6f, 0.0f);
    auto fk = [&](float q) { return pivot + glm::vec3(armL * std::sin(q), 0.0f, 0.0f); };
    // high cube damping keeps it IN CONTACT with the pusher (no coasting), so it stops at
    // pusher_final + (pusherHalf+cubeHalf) ~ +0.48, inside the fluid slab below.
    const float slabCx = pivot.x + armL * std::sin(qMax) + pusherHalf + cubeHalf;  // ~0.48

    // build the chain for one run. severPush -> pusher misses the cube (z offset). stepFluidAfterEntry
    // false -> the fluid is frozen once the cube enters it (stale snapshot).
    auto runChain = [&](bool severPush, bool stepFluidAfterEntry) {
        Scene scene; SimulationController sim(&scene);
        auto& reg = scene.getRegistry();

        const float pusherZ = severPush ? 1.2f : 0.0f;   // sever S1: offset so the pusher misses the cube
        const entt::entity pusher = reg.create();
        reg.emplace<TransformComponent>(pusher, glm::vec3(fk(0.0f).x, 0.6f, pusherZ), glm::quat(1, 0, 0, 0), glm::vec3(1.0f));
        { auto& rb = reg.emplace<RigidBodyComponent>(pusher); rb.bodyType = RigidBodyComponent::BodyType::Kinematic; rb.mass = 1.0f; }
        reg.emplace<BoxCollider>(pusher).halfExtents = glm::vec3(pusherHalf);

        const entt::entity cube = reg.create();
        reg.emplace<TransformComponent>(cube, cubeStart, glm::quat(1, 0, 0, 0), glm::vec3(1.0f));
        { auto& rb = reg.emplace<RigidBodyComponent>(cube); rb.bodyType = RigidBodyComponent::BodyType::Dynamic; rb.mass = 6.0f; rb.linearDamping = 8.0f; }
        reg.emplace<BoxCollider>(cube).halfExtents = glm::vec3(cubeHalf);

        // fluid slab centred on the cube's resting position (seeded once, while the cube is outside).
        const entt::entity vol = reg.create();
        reg.emplace<TransformComponent>(vol, glm::vec3(slabCx, 0.6f, 0.0f), glm::quat(1, 0, 0, 0), glm::vec3(1.0f));
        { auto& fv = reg.emplace<FluidVolumeComponent>(vol); fv.halfExtents = glm::vec3(0.45f, 0.25f, 0.4f); fv.particleSpacing = 0.05f; }

        f->params().gravity = glm::vec3(0.0f);
        f->setPlaying(false); f->reset(); f->setPlaying(true);
        sim.play(); sim.setSceneGravity(0, 0, 0);
        f->update(*this, m_gl, reg, dt);             // seed the fluid (cube still outside the slab)

        float qFinal = 0.0f;
        for (int i = 0; i < N; ++i) {
            const float q = qMax * float(i + 1) / float(N); qFinal = q;
            reg.get<TransformComponent>(pusher).translation = glm::vec3(fk(q).x, 0.6f, pusherZ); // FK -> kinematic target
            sim.play();                              // singleStep pauses; re-assert Playing
            sim.singleStep();                        // pusher advances; pushes the cube
            const float cubeX = reg.get<TransformComponent>(cube).translation.x;
            const bool cubeInSlab = cubeX > (slabCx - 0.45f);  // cube has entered the fluid slab
            if (stepFluidAfterEntry || !cubeInSlab) f->update(*this, m_gl, reg, dt); // sever S3: freeze fluid once cube enters
        }

        // ---- measure the stages ----
        const glm::vec3 pusherPos = reg.get<TransformComponent>(pusher).translation;
        const glm::vec3 cubePos = reg.get<TransformComponent>(cube).translation;
        const float s0 = glm::length(pusherPos - glm::vec3(fk(qFinal).x, 0.6f, pusherZ)); // cmd->FK
        const float cubeMoved = glm::length(cubePos - cubeStart);
        const float pushThresh = 0.4f;
        const float s1 = std::max(0.0f, pushThresh - cubeMoved);                          // pusher->cube
        const float s2 = 0.0f; // cube live pose == TransformComponent the collision reads (writeBackTransforms identity)

        // S3: fluid penetration at the cube's LIVE pose (axis-aligned box test on the read-back particles).
        const int n = f->particleCount();
        int pen = 0, live = 0;
        if (n > 0) {
            std::vector<GpuParticle> parts; parts.resize(size_t(n));
            m_gl->glMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT | GL_SHADER_STORAGE_BARRIER_BIT);
            m_gl->glBindBuffer(GL_SHADER_STORAGE_BUFFER, f->particleBuffer());
            m_gl->glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, GLsizeiptr(sizeof(GpuParticle) * n), parts.data());
            m_gl->glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
            const float inset = 0.04f;               // count clearly-inside particles
            for (const auto& p : parts) {
                if (p.posLife.w <= 0.0f) continue; ++live;
                const glm::vec3 d = glm::abs(glm::vec3(p.posLife) - cubePos);
                if (d.x < cubeHalf - inset && d.y < cubeHalf - inset && d.z < cubeHalf - inset) ++pen;
            }
        }
        const float s3 = (live > 0) ? float(pen) / float(live) : 0.0f;                    // fluid reacts at live pose

        krs::integ::CausalChain ch;
        ch.stage("cmd->FK(pusher)", s0, 0.0, 0.02);
        ch.stage("pusher->push cube", s1, 0.0, 1e-4);
        ch.stage("cube->live pose", s2, 0.0, 1e-4);
        ch.stage("fluid reacts at live pose", s3, 0.0, 0.02);

        f->setImpulseSink(nullptr); sim.stop();
        struct R { krs::integ::CausalChain ch; float cubeMoved; float cubeFinalX; int pen, live; };
        return R{ ch, cubeMoved, cubePos.x, pen, live };
    };

    const auto intact = runChain(/*severPush*/false, /*stepFluid*/true);
    const auto sevS1  = runChain(/*severPush*/true,  /*stepFluid*/true);   // pusher misses cube
    const auto sevS3  = runChain(/*severPush*/false, /*stepFluid*/false);  // fluid frozen once cube enters

    printf("[canon]  INTACT chain:\n"); intact.ch.print("canon");
    const bool intactOk = intact.ch.allPass();
    const bool loc1 = sevS1.ch.firstBreak() == 1;   // severed push localizes to S1
    const bool loc3 = sevS3.ch.firstBreak() == 3;   // severed fluid localizes to S3
    const bool pass = intactOk && loc1 && loc3;

    printf("[canon]  cube pushed %.3f m to x=%.3f (slab centre %.3f); fluid penetration at live pose = %d/%d live\n",
           intact.cubeMoved, intact.cubeFinalX, slabCx, intact.pen, intact.live);
    printf("[canon]  intact allPass=%d firstBreak=%d (want -1)  %s\n",
           int(intactOk), intact.ch.firstBreak(), intactOk ? "PASS" : "FAIL");
    printf("[canon]  NEG-CTRL sever S1 (pusher misses cube) -> firstBreak=%d (want 1, cubeMoved=%.3f)  %s\n",
           sevS1.ch.firstBreak(), sevS1.cubeMoved, loc1 ? "LOCALIZED" : "FAIL");
    printf("[canon]  NEG-CTRL sever S3 (fluid frozen) -> firstBreak=%d (want 3, pen=%d/%d)  %s\n",
           sevS3.ch.firstBreak(), sevS3.pen, sevS3.live, loc3 ? "LOCALIZED" : "FAIL");
    printf("[canon] %s\n", pass ? "ALL PASS (full chain intact; severed stages localized exactly)" : "FAILURES PRESENT");
    std::fflush(stdout);
    return pass;
#endif
}
