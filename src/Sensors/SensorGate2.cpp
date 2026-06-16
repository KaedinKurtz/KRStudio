// SensorGate2.cpp -- Phase 2 GATE DEPTH-STRUCT. Proves the synthetic depth carries the STRUCTURE a real
// stereo sensor shows and a clean+Gaussian model does not:
//   (1) range noise follows the quadratic Z^2 law with the coefficient DERIVED from stereo geometry
//       (subpixelErr / (fx*baseline)), not a free knob;
//   (2) holes are conditioned on the shared material field -- elevated on specular AND dark, ~floor on
//       bright Lambertian;
//   (3) flying pixels appear at depth edges (intermediate depths) but NOT on flat regions;
//   (4) the per-resolution min-Z is enforced.
// NEG-CTRL (clean + constant-Gaussian depth): power-fit exponent ~ 0 (FAILS quadratic) and ~0 holes (FAILS
// material). The Z^2 slope and the material-conditioned dropout are the discriminators.
#include "SensorGates.hpp"
#include "SensorStats.hpp"
#include "SensorProfile.hpp"
#include "DepthModel.hpp"
#include "MaterialField.hpp"
#include <random>
#include <vector>
#include <cmath>
#include <cstdio>

namespace krs::sensor {

static double measuredHoleRate(const DepthModel& m, double Zm, const MaterialSample& mat, int N, std::mt19937_64& rng) {
    std::vector<double> s(N);
    for (int i = 0; i < N; ++i) s[i] = m.sample(Zm, mat, rng);
    return stats::holeRate(s, DEPTH_INVALID);
}

bool runDepthStructGate() {
    using namespace stats;
    std::printf("\n[SENSOR GATE DEPTH-STRUCT] stereo Z^2 range + material holes + flying pixels + min-Z\n");

    SensorProfile prof;
    DepthModel real = DepthModel::fromProfile(prof.depth);
    std::mt19937_64 rng(prof.seed);
    const int N = 60000;

    const MaterialSample bright{ 0.0, 0.85, 1.0 };     // clean Lambertian
    const MaterialSample specular{ 1.0, 0.85, 1.0 };   // mirror-like
    const MaterialSample dark{ 0.0, 0.10, 1.0 };       // black, absorbs IR

    // (1) QUADRATIC range law, isolated (speckle/holes off) so the geometry is clean. Sweep true Z, measure
    //     the depth-error std, power-fit vs Z. Exponent ~ 2 and coefficient ~ subpixelErr/(fx*baseline).
    DepthModel geo = real; geo.addSpeckle = false; geo.materialHoles = false; geo.flyingPixels = false;
    std::vector<double> Zs, sig;
    for (double Z = 1.0; Z <= 6.0; Z += 0.5) {
        std::vector<double> err(N);
        for (int i = 0; i < N; ++i) err[i] = geo.sample(Z, bright, rng) - Z;
        Zs.push_back(Z); sig.push_back(stddev(err));
    }
    const PowerFit pf = powerFit(Zs, sig);
    // INDEPENDENT ground-truth coefficient, hand-computed from the stereo geometry (a fixed LITERAL, NOT read
    // back from the model): coeff = subpixelErr/(fx*baseline) = 0.1/(640*0.095) = 0.00164474 (1/m). Because
    // the model TRIANGULATES (it has no coeff*Z^2 term), emergent-vs-literal is a real physics check: a wrong
    // fx, baseline, or disparity relation moves the emergent coeff away from this fixed number.
    constexpr double HAND_COEFF = 0.00164474;
    const bool t_quadExp   = std::fabs(pf.exponent - 2.0) < 0.05 && pf.r2 > 0.999;   // tight: rejects Z^1.9
    const bool t_quadCoeff = std::fabs(pf.coeff - HAND_COEFF) < 0.06 * HAND_COEFF;
    std::printf("  quadratic: sigma_Z = %.6g * Z^%.3f  r2=%.4f (exp 2.0, coeff vs hand-computed %.6g) -> %s\n",
                pf.coeff, pf.exponent, pf.r2, HAND_COEFF, (t_quadExp && t_quadCoeff) ? "ok" : "FAIL");

    // (1b) speckle adds a constant floor: at a short range the real model's variance exceeds the geometry-only
    //      variance by ~ speckleStd^2.
    DepthModel noSpk = real; noSpk.materialHoles = false; noSpk.flyingPixels = false; noSpk.addSpeckle = false;
    DepthModel spk   = noSpk; spk.addSpeckle = true;
    std::vector<double> a(N), b(N);
    for (int i = 0; i < N; ++i) a[i] = noSpk.sample(1.0, bright, rng) - 1.0;
    for (int i = 0; i < N; ++i) b[i] = spk.sample(1.0, bright, rng) - 1.0;
    const double speckleVar = variance(b) - variance(a);
    const double expSpeckleVar = real.speckleStdM * real.speckleStdM;
    const bool t_speckle = std::fabs(speckleVar - expSpeckleVar) < 0.4 * expSpeckleVar;
    std::printf("  speckle  : added var=%.3e (exp ~ speckleStd^2 %.3e) -> %s\n",
                speckleVar, expSpeckleVar, t_speckle ? "ok" : "FAIL");

    // (2) MATERIAL-CONDITIONED holes at a valid range (Z=2m). Elevated on specular AND dark; ~floor on bright.
    const double hSpec = measuredHoleRate(real, 2.0, specular, N, rng);
    const double hDark = measuredHoleRate(real, 2.0, dark, N, rng);
    const double hBright = measuredHoleRate(real, 2.0, bright, N, rng);
    const bool t_holes = std::fabs(hSpec - double(prof.depth.holeRateSpecular)) < 0.03 &&
                         hDark > 0.20 &&
                         std::fabs(hBright - double(prof.depth.holeRateLambert)) < 0.01;
    std::printf("  holes    : specular=%.3f dark=%.3f bright=%.3f (exp ~0.35, >0.20, ~0.01) -> %s\n",
                hSpec, hDark, hBright, t_holes ? "ok" : "FAIL");

    // (2b) MATERIAL MAPPING (not just a Bernoulli at a fixed rate): the hole rate must follow the SMOOTH
    //      dark-knee. Hand-computed from the intended map drop = 0.01*(1-d)+0.35*d, d=clamp(1-albedo/0.5):
    //      albedo {0.4,0.3,0.2} -> {0.078,0.146,0.214}. A boolean impostor (albedo<0.5 ? 0.35 : 0.01) reads
    //      0.35 at all three and FAILS here -- so this pins the mapping, not just that a Bernoulli works.
    const double albs[3]   = { 0.40, 0.30, 0.20 };
    const double mapExp[3] = { 0.078, 0.146, 0.214 };
    double measMap[3], mapErr = 0.0;
    for (int i = 0; i < 3; ++i) {
        const MaterialSample mm{ 0.0, albs[i], 1.0 };
        measMap[i] = measuredHoleRate(real, 2.0, mm, N, rng);
        mapErr = std::max(mapErr, std::fabs(measMap[i] - mapExp[i]));
    }
    const bool t_mapping = mapErr < 0.015;
    std::printf("  mapping  : albedo .4/.3/.2 holes=%.3f/%.3f/%.3f (smooth exp .078/.146/.214; boolean->.35) -> %s\n",
                measMap[0], measMap[1], measMap[2], t_mapping ? "ok (dark-knee pinned)" : "FAIL");

    // (3) FLYING PIXELS interpolate BETWEEN the two surfaces, so their mean tracks the midpoint (zNear+zFar)/2.
    //     Testing two far depths (3 and 5) pins the INTERPOLATION: the flying-pixel mean must follow zFar (an
    //     arbitrary-elevation model that ignores the far surface would not). And none on a flat region.
    auto flyingStats = [&](double zN, double zF, bool isEdge) {
        std::vector<double> fly; int valid = 0;
        const double margin = zN + 0.02 * (zF - zN);
        for (int i = 0; i < N; ++i) {
            const double z = real.edgeSample(zN, zF, isEdge, bright, rng);
            if (z == DEPTH_INVALID) continue;
            ++valid;
            if (z > margin && z < zF) fly.push_back(z);
        }
        const double frac = valid ? double(fly.size()) / double(valid) : 0.0;
        return std::pair<double, double>{ frac, fly.empty() ? 0.0 : mean(fly) };
    };
    const auto fe3 = flyingStats(1.0, 3.0, true);    // far=3 -> flying mean ~ 2.0
    const auto fe5 = flyingStats(1.0, 5.0, true);    // far=5 -> flying mean ~ 3.0 (tracks zFar)
    const auto fflat = flyingStats(1.0, 3.0, false); // flat region
    const bool t_flying = fe3.first > 0.20 && fe3.first < 0.28 && fflat.first < 0.01 &&
                          std::fabs(fe3.second - 2.0) < 0.1 && std::fabs(fe5.second - 3.0) < 0.1;
    std::printf("  flying   : edge frac=%.3f flat=%.3f; mean(far3)=%.3f mean(far5)=%.3f (track midpoints 2.0/3.0) -> %s\n",
                fe3.first, fflat.first, fe3.second, fe5.second, t_flying ? "ok (interpolated between surfaces)" : "FAIL");

    // (4) MIN-Z enforced: true Z below minZ -> all invalid; above -> valid.
    const double hBelow = measuredHoleRate(geo, real.minZm * 0.6, bright, N, rng);   // 0.6*minZ -> below
    const double hAbove = measuredHoleRate(geo, real.minZm * 1.5, bright, N, rng);   // 1.5*minZ -> above
    const bool t_minz = hBelow > 0.99 && hAbove < 0.01;
    std::printf("  min-Z    : below(%.2fm)=%.3f above(%.2fm)=%.3f (exp ~1.0, ~0.0) -> %s\n",
                real.minZm * 0.6, hBelow, real.minZm * 1.5, hAbove, t_minz ? "ok" : "FAIL");

    // ---- NEG-CTRL: clean + constant-Gaussian depth. No Z^2 (flat power fit) AND no material holes. ----
    DepthModel neg = real; neg.quadraticRange = false; neg.materialHoles = false; neg.flyingPixels = false; neg.addSpeckle = false;
    std::vector<double> nsig;
    for (double Z = 1.0; Z <= 6.0; Z += 0.5) {
        std::vector<double> err(N);
        for (int i = 0; i < N; ++i) err[i] = neg.sample(Z, bright, rng) - Z;
        nsig.push_back(stddev(err));
    }
    const PowerFit npf = powerFit(Zs, nsig);
    const double negHoleSpec = measuredHoleRate(neg, 2.0, specular, N, rng);
    const bool t_negQuad  = std::fabs(npf.exponent) < 0.1;          // flat: no Z dependence
    const bool t_negHoles = negHoleSpec < 0.01;                     // no material dropout
    std::printf("  NEG-CTRL : const-gaussian exp=%.3f (exp ~0, FAILS quadratic) specular-holes=%.3f (~0) -> %s\n",
                npf.exponent, negHoleSpec, (t_negQuad && t_negHoles) ? "ok (clean sensor has neither)" : "FAIL");

    const bool pass = t_quadExp && t_quadCoeff && t_speckle && t_holes && t_mapping && t_flying && t_minz && t_negQuad && t_negHoles;
    std::printf("[SENSOR GATE DEPTH-STRUCT] %s\n", pass ? "PASS" : "FAIL");
    return pass;
}

} // namespace krs::sensor
