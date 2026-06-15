// FluidRigidGate.cpp -- Phase 1 GATE 1.2: the FLUID<->RIGID two-way coupling proven as a Newton's
// 3rd-law (equal-and-opposite) conservation across the boundary. The GPU PBF fluid falls onto a
// DYNAMIC box; scene gravity is OFF so the box's only momentum source is the fluid. The per-collider
// impulse the fluid delivers (the SSBO accumulator, captured via setImpulseSink) must equal the
// rigid momentum the box gains (mass * v_after, starting at rest), within tol, with the per-frame
// dv-clamp verified inactive. NEGATIVE CONTROL: a sink that DROPS the impulse -> the box stays at
// rest (so the coupling, not gravity/anything else, is what moved it).

#include "RenderingSystem.hpp"
#include "FluidSystem.hpp"
#include "components.hpp"

#include <QOpenGLFunctions_4_3_Core>

#include <glm/glm.hpp>

#include <cstdio>
#include <utility>

#if defined(KR_WITH_PHYSX)
#include "Scene.hpp"
#include "SimulationController.hpp"
#endif

bool RenderingSystem::runFluidRigidImpulseGate()
{
    using std::printf;
    setvbuf(stdout, nullptr, _IONBF, 0);
    printf("[fluidrigid] GATE 1.2 -- FLUID<->RIGID Newton's 3rd law (delivered impulse == rigid momentum gained)\n");
#if !defined(KR_WITH_PHYSX)
    printf("[fluidrigid] vacuous pass (no PhysX)\n");
    return true;
#else
    FluidSystem* f = getFluidSystem();
    if (!f || !m_gl) { printf("[fluidrigid] FAIL: no fluid system / GL context\n"); return false; }

    const float boxMass = 150.0f;       // heavy: keeps per-frame dv under the 2 m/s clamp AND keeps the
                                        // box's travel small so it never reaches the y=0 floor (which
                                        // would add contact momentum and break the p==J equality).
    const int   N = 90;
    const float dt = 1.0f / 120.0f;

    // run(forward): the fluid falls onto a dynamic box. The sink ALWAYS accumulates the SSBO-delivered
    // impulse; it only FORWARDS it to the rigid solver when `forward` (the neg-control drops it).
    auto run = [&](bool forward, glm::dvec3& Jdeliv, glm::dvec3& pBox, double& maxClampRatio, double& boxBottomY) {
        Scene scene;
        SimulationController sim(&scene);
        auto& reg = scene.getRegistry();

        // dynamic box at rest, well above the y=0 floor
        const entt::entity box = reg.create();
        reg.emplace<TransformComponent>(box, glm::vec3(0.0f, 2.0f, 0.0f), glm::quat(1, 0, 0, 0), glm::vec3(1.0f));
        auto& rb = reg.emplace<RigidBodyComponent>(box);
        rb.bodyType = RigidBodyComponent::BodyType::Dynamic;
        rb.mass = boxMass; rb.linearDamping = 0.0f; rb.angularDamping = 0.0f;
        auto& bc = reg.emplace<BoxCollider>(box); bc.halfExtents = glm::vec3(0.3f);

        // fluid slab just above the box (falls 0.1 m and impacts)
        const entt::entity vol = reg.create();
        reg.emplace<TransformComponent>(vol, glm::vec3(0.0f, 2.65f, 0.0f), glm::quat(1, 0, 0, 0), glm::vec3(1.0f));
        auto& fv = reg.emplace<FluidVolumeComponent>(vol); fv.halfExtents = glm::vec3(0.25f, 0.2f, 0.25f);
        fv.particleSpacing = 0.05f;

        Jdeliv = glm::dvec3(0.0); maxClampRatio = 0.0;
        f->setImpulseSink([&](entt::entity e, const glm::vec3& J) {
            if (e != box) return;
            Jdeliv += glm::dvec3(J);
            maxClampRatio = std::max(maxClampRatio, double(glm::length(J)) / double(boxMass));
            if (forward) sim.applyFluidImpulse(e, J);  // applyFluidImpulse self-guards on Playing state
        });

        f->params().gravity = glm::vec3(0.0f, -9.81f, 0.0f);  // fluid falls
        f->setPlaying(false); f->reset(); f->setPlaying(true);
        sim.play();
        sim.setSceneGravity(0, 0, 0);                          // box only moves from the fluid impulse

        for (int i = 0; i < N; ++i) {
            // singleStep() pauses the sim, but applyFluidImpulse only acts while Playing -- so
            // re-assert Playing each frame (cheap: when already-built/Paused, play() just flips the
            // state, no rebuild) BEFORE update() fires the sink.
            sim.play();
            f->update(*this, m_gl, reg, dt);   // advect fluid + fire the sink (-> applyFluidImpulse)
            sim.singleStep();                  // integrate the rigid body with the queued impulse
        }
        pBox = double(boxMass) * glm::dvec3(reg.get<RigidBodyComponent>(box).linearVelocity);
        boxBottomY = double(reg.get<TransformComponent>(box).translation.y) - 0.3; // collider bottom
        f->setImpulseSink(nullptr);
        sim.stop();
    };

    glm::dvec3 JdelivR, pBoxR, JdelivN, pBoxN;
    double clampR = 0.0, clampN = 0.0, bottomR = 0.0, bottomN = 0.0;
    run(true, JdelivR, pBoxR, clampR, bottomR);    // real coupling
    run(false, JdelivN, pBoxN, clampN, bottomN);   // NEG-CTRL: sink drops the impulse

    const double jMag = glm::length(JdelivR);
    const double pMag = glm::length(pBoxR);
    const double eqErr = glm::length(pBoxR - JdelivR);                 // Newton's 3rd: gained == delivered
    const double relErr = jMag > 1e-9 ? eqErr / jMag : 1e9;
    const bool nonTrivial = jMag > 0.5 && pMag > 0.5;                  // the fluid actually hit the box
    const bool clampInactive = clampR < 2.0;                          // dv clamp never engaged
    const bool equalOpposite = relErr < 0.05;                         // gained == delivered to <5%
    const bool negCtrlStill = glm::length(pBoxN) < 0.10 * pMag;        // dropped impulse -> box stays at rest
    const bool floorClear = bottomR > 0.3;                            // box never reached the y=0 floor (no contact contamination)
    const bool pass = nonTrivial && clampInactive && equalOpposite && negCtrlStill && floorClear;

    printf("[fluidrigid]   delivered impulse |J|=%.3f N.s ; rigid momentum gained |p|=%.3f kg.m/s (dir match)\n", jMag, pMag);
    printf("[fluidrigid]   Newton 3rd: |p_gained - J_delivered|=%.4f (rel %.2f%%, bound<5%%) ; clamp ratio=%.3f (<2.0)  %s\n",
           eqErr, relErr * 100.0, clampR, (equalOpposite && clampInactive) ? "EQUAL-OPPOSITE" : "VIOLATED");
    printf("[fluidrigid]   NEG-CTRL (sink drops impulse): box momentum |p|=%.4f (must be ~0; J still %.3f)  %s\n",
           glm::length(pBoxN), glm::length(JdelivN), negCtrlStill ? "REJECTS(non-vacuous)" : "VACUOUS!");
    printf("[fluidrigid]   floor clearance: box collider bottom y=%.3f (must stay >0.3, no floor contact)  %s\n",
           bottomR, floorClear ? "CLEAR" : "FLOOR-CONTACT(contaminated!)");
    if (!nonTrivial)
        printf("[fluidrigid]   NOTE: trivial (fluid did not transfer momentum to the box) -- tune slab/box geometry\n");
    printf("[fluidrigid] %s\n", pass ? "ALL PASS (fluid->rigid equal-and-opposite; neg-ctrl inert box)" : "FAILURES PRESENT");
    std::fflush(stdout);
    return pass;
#endif
}
