// ConservationHarness.cpp -- Phase 0 harness A: the CONSERVATION instrument + GATE 0a.
// measureRigidBodies sums mass/momentum/energy from the ECS; GATE 0a drives a CLOSED two-body
// collision under zero gravity and asserts total linear momentum is conserved to <tol, with a
// non-vacuous NEGATIVE CONTROL (an injected momentum leak FAILS the same check).

#include "IntegrationHarness.hpp"
#include "components.hpp"

#include <cstdio>

#if defined(KR_WITH_PHYSX)
#include "Scene.hpp"
#include "SimulationController.hpp"
#endif

namespace krs::integ {

Conserved measureRigidBodies(entt::registry& reg, double gMag)
{
    Conserved c;
    for (auto e : reg.view<RigidBodyComponent, TransformComponent>()) {
        const auto& rb = reg.get<RigidBodyComponent>(e);
        if (rb.bodyType != RigidBodyComponent::BodyType::Dynamic) continue; // only free bodies carry momentum
        const auto& xf = reg.get<TransformComponent>(e);
        c.add(double(rb.mass), glm::dvec3(rb.linearVelocity), double(xf.translation.y), gMag);
    }
    return c;
}

bool runConservationGate0()
{
    using std::printf;
    setvbuf(stdout, nullptr, _IONBF, 0);
    printf("[conserve] GATE 0a -- conservation instrument: closed 2-body collision conserves momentum + neg-ctrl\n");
#if !defined(KR_WITH_PHYSX)
    printf("[conserve] vacuous pass (no PhysX)\n");
    return true;
#else
    Scene scene;
    SimulationController sim(&scene);
    auto& reg = scene.getRegistry();

    // Two dynamic spheres on the x-axis, no gravity -> a CLOSED system (only internal contact
    // forces). b1 (m=1) moves +x into b2 (m=2) at rest; total p_x = 2 throughout.
    auto mk = [&](float m, glm::vec3 pos, glm::vec3 vel) {
        const entt::entity e = reg.create();
        reg.emplace<TransformComponent>(e, pos, glm::quat(1, 0, 0, 0), glm::vec3(1.0f));
        auto& rb = reg.emplace<RigidBodyComponent>(e);
        rb.bodyType = RigidBodyComponent::BodyType::Dynamic;
        rb.mass = m; rb.linearVelocity = vel; rb.linearDamping = 0.0f; rb.angularDamping = 0.0f;
        auto& col = reg.emplace<SphereCollider>(e); col.radius = 0.25f;
        return e;
    };
    const entt::entity b1 = mk(1.0f, { -0.6f, 0.5f, 0.0f }, { 2.0f, 0.0f, 0.0f });
    mk(2.0f, { 0.3f, 0.5f, 0.0f }, { 0.0f, 0.0f, 0.0f });

    sim.play();
    sim.setSceneGravity(0, 0, 0);  // closed: isolate momentum from gravity impulse

    const Conserved before = measureRigidBodies(reg, 0.0);  // rb.linearVelocity = authored seeds
    for (int i = 0; i < 140; ++i) sim.singleStep();         // collide + separate (~48 steps to contact)
    const Conserved after = measureRigidBodies(reg, 0.0);   // rb.linearVelocity from PhysX

    const double dP = glm::length(after.momentum - before.momentum);

    // NEGATIVE CONTROL: inject a momentum leak (a hypothetical solver/coupling bug that loses a
    // body's momentum) by ZEROING b1's velocity, then re-measure -> the SAME conservation check the
    // real data passed must now FAIL on the leaked state.
    reg.get<RigidBodyComponent>(b1).linearVelocity = glm::vec3(0.0f);
    const Conserved leaked = measureRigidBodies(reg, 0.0);
    const double dPleak = glm::length(leaked.momentum - before.momentum);

    const double tol = 0.05;  // |dp| over the collision (PhysX contact is momentum-conserving)
    const bool conserved = dP < tol;
    const bool leakCaught = dPleak > 5.0 * tol;  // the leaked state clearly violates the check
    // The momentum check alone is also satisfied by FREE FLIGHT (a broken contact feature). Gate that
    // a real inelastic CONTACT happened: kinetic energy must drop (gMag=0 here so energy==KE). This
    // closes the "a one-line break that kills sphere-sphere contact still passes" hole.
    const bool collided = after.energy() < 0.5 * before.energy();
    const bool pass = conserved && leakCaught && collided && before.count == 2;

    printf("[conserve]   p_before=(%.4f,%.4f,%.4f) |p|=%.4f  E_before=%.4f\n",
           before.momentum.x, before.momentum.y, before.momentum.z, glm::length(before.momentum), before.energy());
    printf("[conserve]   p_after =(%.4f,%.4f,%.4f)  |dp|=%.5f (tol<%.3f)  E_after=%.4f  %s\n",
           after.momentum.x, after.momentum.y, after.momentum.z, dP, tol, after.energy(),
           conserved ? "CONSERVED" : "VIOLATED");
    printf("[conserve]   NEG-CTRL injected leak: |dp_leak|=%.4f (>>tol -> check catches non-conservation)  %s\n",
           dPleak, leakCaught ? "REJECTS(non-vacuous)" : "VACUOUS!");
    printf("[conserve]   contact check: KE %.3f->%.3f (must drop <0.5x for a real inelastic collision)  %s\n",
           before.energy(), after.energy(), collided ? "COLLIDED" : "FREE-FLIGHT(no contact!)");
    printf("[conserve] %s\n", pass ? "ALL PASS (momentum conserved + leak caught + real contact)" : "FAILURES PRESENT");
    sim.stop();
    std::fflush(stdout);
    return pass;
#endif
}

} // namespace krs::integ
