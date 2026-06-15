#pragma once
// IntegrationHarness.hpp -- Phase 0 INTEGRATION SUBSTRATE. Reusable instruments that every
// cross-subsystem gate (Phases 1-5) measures against: a CONSERVATION instrument (total
// mass/momentum/energy across a boundary) and a CAUSAL-CHAIN instrument (drive an input, assert the
// propagated value at each downstream stage, localize the first break). Each ships with a GATE-0
// self-test on a known case + a non-vacuous negative control (injected leak / severed stage).

#include <glm/glm.hpp>
#include <entt/entt.hpp>

#include <string>
#include <vector>
#include <cmath>

namespace krs::integ {

// ----------------------------------------------------------------------------------------------
// CONSERVATION instrument (harness A): a snapshot of a system's conserved quantities. Accumulate
// any mix of rigid bodies / fluid particles / MPM particles into one Conserved, then diff two
// snapshots across a subsystem boundary.
// ----------------------------------------------------------------------------------------------
struct Conserved {
    double mass = 0.0;
    glm::dvec3 momentum{ 0.0 };   // sum m*v  [kg m/s]
    double kinetic = 0.0;         // sum 1/2 m |v|^2  [J]
    double potential = 0.0;       // sum m*g*y  [J] (g = +gravity magnitude, y = up)
    long count = 0;

    void add(double m, const glm::dvec3& v, double y, double gMag) {
        mass += m;
        momentum += m * v;
        kinetic += 0.5 * m * glm::dot(v, v);
        potential += m * gMag * y;
        ++count;
    }
    double energy() const { return kinetic + potential; }
};

// Accumulate all DYNAMIC rigid bodies from the ECS (RigidBodyComponent.mass + .linearVelocity, and
// TransformComponent.translation.y for PE). Velocity must be current (call after writeBackTransforms
// / singleStep). gMag is the gravity magnitude used for the PE term.
Conserved measureRigidBodies(entt::registry& reg, double gMag);

bool runConservationGate0();   // GATE 0a  (KRS_CONSERVATION_SELFTEST)

// ----------------------------------------------------------------------------------------------
// CAUSAL-CHAIN instrument (harness B): each stage records a residual that should be ~0 (measured vs
// expected within tol). firstBreak() localizes the earliest failing stage; allPass() is the AND.
// Drive an input at the head; a SEVERED stage (broken propagation) must make firstBreak() point at
// exactly that stage, not a downstream one.
// ----------------------------------------------------------------------------------------------
struct CausalStage {
    std::string name;
    double measured = 0.0;   // residual or value
    double expected = 0.0;
    double tol = 0.0;
    bool ok() const { return std::abs(measured - expected) <= tol; }
};

class CausalChain {
public:
    void stage(const std::string& name, double measured, double expected, double tol) {
        m_stages.push_back({ name, measured, expected, tol });
    }
    int firstBreak() const {
        for (size_t i = 0; i < m_stages.size(); ++i)
            if (!m_stages[i].ok()) return int(i);
        return -1;
    }
    bool allPass() const { return firstBreak() < 0; }
    const std::vector<CausalStage>& stages() const { return m_stages; }
    void print(const char* tag) const;   // "[tag]   stage k 'name': measured=.. expected=.. PASS/FAIL"
private:
    std::vector<CausalStage> m_stages;
};

bool runCausalChainGate0();    // GATE 0b  (KRS_CAUSALCHAIN_SELFTEST)

} // namespace krs::integ
