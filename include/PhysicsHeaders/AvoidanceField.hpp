#pragma once
// ===========================================================================
// Avoidance-field sprint — krs::field. The gateable FIELD foundation the
// (deferred) visualizer + MPC controller consume:
//   Phase 2  substance emission model (water/fire as an object MODIFIER)
//   Phase 3  dynamics-driven field-amplitude law  amplitude = f(v, a)
//   Phase 4  particle grid-SDF (distance + gradient)
//   Phase 4.5 SDF uncertainty + reaction tempering + temporal coherence
// The AVOIDANCE-FIELD emitter itself places engine PointEffectorComponents
// sampled by FieldSolver (rule 6 -- one field), so it has no struct here.
// ===========================================================================
#include <glm/glm.hpp>
#include <vector>
#include <algorithm>
#include <cmath>

namespace krs::field {

// --- Phase 3: dynamics-driven field-amplitude law (RL-free) ----------------
// amplitude = base + wV*max(0,vApproach) + wA*aApproach, clamped to >= 0.
// vApproach / aApproach are the velocity / acceleration components TOWARD the
// protected point (closing). At the SAME geometry + speed, a fast-ACCELERATING-
// toward object (aApproach>0) is scarier than a fast-DECELERATING one
// (aApproach<0): what matters is where it is GOING, not where it IS. wV/wA are
// author-tunable weights (node inputs).
inline double dynamicAmplitude(double vApproach, double aApproach,
                               double wV, double wA, double base = 1.0) {
    const double amp = base + wV * std::max(0.0, vApproach) + wA * aApproach;
    return amp > 0.0 ? amp : 0.0;
}

// the WRONG model: it sees geometry + velocity but IGNORES acceleration (drops the
// wA*aApproach term). So a fast-accelerating and a fast-decelerating object at the
// same speed get the IDENTICAL amplitude -> accel == const == decel -> it FAILS the
// strict ordering the real law passes. A genuine failing control (NOT base==base).
inline double geometryOnlyAmplitude(double vApproach, double /*aApproach*/,
                                    double wV, double /*wA*/, double base = 1.0) {
    const double amp = base + wV * std::max(0.0, vApproach);
    return amp > 0.0 ? amp : 0.0;
}

// --- Phase 2: substance emission MODEL ------------------------------------
// Water/fire emitted FROM an object: particles originate at the emitter object
// and follow it; rate-controlled. The live GPU injection (MPM/fluid SSBO) is
// deferred -- this is the gateable emission geometry (origin + rate + follows).
struct SubstanceParticle { glm::vec3 pos{ 0 }; glm::vec3 vel{ 0 }; double birth = 0; };

class SubstanceEmitter {
public:
    // Spawn from `origin` at `rate` particles/sec over dt (with `emitVel`),
    // carrying a fractional remainder so the long-run average rate is exact.
    // (NOT named emit() -- that is a Qt macro.)
    int spawn(const glm::vec3& origin, const glm::vec3& emitVel, double rate, double dt, double now) {
        if (rate <= 0.0 || dt <= 0.0) return 0;
        m_accum += rate * dt;
        int n = int(m_accum);
        m_accum -= double(n);
        for (int i = 0; i < n; ++i) m_parts.push_back({ origin, emitVel, now });
        return n;
    }
    const std::vector<SubstanceParticle>& particles() const { return m_parts; }
    int count() const { return int(m_parts.size()); }
    void clear() { m_parts.clear(); m_accum = 0.0; }
private:
    std::vector<SubstanceParticle> m_parts;
    double m_accum = 0.0;
};

// GATE EMITTER (env KRS_EMITTER_SELFTEST; in the bench): EMITTER-FIELD /
// EMITTER-SUBSTANCE / EMITTER-TYPE-SWITCH. Returns true iff all pass.
bool runEmitterGate();

// GATE FIELD-LAW (env KRS_FIELDLAW_SELFTEST; in the bench): FIELD-DYNAMICS (the
// amplitude ordering accel>const>decel>static; geometry-only fails it) +
// FIELD-AUTHORABLE (weighting changes amplitude) + the law->emitter pipe.
bool runFieldLawGate();

} // namespace krs::field
