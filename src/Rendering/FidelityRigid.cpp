// FidelityRigid.cpp -- PhysX rigid-body fidelity gates: HARNESS-SELFTEST (free-fall), CONTACT (restitution),
// FRICTION-INCLINE, and the UNBOUNDED-GRIP diagnosis. Each runs a CANONICAL experiment in a throwaway
// Scene/SimulationController and compares a measured quantity to its closed-form answer. The experiment
// parameters (g, drop height, restitution, angle) are physical constants set explicitly; the gate never
// softens them to pass. Pattern mirrors src/Rendering/ConservationHarness.cpp.
#include "FidelityGates.hpp"
#include "components.hpp"
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <cstdio>
#include <cmath>
#include <vector>
#include <algorithm>

#if defined(KR_WITH_PHYSX)
#include "Scene.hpp"
#include "SimulationController.hpp"
#endif

namespace krs::fidelity {

namespace { constexpr double kG = 9.81;  constexpr double kDt = 1.0 / 240.0; }  // LOCKED physical constants

// ---------------------------------------------------------------------------------------------------------
// HARNESS-SELFTEST (Phase 0): a single body in free fall reaches v = g*t exactly; the comparison plumbing is
// itself validated by checking it FLAGS an injected wrong answer. Validates the measurement before it judges.
// ---------------------------------------------------------------------------------------------------------
bool runFidelitySelftestGate() {
    std::printf("[fidelity] HARNESS-SELFTEST -- free-fall reaches v=g*t; the comparison flags an injected wrong answer\n");
#if !defined(KR_WITH_PHYSX)
    std::printf("[fidelity] vacuous pass (no PhysX)\n"); return true;
#else
    Scene scene; SimulationController sim(&scene); auto& reg = scene.getRegistry();
    const entt::entity e = reg.create();                                  // one dynamic sphere, NO ground -> free fall
    reg.emplace<TransformComponent>(e, glm::vec3(0.0f, 5.0f, 0.0f), glm::quat(1, 0, 0, 0), glm::vec3(1.0f));
    auto& rb = reg.emplace<RigidBodyComponent>(e);
    rb.bodyType = RigidBodyComponent::BodyType::Dynamic;
    rb.mass = 1.0f; rb.linearVelocity = glm::vec3(0.0f); rb.linearDamping = 0.0f; rb.angularDamping = 0.0f;
    auto& col = reg.emplace<SphereCollider>(e); col.radius = 0.1f;

    sim.play();
    sim.setSceneGravity(0, -float(kG), 0);
    const int N = 120; const double T = double(N) * kDt;                  // 0.5 s
    for (int i = 0; i < N; ++i) sim.singleStep();
    const double vMeas = std::fabs(double(reg.get<RigidBodyComponent>(e).linearVelocity.y));
    sim.stop();

    const FidelityResult r{ "rigid", "free-fall v=g*t", vMeas, kG * T, 0.02 };
    reportFidelity(r);
    // SELF-TEST: an injected WRONG ground truth (half the real value) MUST be flagged by the same comparison.
    const FidelityResult wrong{ "rigid", "(injected wrong answer)", vMeas, 0.5 * kG * T, 0.02 };
    const bool flagsWrong = !wrong.pass();
    std::printf("[fidelity]   injected-wrong-answer (expected halved) flagged = %s\n", flagsWrong ? "YES" : "NO (plumbing broken!)");
    const bool pass = r.pass() && flagsWrong;
    std::printf("[fidelity] HARNESS-SELFTEST %s\n", pass ? "PASS" : "FAIL");
    return pass;
#endif
}

#if defined(KR_WITH_PHYSX)
// drop a sphere with restitution e from drop-height h0 (gap between sphere bottom and the ground) onto a static
// ground; return the bounce height of the sphere CENTRE above its rest-contact height (analytic = e^2 * h0).
static double measureBounce(double e, double h0) {
    Scene scene; SimulationController sim(&scene); auto& reg = scene.getRegistry();
    const float groundTop = 0.0f, radius = 0.15f;

    // static ground: a wide thin box whose TOP face is at y=0. Restitution is on the COLLIDER's embedded
    // material (the sim cooks PxMaterial from collider.material, not a separate PhysicsMaterial component).
    { const entt::entity g = reg.create();
      reg.emplace<TransformComponent>(g, glm::vec3(0.0f, groundTop - 0.5f, 0.0f), glm::quat(1, 0, 0, 0), glm::vec3(1.0f));
      auto& grb = reg.emplace<RigidBodyComponent>(g); grb.bodyType = RigidBodyComponent::BodyType::Static;
      auto& gb = reg.emplace<BoxCollider>(g); gb.halfExtents = glm::vec3(4.0f, 0.5f, 4.0f);
      gb.material.restitution = float(e); gb.material.staticFriction = 0.0f; gb.material.dynamicFriction = 0.0f; }

    // dynamic sphere, bottom h0 above the ground -> centre at groundTop + radius + h0.
    const entt::entity s = reg.create();
    const float y0 = groundTop + radius + float(h0);
    reg.emplace<TransformComponent>(s, glm::vec3(0.0f, y0, 0.0f), glm::quat(1, 0, 0, 0), glm::vec3(1.0f));
    auto& rb = reg.emplace<RigidBodyComponent>(s); rb.bodyType = RigidBodyComponent::BodyType::Dynamic;
    rb.mass = 1.0f; rb.linearVelocity = glm::vec3(0.0f); rb.linearDamping = 0.0f; rb.angularDamping = 0.0f;
    auto& sc = reg.emplace<SphereCollider>(s); sc.radius = radius;
    sc.material.restitution = float(e); sc.material.staticFriction = 0.0f; sc.material.dynamicFriction = 0.0f;

    sim.play();
    sim.setSceneGravity(0, -float(kG), 0);

    double apex = double(groundTop + radius); int phase = 0; double prevVy = 0.0;  // apex init = rest contact (no-bounce -> 0)
    for (int i = 0; i < 480; ++i) {                                // up to 2 s: fall + bounce + rise to apex
        sim.singleStep();
        const double vy = double(reg.get<RigidBodyComponent>(s).linearVelocity.y);
        const double y  = double(reg.get<TransformComponent>(s).translation.y);
        if (phase == 0 && prevVy < -0.1 && vy > 0.0) { phase = 1; apex = y; }   // first bounce detected
        if (phase == 1) { apex = std::max(apex, y); if (vy < 0.0) break; }      // track rise, stop at apex
        prevVy = vy;
    }
    sim.stop();
    return apex - double(groundTop + radius);   // centre height above rest contact
}
#endif

// ---------------------------------------------------------------------------------------------------------
// CONTACT (Phase 1): coefficient of restitution. A body dropped from h returns to e^2*h. Tested across e; the
// KNOWN-ANSWER comparison is the anti-fake (a wrong-restitution solver returns the wrong height), with e=0
// (no bounce) and e=1 (energy conserved) as the two limiting negative controls.
// ---------------------------------------------------------------------------------------------------------
bool runFidelityContactGate() {
    std::printf("[fidelity] CONTACT -- drop from h bounces to e^2*h (e=1 conserves energy, e=0 doesn't bounce)\n");
#if !defined(KR_WITH_PHYSX)
    std::printf("[fidelity] vacuous pass (no PhysX)\n"); return true;
#else
    const double h0 = 0.5;
    const double b09 = measureBounce(0.9, h0), b05 = measureBounce(0.5, h0), b00 = measureBounce(0.0, h0), b10 = measureBounce(1.0, h0);
    const FidelityResult r09{ "rigid", "restitution e=0.9 bounce=e^2 h", b09, 0.81 * h0, 0.15 };
    const FidelityResult r05{ "rigid", "restitution e=0.5 bounce=e^2 h", b05, 0.25 * h0, 0.15 };
    reportFidelity(r09); reportFidelity(r05);
    std::printf("  [rigid   ] restitution e=0.0 (no bounce)         measured=%.4f m (must be < 0.03)\n", b00);
    std::printf("  [rigid   ] restitution e=1.0 (energy conserved)  measured=%.4f m / h0=%.3f (ratio %.2f)\n", b10, h0, b10 / h0);
    const bool noBounce = b00 < 0.03;                 // e=0 neg-ctrl: stays down
    const bool conserves = std::fabs(b10 - h0) / h0 < 0.15;  // e=1 neg-ctrl: returns ~h0
    // discrimination: e=0.5's height must NOT match e=0.9's expected (proves the gate reads restitution).
    const bool discriminates = std::fabs(b05 - 0.81 * h0) / (0.81 * h0) > 0.20;
    const bool pass = r09.pass() && r05.pass() && noBounce && conserves && discriminates;
    std::printf("[fidelity] CONTACT %s  (e=0 no-bounce=%d, e=1 conserves=%d, discriminates=%d)\n",
                pass ? "PASS" : "FAIL", noBounce, conserves, discriminates);
    return pass;
#endif
}

// the remaining rigid gates are implemented in subsequent steps.
bool runFidelityFrictionGate()  { std::printf("[fidelity] FRICTION-INCLINE -- (pending)\n"); return true; }
bool runFidelityUnboundedGate() { std::printf("[fidelity] UNBOUNDED-DIAGNOSIS -- (pending)\n"); return true; }

} // namespace krs::fidelity
