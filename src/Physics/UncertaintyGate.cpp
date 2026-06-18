// ===========================================================================
// Avoidance-field sprint, Phase 4.5 — SDF uncertainty + reaction tempering +
// temporal coherence (krs::field; KRS_UNCERTAINTY_SELFTEST). The jitter/safety
// guard: don't violently dodge something you're unsure about, and don't let a
// noisy obstacle make the dodge direction flicker. Pure CPU.
//   UNCERTAINTY    : a freshly-seen region is high-variance; observation lowers it.
//   REACTION-TEMPER: for the same magnitude, high-uncertainty -> gentler reaction.
//   TEMPORAL-STABLE: a temporally-filtered gradient doesn't flicker; raw does.
// ===========================================================================
#include "AvoidanceField.hpp"

#include <cstdio>
#include <cmath>
#include <vector>
#include <algorithm>

namespace krs::field {

namespace {
GridSdf makeGrid(glm::vec3 origin, glm::vec3 extent, glm::ivec3 dims) {
    GridSdf g; g.origin = origin; g.extent = extent; g.dims = dims; return g;
}
} // namespace

bool runUncertaintyGate() {
    using std::printf;
    setvbuf(stdout, nullptr, _IONBF, 0);
    printf("[uncert] GATE UNCERTAINTY -- variance drops with observation + reaction tempering + temporal coherence\n");
    bool allOk = true;

    // ---- UNCERTAINTY: a fresh region is high-variance; observation lowers it ----
    {
        UncertainValue fresh;                                 // variance = prior (1.0), 0 observations
        UncertainValue observed;
        const double measVar = 0.3;
        double prev = observed.variance; bool monotonic = true;
        for (int i = 0; i < 10; ++i) { observed.observe(5.0, measVar); if (observed.variance >= prev) monotonic = false; prev = observed.variance; }
        const bool dropsOk = observed.variance < fresh.variance * 0.3 && observed.observations == 10 && monotonic;

        // NEG-CTRL: a real no-uncertainty model -- it is OBSERVED the same 10 times but
        // its variance never updates (ignores observation) -> it FAILS the drop contrast
        // the real model passes. (a function of the discriminating input that ignores it.)
        auto blindVarianceAfter = [](int observations, double prior, double /*measVar*/) {
            (void)observations; return prior;                 // no fusion: variance stays at the prior
        };
        const double blindVar = blindVarianceAfter(10, 1.0, measVar);
        const bool negFails = !(blindVar < fresh.variance * 0.3);   // the blind model does NOT drop -> contrast lost

        const bool ok = dropsOk && negFails;
        printf("[uncert]   UNCERTAINTY: fresh var=%.3f -> after 10 obs var=%.4f (drops monotonically:%d); "
               "NEG blind model var stays %.3f after 10 obs (no drop):%d  %s\n",
               fresh.variance, observed.variance, int(dropsOk), blindVar, int(negFails), ok ? "PASS" : "FAIL");
        allOk = allOk && ok;
    }

    // ---- REACTION-TEMPER: high uncertainty -> gentler reaction (lower gain) ----
    {
        const double baseGain = 1.0, magnitude = 5.0;         // SAME field magnitude for both regions
        const double varHigh = 1.0, varLow = 0.05;
        const double gainHigh = reactionGain(baseGain, varHigh);   // gentle
        const double gainLow  = reactionGain(baseGain, varLow);    // sharp
        const double reactHigh = magnitude * gainHigh, reactLow = magnitude * gainLow;
        const bool temperOk = gainHigh < gainLow && reactHigh < reactLow * 0.5;

        // ORTHOGONALITY DEMONSTRATED: a fast (high-magnitude) but UNSURE object has a
        // HIGHER magnitude yet a LOWER reaction than a slow (low-magnitude) but SURE one
        // -- magnitude (dynamics) and reaction-sharpness (uncertainty) move independently.
        const double magFast = dynamicAmplitude(2.0, 1.0, 1.0, 0.5, 1.0);   // fast+accel -> 3.5
        const double magSlow = dynamicAmplitude(0.3, 0.0, 1.0, 0.5, 1.0);   // slow -> 1.3
        const double reactFastUnsure = magFast * reactionGain(baseGain, varHigh);   // big magnitude, tempered
        const double reactSlowSure   = magSlow * reactionGain(baseGain, varLow);    // small magnitude, sharp
        const bool orthoOk = magFast > magSlow && reactFastUnsure < reactSlowSure;

        // NEG-CTRL: a gain model that IGNORES uncertainty -> identical gain regardless of
        // confidence -> it FAILS the tempering the real model produces.
        const double bHigh = uncertaintyBlindGain(baseGain, varHigh), bLow = uncertaintyBlindGain(baseGain, varLow);
        const bool negFails = !(bHigh < bLow && magnitude * bHigh < magnitude * bLow * 0.5);

        const bool ok = temperOk && orthoOk && negFails;
        printf("[uncert]   REACTION-TEMPER: high-var gain=%.3f (react %.2f) < low-var gain=%.3f (react %.2f):%d; "
               "ORTHO fast-unsure react=%.2f < slow-sure react=%.2f despite mag %.1f>%.1f:%d; NEG blind gain can't temper:%d  %s\n",
               gainHigh, reactHigh, gainLow, reactLow, int(temperOk), reactFastUnsure, reactSlowSure, magFast, magSlow, int(orthoOk), int(negFails), ok ? "PASS" : "FAIL");
        allOk = allOk && ok;
    }

    // ---- TEMPORAL-STABLE: the filtered dodge direction is STABLE *and* still
    // CORRECT/RESPONSIVE (a frozen filter is stable but gives a stale, wrong
    // direction -- the dangerous failure -- so we also require it to track a
    // persistent maneuver). ----
    {
        const glm::vec3 probe(1.0f, 0, 0);
        const float alpha = 0.15f;
        GridSdf sdf = makeGrid(glm::vec3(-1.5f), glm::vec3(3.0f), glm::ivec3(33));
        TemporalVec3 filt;
        double maxRawDelta = 0.0, maxFiltDelta = 0.0;
        glm::vec3 prevRaw(0), prevFilt(0); bool first = true;
        glm::vec3 lastFiltDir(0);
        // PHASE 1 (noise): obstacle jitters around mean M0=(0,0,0); the filter must reject the noise.
        for (int t = 0; t < 30; ++t) {
            const glm::vec3 obstacle(0.0f, 0.15f * std::sin(0.9f * float(t)), 0.15f * std::cos(1.7f * float(t)));
            sdf.build({ obstacle }, 0.1f);
            const glm::vec3 g = sdf.gradient(probe);
            const glm::vec3 rawDir = glm::normalize(g);
            const glm::vec3 filtDir = glm::normalize(filt.update(g, alpha));
            if (!first) {
                maxRawDelta = std::max(maxRawDelta, double(glm::length(rawDir - prevRaw)));
                maxFiltDelta = std::max(maxFiltDelta, double(glm::length(filtDir - prevFilt)));
            }
            prevRaw = rawDir; prevFilt = filtDir; first = false; lastFiltDir = filtDir;
        }
        const bool stableOk = maxRawDelta > 0.02 && maxFiltDelta < 0.4 * maxRawDelta;   // rejects the jitter
        // PHASE 1 correctness: the filtered direction tracks away-from-mean-M0 = +X (not frozen off-axis).
        const bool m0Ok = glm::dot(lastFiltDir, glm::vec3(1, 0, 0)) > 0.98f;

        // PHASE 2 (maneuver): the mean obstacle SHIFTS persistently to M1=(0,0.8,0).
        // A correct filter FOLLOWS (converges to away-from-M1); a FROZEN filter does NOT.
        const glm::vec3 M1(0.0f, 0.8f, 0.0f);
        const glm::vec3 awayM1 = glm::normalize(probe - M1);
        for (int t = 30; t < 70; ++t) {
            const glm::vec3 obstacle = M1 + glm::vec3(0.0f, 0.15f * std::sin(0.9f * float(t)), 0.15f * std::cos(1.7f * float(t)));
            sdf.build({ obstacle }, 0.1f);
            lastFiltDir = glm::normalize(filt.update(sdf.gradient(probe), alpha));
        }
        const float m1Dot = glm::dot(lastFiltDir, awayM1);
        const bool responsiveOk = m1Dot > 0.98f;   // converged to the new away-direction (a frozen filter fails this)

        const bool ok = stableOk && m0Ok && responsiveOk;
        printf("[uncert]   TEMPORAL-STABLE: noise raw-delta=%.4f vs filtered=%.4f (stable:%d); tracks M0 (+X):%d; "
               "after maneuver follows away-from-M1 (dot=%.3f>0.98, NOT frozen):%d  %s\n",
               maxRawDelta, maxFiltDelta, int(stableOk), int(m0Ok), m1Dot, int(responsiveOk), ok ? "PASS" : "FAIL");
        allOk = allOk && ok;
    }

    printf("[uncert] %s\n", allOk ? "ALL PASS (uncertainty drops with observation; reaction tempered by uncertainty; gradient temporally stable)"
                                  : "FAILURES PRESENT");
    fflush(stdout);
    return allOk;
}

} // namespace krs::field
