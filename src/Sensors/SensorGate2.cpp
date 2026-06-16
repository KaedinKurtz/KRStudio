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
    const double derivedCoeff = double(prof.depth.subpixelErr) / (geo.fx * geo.baselineM);   // 1/m
    const bool t_quadExp   = std::fabs(pf.exponent - 2.0) < 0.1 && pf.r2 > 0.999;
    const bool t_quadCoeff = std::fabs(pf.coeff - derivedCoeff) < 0.10 * derivedCoeff;
    std::printf("  quadratic: sigma_Z = %.5g * Z^%.3f  r2=%.4f (exp 2.0, coeff %.5g derived) -> %s\n",
                pf.coeff, pf.exponent, pf.r2, derivedCoeff, (t_quadExp && t_quadCoeff) ? "ok" : "FAIL");

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

    // (3) FLYING PIXELS at a depth edge (zNear=1, zFar=3): a fraction land at INTERMEDIATE depth. None on a flat.
    const double zNear = 1.0, zFar = 3.0;
    const double gapMargin = zNear + 0.02 * (zFar - zNear);   // clearly above foreground range-noise
    auto flyingFraction = [&](bool isEdge) {
        int valid = 0, flying = 0;
        for (int i = 0; i < N; ++i) {
            const double z = real.edgeSample(zNear, zFar, isEdge, bright, rng);
            if (z == DEPTH_INVALID) continue;
            ++valid;
            if (z > gapMargin && z < zFar) ++flying;
        }
        return valid ? double(flying) / double(valid) : 0.0;
    };
    const double flyEdge = flyingFraction(true);
    const double flyFlat = flyingFraction(false);
    const bool t_flying = flyEdge > 0.20 && flyEdge < 0.28 && flyFlat < 0.01;
    std::printf("  flying   : edge=%.3f flat=%.3f (exp ~%.2f at edge, ~0 on flat) -> %s\n",
                flyEdge, flyFlat, real.flyingPixelRate, t_flying ? "ok" : "FAIL");

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

    const bool pass = t_quadExp && t_quadCoeff && t_speckle && t_holes && t_flying && t_minz && t_negQuad && t_negHoles;
    std::printf("[SENSOR GATE DEPTH-STRUCT] %s\n", pass ? "PASS" : "FAIL");
    return pass;
}

} // namespace krs::sensor
