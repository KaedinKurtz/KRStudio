#pragma once
// FidelityGates.hpp -- the PHYSICS-FIDELITY VALIDATION HARNESS. Each gate is a CANONICAL EXPERIMENT whose
// answer is known from theory or published experiment (the textbook is the ground truth). A solver that
// matches reality to a stated tolerance PASSES; a solver that is unfaithful FAILS, and that failure is a REAL
// FINDING (the spec for the upgrade), reported honestly -- never tuned around. The experiment parameters
// (g, densities, dimensions, E) are PHYSICAL CONSTANTS, locked and asserted; softening them to pass is faking.
// The built-in NEGATIVE CONTROL is wrong-physics FAILING the same known-answer check.
#include <cmath>
#include <cstdio>

namespace krs::fidelity {

// one measured-vs-known-answer comparison. relErr is the fidelity number; pass = within the stated tolerance.
struct FidelityResult {
    const char* subsystem;
    const char* experiment;
    double measured;
    double expected;     // the analytic / published ground truth
    double tol;          // PASS iff relErr <= tol (relative); the fidelity tolerance
    double relErr() const {
        const double d = std::fabs(expected) > 1e-12 ? std::fabs(expected) : 1e-12;
        return std::fabs(measured - expected) / d;
    }
    bool pass() const { return relErr() <= tol; }
};

inline void reportFidelity(const FidelityResult& r) {
    std::printf("  [%-8s] %-30s measured=%.5g  expected=%.5g  relErr=%5.1f%%  tol=%4.1f%%  -> %s\n",
                r.subsystem, r.experiment, r.measured, r.expected, r.relErr() * 100.0, r.tol * 100.0,
                r.pass() ? "PASS" : "FAIL");
}

// --- the gates (each a canonical experiment with a wrong-physics negative control) ---
bool runFidelitySelftestGate();    // HARNESS-SELFTEST : free-fall v=gt to <tol; an injected wrong answer is flagged
bool runFidelityContactGate();     // CONTACT          : drop from h bounces to e^2*h across e (e=1 conserves, e=0 no bounce)
bool runFidelityFrictionGate();    // FRICTION-INCLINE : slides iff theta>atan(mu_s); accel g(sin-mu_k cos) above
bool runFidelityUnboundedGate();   // UNBOUNDED-DIAGNOSIS : is gripper over-squeeze real wedging or a rigid-jaw artifact?
bool runFidelityCantileverGate();  // FEM-CANTILEVER   : tip deflection vs PL^3/(3EI)
bool runFidelityThermalGate();     // THERMAL-ANALYTIC : steady-state bar temperature vs the linear heat-equation profile
bool runFidelityScorecardGate();   // SCORECARD        : consolidated per-subsystem fidelity table

} // namespace krs::fidelity
