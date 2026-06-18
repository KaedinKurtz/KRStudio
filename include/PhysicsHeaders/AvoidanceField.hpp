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

// --- Phase 4: particle grid-SDF (distance + gradient) ----------------------
// Splat particles to a grid signed-distance field each frame: per node,
// d = min_i |node - p_i| - radius (<0 inside the union of particle spheres, the
// engine's SdfColliderView sign convention). The avoidance layer reads distance
// AND gradient (the gradient is the away-from-substance dodge direction). NOT a
// per-frame mesh. `band` caps each particle's update radius in cells: band >=
// grid -> exact min (brute force); a small band -> a fast near-field splat.
struct GridSdf {
    glm::vec3 origin{ 0.0f }, extent{ 1.0f };
    glm::ivec3 dims{ 8, 8, 8 };
    std::vector<float> field;     // dims.x*dims.y*dims.z, signed distance (<0 inside)
    float radius = 0.05f;

    int idx(int i, int j, int k) const { return (k * dims.y + j) * dims.x + i; }
    glm::vec3 nodePos(int i, int j, int k) const {
        return origin + extent * (glm::vec3(float(i), float(j), float(k)) / glm::vec3(dims - glm::ivec3(1)));
    }

    void build(const std::vector<glm::vec3>& parts, float r, int band = 1 << 20) {
        radius = r;
        field.assign(size_t(dims.x) * dims.y * dims.z, 1.0e9f);
        const glm::vec3 span = glm::vec3(dims - glm::ivec3(1));
        for (const auto& p : parts) {
            const glm::vec3 t = (p - origin) / extent * span;        // particle node-index
            const glm::ivec3 c = glm::ivec3(glm::round(t));
            const glm::ivec3 lo = glm::max(c - glm::ivec3(band), glm::ivec3(0));
            const glm::ivec3 hi = glm::min(c + glm::ivec3(band), dims - glm::ivec3(1));
            for (int k = lo.z; k <= hi.z; ++k)
                for (int j = lo.y; j <= hi.y; ++j)
                    for (int i = lo.x; i <= hi.x; ++i) {
                        const float d = glm::length(nodePos(i, j, k) - p) - r;
                        float& cell = field[idx(i, j, k)];
                        if (d < cell) cell = d;
                    }
        }
    }
    float nodeValue(int i, int j, int k) const { return field[idx(i, j, k)]; }

    // trilinear sample at a world point (node-centred grid; clamped to the grid).
    float sample(const glm::vec3& w) const {
        const glm::vec3 t = (w - origin) / extent * glm::vec3(dims - glm::ivec3(1));
        const glm::ivec3 i0 = glm::clamp(glm::ivec3(glm::floor(t)), glm::ivec3(0), dims - glm::ivec3(2));
        const glm::vec3 f = glm::clamp(t - glm::vec3(i0), glm::vec3(0.0f), glm::vec3(1.0f));
        auto V = [&](int di, int dj, int dk) { return field[idx(i0.x + di, i0.y + dj, i0.z + dk)]; };
        const float x00 = glm::mix(V(0,0,0), V(1,0,0), f.x), x10 = glm::mix(V(0,1,0), V(1,1,0), f.x);
        const float x01 = glm::mix(V(0,0,1), V(1,0,1), f.x), x11 = glm::mix(V(0,1,1), V(1,1,1), f.x);
        return glm::mix(glm::mix(x00, x10, f.y), glm::mix(x01, x11, f.y), f.z);
    }
    // central-difference gradient (the avoidance direction; points away from the substance).
    glm::vec3 gradient(const glm::vec3& w) const {
        const glm::vec3 h = extent / glm::vec3(dims - glm::ivec3(1));
        return glm::vec3(
            (sample(w + glm::vec3(h.x, 0, 0)) - sample(w - glm::vec3(h.x, 0, 0))) / (2.0f * h.x),
            (sample(w + glm::vec3(0, h.y, 0)) - sample(w - glm::vec3(0, h.y, 0))) / (2.0f * h.y),
            (sample(w + glm::vec3(0, 0, h.z)) - sample(w - glm::vec3(0, 0, h.z))) / (2.0f * h.z));
    }
};

// the avoidance field a SUBSTANCE produces at a point: distance falloff (from the
// SDF) scaled by the stream's DYNAMICS (Phase-3 law) -- a fast-moving stream is
// scarier than a still pool of the same geometry.
// SENTINEL/BAND note: a band-limited build leaves cells beyond band*cell at the
// 1e9 sentinel; that is intentional -- with range <= band*cell, a sentinel sample
// gives falloff = max(0, 1 - 1e9/range) = 0, so the substance field degrades
// GRACEFULLY to 0 outside the avoidance-relevant near field (no avoidance far away,
// which is correct), while the in-band field is exact.
inline double substanceFieldMagnitude(const GridSdf& sdf, const glm::vec3& point,
                                      double streamSpeed, double streamAccel,
                                      double wV, double wA, double base, double range) {
    const double dist = double(sdf.sample(point));
    const double falloff = std::max(0.0, 1.0 - std::max(0.0, dist) / range);   // 1 at surface, 0 at range
    return dynamicAmplitude(streamSpeed, streamAccel, wV, wA, base) * falloff;
}

// GATE SDF (env KRS_SDF_SELFTEST; in the bench): SDF-DISTANCE / SDF-GRADIENT /
// SDF-DYNAMICS / SDF-PERF. Returns true iff all pass.
bool runSdfGate();

// --- Phase 4.5: SDF uncertainty + reaction tempering + temporal coherence ---
// A region's UNCERTAINTY drops as it is observed (fresh = high variance). A 1-D
// Kalman fusion: each measurement shrinks the variance.
struct UncertainValue {
    double mean = 0.0;
    double variance = 1.0;      // prior -- HIGH when freshly seen
    int observations = 0;
    void observe(double z, double measVar) {
        const double k = variance / (variance + measVar);   // Kalman gain
        mean += k * (z - mean);
        variance *= (1.0 - k);                               // shrinks each observation
        ++observations;
    }
};

// REACTION GAIN scales INVERSELY with uncertainty: gentle (low gain) when unsure,
// sharp (high gain) when confident -- a SEPARATE coupling from dynamics->magnitude.
// Don't violently dodge something you are not sure about (the violent reaction to
// noise is itself dangerous).
inline double reactionGain(double baseGain, double variance, double k = 4.0) {
    return baseGain / (1.0 + k * variance);
}
// the neg-control: a model whose gain IGNORES uncertainty (constant).
inline double uncertaintyBlindGain(double baseGain, double /*variance*/, double /*k*/ = 4.0) { return baseGain; }

// TEMPORAL coherence: an EMA filter on the field gradient so the dodge DIRECTION is
// stable frame-to-frame (no flickering avoidance that makes the robot chatter).
struct TemporalVec3 {
    glm::vec3 value{ 0.0f };
    bool init = false;
    glm::vec3 update(const glm::vec3& sample, float alpha) {
        value = init ? glm::mix(value, sample, alpha) : sample;
        init = true;
        return value;
    }
};

// GATE UNCERTAINTY (env KRS_UNCERTAINTY_SELFTEST; in the bench): UNCERTAINTY /
// REACTION-TEMPER / TEMPORAL-STABLE. Returns true iff all pass.
bool runUncertaintyGate();

// GATE FIELD-LAW (env KRS_FIELDLAW_SELFTEST; in the bench): FIELD-DYNAMICS (the
// amplitude ordering accel>const>decel>static; geometry-only fails it) +
// FIELD-AUTHORABLE (weighting changes amplitude) + the law->emitter pipe.
bool runFieldLawGate();

} // namespace krs::field
