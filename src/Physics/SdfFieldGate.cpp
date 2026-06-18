// ===========================================================================
// Avoidance-field sprint, Phase 4 — particle grid-SDF gates (krs::field;
// KRS_SDF_SELFTEST). The grid-SDF (AvoidanceField.hpp GridSdf) splats particles
// to a signed-distance grid (d = min|x-p| - r), providing distance + gradient
// for the avoidance layer. Pure CPU, analytic ground truth.
//   SDF-DISTANCE : sampled distance matches the analytic distance to the cloud.
//   SDF-GRADIENT : the gradient points AWAY from the nearest substance.
//   SDF-DYNAMICS : a fast-moving stream -> stronger field than a still pool.
//   SDF-PERF     : per-frame splat cost at a realistic resolution (interactive).
// ===========================================================================
#include "AvoidanceField.hpp"

#include <cstdio>
#include <cmath>
#include <vector>
#include <chrono>
#include <glm/gtc/constants.hpp>

namespace krs::field {

namespace {
GridSdf makeGrid(glm::vec3 origin, glm::vec3 extent, glm::ivec3 dims) {
    GridSdf g; g.origin = origin; g.extent = extent; g.dims = dims; return g;
}
} // namespace

bool runSdfGate() {
    using std::printf;
    setvbuf(stdout, nullptr, _IONBF, 0);
    printf("[sdf] GATE SDF -- particle grid-SDF distance + gradient + dynamics scaling + perf\n");
    bool allOk = true;
    const float r = 0.1f;
    const glm::vec3 center(0, 0, 0);

    // ---- SDF-DISTANCE: sampled distance vs analytic |x-c|-r ----
    {
        GridSdf sdf = makeGrid(glm::vec3(-1.0f), glm::vec3(2.0f), glm::ivec3(49));
        std::vector<glm::vec3> parts = { center };
        sdf.build(parts, r);                                      // exact (band default)
        const glm::vec3 probes[] = { {0.5f,0,0}, {0.3f,0.2f,0}, {0,0.4f,0.2f}, {-0.35f,0.1f,0.15f} };
        double maxErr = 0.0;
        for (const auto& p : probes) {
            const double analytic = double(glm::length(p - center)) - r;
            maxErr = std::max(maxErr, std::abs(double(sdf.sample(p)) - analytic));
        }
        // INTERIOR: a point inside the particle reads d < 0 (the <0-inside convention the collider layer needs).
        const double interiorErr = std::abs(double(sdf.sample(glm::vec3(0.05f, 0, 0))) - (-0.05));   // |0.05|-0.1
        const bool interiorOk = sdf.sample(glm::vec3(0, 0, 0)) < 0.0f && interiorErr < 0.02;
        // NEG-CTRL: an EMPTY SDF (no particles) reads a huge distance, not the analytic.
        GridSdf empty = makeGrid(glm::vec3(-1.0f), glm::vec3(2.0f), glm::ivec3(49));
        empty.build({}, r);
        const double emptyErr = std::abs(double(empty.sample(glm::vec3(0.5f, 0, 0))) - 0.4);
        const bool ok = maxErr < 0.02 && interiorOk && emptyErr > 1.0;
        printf("[sdf]   SDF-DISTANCE: max |sample - analytic| = %.5f m (<0.02, ok:%d); interior d<0 (err %.4f):%d; NEG empty-SDF err=%.2e (>1)  %s\n",
               maxErr, int(maxErr < 0.02), interiorErr, int(interiorOk), emptyErr, ok ? "PASS" : "FAIL");
        allOk = allOk && ok;
    }

    // ---- SDF-GRADIENT: points away from the nearest substance ----
    {
        GridSdf sdf = makeGrid(glm::vec3(-1.0f), glm::vec3(2.0f), glm::ivec3(49));
        sdf.build({ center }, r);
        const glm::vec3 probes[] = { {0.3f,0,0}, {0,0.3f,0}, {0.2f,0.2f,0.1f}, {-0.3f,0.1f,0} };
        double minDot = 2.0, minMag = 1e9;
        for (const auto& p : probes) {
            const glm::vec3 g = sdf.gradient(p);
            const glm::vec3 away = glm::normalize(p - center);    // analytic away-from-particle
            minMag = std::min(minMag, double(glm::length(g)));
            if (glm::length(g) > 1e-4f) minDot = std::min(minDot, double(glm::dot(glm::normalize(g), away)));
        }
        // NEG-CTRL: an empty SDF is flat -> gradient ~ 0 -> no dodge direction.
        GridSdf empty = makeGrid(glm::vec3(-1.0f), glm::vec3(2.0f), glm::ivec3(49));
        empty.build({}, r);
        const double emptyMag = double(glm::length(empty.gradient(glm::vec3(0.3f, 0, 0))));
        const bool ok = minDot > 0.99 && minMag > 0.5 && emptyMag < 1e-3;
        printf("[sdf]   SDF-GRADIENT: min dot(grad, away)=%.4f (>0.99, ok:%d), min|grad|=%.3f; NEG empty |grad|=%.2e (~0)  %s\n",
               minDot, int(minDot > 0.99), minMag, emptyMag, ok ? "PASS" : "FAIL");
        allOk = allOk && ok;
    }

    // ---- SDF-DYNAMICS: a fast-moving stream is stronger than a still pool ----
    {
        // a small cloud (the substance), same geometry for both cases.
        std::vector<glm::vec3> cloud;
        for (int i = 0; i < 27; ++i) cloud.push_back(glm::vec3(0.04f * (i % 3 - 1), 0.04f * ((i / 3) % 3 - 1), 0.04f * (i / 9 - 1)));
        GridSdf sdf = makeGrid(glm::vec3(-1.0f), glm::vec3(2.0f), glm::ivec3(49));
        sdf.build(cloud, r);
        const glm::vec3 probe(0.3f, 0, 0);
        const double wV = 1.0, wA = 0.5, base = 1.0, range = 1.0;
        const double fieldMoving = substanceFieldMagnitude(sdf, probe, 2.0, 1.0, wV, wA, base, range);   // fast stream
        const double fieldStill  = substanceFieldMagnitude(sdf, probe, 0.0, 0.0, wV, wA, base, range);   // still pool
        const bool dynOk = fieldMoving > fieldStill * 1.2;       // meaningfully stronger
        // NEG-CTRL: a geometry-only model (wV=wA=0 -> ignores the stream's motion) fed
        // the ACTUAL moving vs still inputs -> identical (a real failing model, not base==base:
        // if a regression wired the motion through, these would differ).
        const double gMoving = substanceFieldMagnitude(sdf, probe, 2.0, 1.0, 0.0, 0.0, base, range);
        const double gStill  = substanceFieldMagnitude(sdf, probe, 0.0, 0.0, 0.0, 0.0, base, range);
        const bool negOk = std::abs(gMoving - gStill) < 1e-12;
        const bool ok = dynOk && negOk && fieldStill > 0.0;
        printf("[sdf]   SDF-DYNAMICS: moving stream field=%.3f vs still pool=%.3f (stronger:%d); "
               "NEG geometry-only identical (%.3f==%.3f):%d  %s\n",
               fieldMoving, fieldStill, int(dynOk), gMoving, gStill, int(negOk), ok ? "PASS" : "FAIL");
        allOk = allOk && ok;
    }

    // ---- SDF-PERF: per-frame BANDED splat -- the production near-field. Times the
    // field that is actually CONSUMED (band sized to the avoidance range), asserts
    // it is correct within the band, and degrades gracefully (falloff 0) beyond. ----
    {
        GridSdf sdf = makeGrid(glm::vec3(-1.5f), glm::vec3(3.0f), glm::ivec3(48));
        std::vector<glm::vec3> cloud;
        for (int i = 0; i < 1000; ++i) {
            const float a = 0.5f * float(i), b = 0.31f * float(i);
            cloud.push_back(glm::vec3(0.5f * std::cos(a), 0.3f * std::sin(b), 0.4f * std::cos(b)));
        }
        const float cell = 3.0f / 47.0f;                         // ~0.0638 m node spacing
        const int band = 8;                                      // ~0.51 m -- the avoidance-relevant near field
        const double range = double(band) * double(cell);
        const double base = 1.0;
        const auto t0 = std::chrono::steady_clock::now();
        const int reps = 5;
        for (int n = 0; n < reps; ++n) sdf.build(cloud, r, band);
        const auto t1 = std::chrono::steady_clock::now();
        const double msPerFrame = std::chrono::duration<double, std::milli>(t1 - t0).count() / reps;
        const bool perfOk = msPerFrame < 33.0;                   // < one 30 Hz frame

        // CORRECT within the band: a probe near the cloud reads the analytic distance.
        const glm::vec3 nearProbe = cloud[0] + glm::vec3(0.1f, 0, 0);
        double analytic = 1e9; for (const auto& p : cloud) analytic = std::min(analytic, double(glm::length(nearProbe - p)));
        analytic -= double(r);
        const double nearErr = std::abs(double(sdf.sample(nearProbe)) - analytic);
        const bool nearOk = nearErr < 0.05;
        // GRACEFUL beyond the band: a far probe -> sentinel -> substance field == 0.
        const double farField = substanceFieldMagnitude(sdf, glm::vec3(1.4f, 1.4f, 1.4f), 2.0, 1.0, 1.0, 0.5, base, range);
        const bool farOk = (farField == 0.0);

        const bool ok = perfOk && nearOk && farOk;
        printf("[sdf]   SDF-PERF: 48^3, 1000 particles, band=%d (~%.2fm) -> %.3f ms/frame (<33ms:%d); correct in-band (err %.4f):%d; "
               "graceful sentinel beyond (field=%.1f==0):%d  %s\n",
               band, range, msPerFrame, int(perfOk), nearErr, int(nearOk), farField, int(farOk), ok ? "PASS" : "FAIL");
        allOk = allOk && ok;
    }

    printf("[sdf] %s\n", allOk ? "ALL PASS (analytic distance; away-gradient; dynamics scaling; interactive perf)"
                               : "FAILURES PRESENT");
    fflush(stdout);
    return allOk;
}

} // namespace krs::field
