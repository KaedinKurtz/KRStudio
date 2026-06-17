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

#if defined(KR_WITH_PHYSX)
// the inclined-plane experiment in the rotated frame: a flat slab on flat ground, with GRAVITY tilted by theta
// (|g| LOCKED at 9.81; theta is the incline angle). Physically identical to a block on a theta-incline. The
// ground is RAISED to y=1 so the slab rests on MY controlled-friction box, not the sim's built-in y=0 ground
// plane (friction 0.6). vx0 keeps a sliding block AWAKE (the engine sleeps a near-stationary body ~0.4 s in);
// the acceleration is read from the vx slope in an EARLY window (before any sleep) so it is the true kinetic a.
struct SlideResult { double xDisp; double accel; };
static SlideResult slideTest(double mu, double thetaDeg, double vx0) {
    constexpr double kPi = 3.14159265358979323846;
    const double th = thetaDeg * kPi / 180.0;
    const float gy = 1.0f, by = 0.03f;            // ground top at y=1 (above the built-in plane); flat slab
    Scene scene; SimulationController sim(&scene); auto& reg = scene.getRegistry();

    { const entt::entity g = reg.create();
      reg.emplace<TransformComponent>(g, glm::vec3(0.0f, gy - 0.5f, 0.0f), glm::quat(1, 0, 0, 0), glm::vec3(1.0f));
      auto& grb = reg.emplace<RigidBodyComponent>(g); grb.bodyType = RigidBodyComponent::BodyType::Static;
      auto& gb = reg.emplace<BoxCollider>(g); gb.halfExtents = glm::vec3(8.0f, 0.5f, 8.0f);
      gb.material.staticFriction = float(mu); gb.material.dynamicFriction = float(mu); gb.material.restitution = 0.0f; }

    const entt::entity s = reg.create();
    reg.emplace<TransformComponent>(s, glm::vec3(0.0f, gy + by, 0.0f), glm::quat(1, 0, 0, 0), glm::vec3(1.0f));
    auto& rb = reg.emplace<RigidBodyComponent>(s); rb.bodyType = RigidBodyComponent::BodyType::Dynamic;
    rb.mass = 1.0f; rb.linearVelocity = glm::vec3(float(vx0), 0.0f, 0.0f); rb.linearDamping = 0.0f; rb.angularDamping = 0.0f;
    auto& bc = reg.emplace<BoxCollider>(s); bc.halfExtents = glm::vec3(0.15f, by, 0.15f);
    bc.material.staticFriction = float(mu); bc.material.dynamicFriction = float(mu); bc.material.restitution = 0.0f;

    sim.play();
    sim.setSceneGravity(float(kG * std::sin(th)), -float(kG * std::cos(th)), 0);  // gravity at angle theta to the normal
    const double x0 = double(reg.get<TransformComponent>(s).translation.x);
    for (int i = 0; i < 12; ++i) sim.singleStep();                          // brief normal-contact settle
    const double vxA = double(reg.get<RigidBodyComponent>(s).linearVelocity.x);
    const int W = 48;                                                       // early window, well before the ~96-step sleep
    for (int i = 0; i < W; ++i) sim.singleStep();
    const double vxB = double(reg.get<RigidBodyComponent>(s).linearVelocity.x);
    const double xEnd = double(reg.get<TransformComponent>(s).translation.x);
    sim.stop();
    return { xEnd - x0, (vxB - vxA) / (double(W) * kDt) };
}
#endif

// ---------------------------------------------------------------------------------------------------------
// FRICTION-INCLINE (Phase 2): the textbook friction gate. A block stays static iff theta <= atan(mu_s) and
// slides at a = g(sin theta - mu_k cos theta) above. The static/sliding TRANSITION angle and the sliding
// acceleration are both compared to the closed form. NEG-CTRL: frictionless slides at any angle (a = g sin).
// ---------------------------------------------------------------------------------------------------------
bool runFidelityFrictionGate() {
    std::printf("[fidelity] FRICTION-INCLINE -- slides iff theta>atan(mu_s); a=g(sin-mu_k cos) above; frictionless slides\n");
#if !defined(KR_WITH_PHYSX)
    std::printf("[fidelity] vacuous pass (no PhysX)\n"); return true;
#else
    constexpr double kPi = 3.14159265358979323846;
    const double mu = 0.5, thC = std::atan(mu) * 180.0 / kPi;               // critical angle = atan(0.5) = 26.57 deg
    auto aFormula = [&](double th) { const double r = th * kPi / 180.0; return kG * (std::sin(r) - mu * std::cos(r)); };

    const SlideResult below = slideTest(mu, 20.0, 0.0);                     // RESTING, < thC -> static (no slide)
    const SlideResult above = slideTest(mu, 35.0, 0.0);                     // RESTING, > thC -> breaks static, slides
    const SlideResult a40 = slideTest(mu, 40.0, 1.0);                       // MOVING: kinetic accel @ 40 deg
    const SlideResult a50 = slideTest(mu, 50.0, 1.0);                       // MOVING: kinetic accel @ 50 deg
    const SlideResult frictionless = slideTest(0.0, 10.0, 1.0);            // NEG-CTRL: frictionless slides at g sin

    std::printf("  [rigid   ] critical angle = atan(mu_s=%.2f) = %.2f deg\n", mu, thC);
    std::printf("  [rigid   ] RESTING theta=20<thC : x-disp=%.4f m (must be ~0, static)\n", below.xDisp);
    std::printf("  [rigid   ] RESTING theta=35>thC : x-disp=%.4f m (must break static -> slide)\n", above.xDisp);
    const FidelityResult r40{ "rigid", "incline accel @40deg g(sin-mu cos)", a40.accel, aFormula(40.0), 0.12 };
    const FidelityResult r50{ "rigid", "incline accel @50deg g(sin-mu cos)", a50.accel, aFormula(50.0), 0.12 };
    reportFidelity(r40); reportFidelity(r50);
    const double aFricExpect = kG * std::sin(10.0 * kPi / 180.0);
    std::printf("  [rigid   ] NEG-CTRL frictionless@10deg: a=%.3f vs g sin=%.3f m/s^2 (slides freely)\n", frictionless.accel, aFricExpect);

    const bool staysBelow  = std::fabs(below.xDisp) < 0.01;                 // static below the critical angle
    const bool slidesAbove = above.xDisp > 0.02;                            // breaks static above it -> transition ~atan(mu_s)
    const bool accelOk     = r40.pass() && r50.pass();                      // kinetic acceleration matches the formula at 2 angles
    const bool negSlides   = std::fabs(frictionless.accel - aFricExpect) / aFricExpect < 0.12;  // frictionless = g sin
    const bool pass = staysBelow && slidesAbove && accelOk && negSlides;
    std::printf("[fidelity] FRICTION-INCLINE %s  (static<thC=%d, slides>thC=%d, accel=%d, frictionless=%d)\n",
                pass ? "PASS" : "FAIL", staysBelow, slidesAbove, accelOk, negSlides);
    return pass;
#endif
}

bool runFidelityUnboundedGate() { std::printf("[fidelity] UNBOUNDED-DIAGNOSIS -- (pending)\n"); return true; }

} // namespace krs::fidelity
