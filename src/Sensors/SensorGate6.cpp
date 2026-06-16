// SensorGate6.cpp -- Phase 6 (FINAL): GATE E2E + GATE REAL-TRANSFER.
//
// GATE E2E: one scene/profile/seed drives all three sensors; the master seed is split into RGB/depth/IMU
// substreams (fixed order). Each stream passes its OWN gate's signature IN-CONTEXT (RGB shot slope, depth Z^2,
// IMU white slope + drift), the noise is UNBIASED so the ensemble mean conserves the true signal/depth/rate
// (a biased pedestal model FAILS), and the streams are byte-deterministic per seed.
//
// GATE REAL-TRANSFER -- THE HONEST LIMIT. A real sim-to-real transfer claim needs a real D456 capture, which
// is NOT available. The transfer harness is therefore gated ONLY against a SECOND SYNTHETIC instance: two
// independent draws of the same profile MATCH (self-consistency), and a perturbed profile MISMATCHES
// (discriminating power). This proves the comparison works; it does NOT prove sim-to-real transfer. The gate
// prints the honesty label every run. NOTHING here may be read as evidence the synthetic sensor matches reality.
#include "SensorGates.hpp"
#include "SensorStats.hpp"
#include "SensorProfile.hpp"
#include "RgbNoise.hpp"
#include "DepthModel.hpp"
#include "MaterialField.hpp"
#include "ImuModel.hpp"
#include "TransferHarness.hpp"
#include <random>
#include <vector>
#include <cmath>
#include <cstdio>

namespace krs::sensor {

bool runE2EGate() {
    using namespace stats;
    std::printf("\n[SENSOR GATE E2E] one scene -> RGB+depth+IMU; in-context signatures + conservation + determinism\n");

    SensorProfile prof;
    std::mt19937_64 master(prof.seed);
    const std::uint64_t sRgb = master(), sDepth = master(), sImu = master();   // fixed-order substreams

    // ---- RGB stream: shot variance-vs-signal slope (1/photonsPerDN); UNBIASED mean conserves the signal. ----
    const RgbNoiseModel rgb = RgbNoiseModel::fromRgb(prof.rgb);
    std::mt19937_64 rR(sRgb);
    std::vector<double> rsig, rvar; double rgbConsErr = 0;
    const int M = 80000;
    for (double S = 30.0; S <= 200.0; S += 17.0) {
        std::vector<double> o(M); for (auto& v : o) v = rgb.apply(S, rR);
        rsig.push_back(S); rvar.push_back(variance(o));
        rgbConsErr = std::max(rgbConsErr, std::fabs(mean(o) - S));          // unbiased -> ~0
    }
    const double rgbSlope = linearFit(rsig, rvar).slope;
    const bool t_rgb = std::fabs(rgbSlope - 1.0 / double(prof.rgb.photonsPerDN)) < 0.05 / double(prof.rgb.photonsPerDN);
    std::printf("  rgb       : var-slope=%.5f (exp %.5f) ; unbiased mean err=%.3f DN -> %s\n",
                rgbSlope, 1.0 / double(prof.rgb.photonsPerDN), rgbConsErr, t_rgb ? "ok" : "FAIL");

    // ---- Depth stream: sigma-vs-Z power exponent (~2); UNBIASED mean conserves depth. ----
    DepthModel dep = DepthModel::fromProfile(prof.depth);
    dep.addSpeckle = false; dep.materialHoles = false; dep.flyingPixels = false;
    std::mt19937_64 rD(sDepth);
    const MaterialSample bright{ 0.0, 0.85, 1.0 };
    std::vector<double> Zs, sg; double depConsErr = 0;
    const int N = 60000;
    for (double Z = 1.0; Z <= 6.0; Z += 0.5) {
        std::vector<double> z(N); for (int i = 0; i < N; ++i) z[i] = dep.sample(Z, bright, rD);
        Zs.push_back(Z); std::vector<double> e(N); for (int i = 0; i < N; ++i) e[i] = z[i] - Z;
        sg.push_back(stddev(e));
        depConsErr = std::max(depConsErr, std::fabs(mean(z) - Z) / Z);     // unbiased -> ~0 (relative)
    }
    const double depExp = powerFit(Zs, sg).exponent;
    const bool t_depth = std::fabs(depExp - 2.0) < 0.06 && depConsErr < 0.005;
    std::printf("  depth     : sigma~Z^%.3f (exp 2.0) ; conservation rel-err=%.4f -> %s\n",
                depExp, depConsErr, t_depth ? "ok" : "FAIL");

    // ---- IMU stream: white slope (~-0.5) + stateful drift >> white-only; UNBIASED stationary mean ~ 0. ----
    const ImuModel imu = ImuModel::fromProfile(prof.imu);
    std::mt19937_64 rI(sImu);
    InertialAxis gw = imu.gyro; gw.enableGM = false; gw.enableRW = false;
    const double dt = 1.0 / double(prof.imu.gyroRateHz);
    std::vector<double> series(120000); for (auto& v : series) v = gw.step(0.0, rI);
    const auto ad = allanDeviation(series, dt, { 1, 2, 4, 8 });
    std::vector<double> tau, dev; for (const auto& p : ad) { tau.push_back(p.first); dev.push_back(p.second); }
    const double imuSlope = powerFit(tau, dev).exponent;
    const double imuMean = mean(series);                                   // stationary, unbiased -> ~0
    // drift: stateful (full) vs white-only over a tractable run (compressed tau).
    auto driftRms = [&](bool stateful) {
        double s2 = 0; const int Rr = 6, Nd = 60000;
        for (int k = 0; k < Rr; ++k) {
            InertialAxis ax = imu.gyro; ax.biasTau = 2.0; ax.enableGM = stateful; ax.enableRW = stateful;
            std::mt19937_64 rr(sImu ^ (0xD717u + k));
            double acc = 0; for (int i = 0; i < Nd; ++i) acc += ax.step(0.0, rr) * dt;
            s2 += acc * acc;
        }
        return std::sqrt(s2 / Rr);
    };
    const double dS = driftRms(true), dW = driftRms(false);
    const bool t_imu = std::fabs(imuSlope + 0.5) < 0.08 && std::fabs(imuMean) < 0.01 * gw.noiseDensity * std::sqrt(gw.rateHz) + 1e-4 && dS > 3.0 * dW;
    std::printf("  imu       : white-slope=%.3f drift stateful=%.4f white=%.4f (%.1fx) ; stationary mean=%.2e -> %s\n",
                imuSlope, dS, dW, dS / dW, imuMean, t_imu ? "ok" : "FAIL");

    // ---- SHARED SCENE: ONE geometry+material observed by BOTH depth (geometry) and RGB (reflectance). The
    //      two streams co-observe the same world: dark cells are dim in RGB AND drop in depth (shared
    //      material), so the per-cell RGB brightness and depth valid-rate correlate. (The IMU is ego-motion,
    //      not a scene observer, so it shares only the seed lineage -- physically correct.) ----
    DepthModel depScene = DepthModel::fromProfile(prof.depth);     // holes ON
    std::mt19937_64 rS(sRgb ^ 0x5CE2Eu);
    std::vector<double> cellRgb, cellValid;
    const int SW = 40, SH = 30;
    for (int v = 0; v < SH; ++v)
        for (int u = 0; u < SW; ++u) {
            const bool dark = (u >= 12 && u < 24 && v >= 8 && v < 22);
            const double alb = dark ? 0.12 : 0.85;
            const MaterialSample mat{ 0.0, alb, 1.0 };
            const double sigDN = alb * double(prof.rgb.fullScaleDN);    // reflectance -> RGB signal
            double accR = 0; for (int t = 0; t < 200; ++t) accR += rgb.apply(sigDN, rS);
            int valid = 0; for (int t = 0; t < 200; ++t) if (depScene.sample(1.5, mat, rS) != DEPTH_INVALID) ++valid;
            cellRgb.push_back(accR / 200.0); cellValid.push_back(valid / 200.0);
        }
    const LinFit sceneFit = linearFit(cellRgb, cellValid);
    const bool t_scene = sceneFit.slope > 0 && sceneFit.r2 > 0.5;
    std::printf("  scene     : RGB-brightness vs depth-valid-rate corr slope>0 r2=%.3f (one shared scene) -> %s\n",
                sceneFit.r2, t_scene ? "ok (RGB+depth observe the same world)" : "FAIL");

    // ---- CONSERVATION through the model bias knob + DETERMINISM across all three streams. ----
    RgbNoiseModel biased = rgb; biased.biasDN = 5.0;
    std::mt19937_64 rc(sRgb ^ 0xC04Eu);
    std::vector<double> ub(40000), bi(40000);
    for (auto& v : ub) v = rgb.apply(120.0, rc);
    for (auto& v : bi) v = biased.apply(120.0, rc);
    const double ubErr = std::fabs(mean(ub) - 120.0), biErr = std::fabs(mean(bi) - 120.0);
    const bool t_conserve = rgbConsErr < 0.5 && depConsErr < 0.005 && ubErr < 0.3 && biErr > 4.0;
    std::printf("  conserve  : unbiased mean err=%.3f (depth %.4f) ; biased MODEL mean err=%.3f (>4, FAILS) -> %s\n",
                ubErr, depConsErr, biErr, t_conserve ? "ok (true signal preserved; biased model breaks it)" : "FAIL");

    bool identRgb = true, identDepth = true, identImu = true;
    { std::mt19937_64 a(sRgb), b(sRgb); for (int i = 0; i < 4000; ++i) if (rgb.apply(120.0, a) != rgb.apply(120.0, b)) { identRgb = false; break; } }
    { std::mt19937_64 a(sDepth), b(sDepth); DepthModel d = DepthModel::fromProfile(prof.depth);
      for (int i = 0; i < 4000; ++i) if (d.sample(2.0, bright, a) != d.sample(2.0, bright, b)) { identDepth = false; break; } }
    { std::mt19937_64 a(sImu), b(sImu); InertialAxis x = imu.gyro, y = imu.gyro;
      for (int i = 0; i < 4000; ++i) if (x.step(0.0, a) != y.step(0.0, b)) { identImu = false; break; } }
    const bool identical = identRgb && identDepth && identImu;
    std::printf("  determ    : same-seed byte-identical rgb=%s depth=%s imu=%s -> %s\n",
                identRgb ? "y" : "n", identDepth ? "y" : "n", identImu ? "y" : "n", identical ? "ok" : "FAIL");

    const bool pass = t_rgb && t_depth && t_imu && t_scene && t_conserve && identical;
    std::printf("[SENSOR GATE E2E] %s\n", pass ? "PASS" : "FAIL");
    return pass;
}

bool runRealTransferGate() {
    std::printf("\n[SENSOR GATE REAL-TRANSFER] transfer harness -- SELF-CONSISTENCY ONLY (NOT real hardware)\n");
    std::printf("  HONESTY  : %s\n", transferHonestyLabel());

    // SCOPE: the fingerprint reflects RGB gain/read, depth Z^2 + hole rate, IMU white-noise + bias-instability
    // floor (measured at a COMPRESSED biasTau, so a difference in the PROFILED biasTau is NOT captured -- a
    // known blind spot of this proxy). It is also a SELF-CONSISTENCY check: A~B matching proves the synthetic
    // pipeline is reproducible across seeds, NOT that it is faithful to real hardware.
    std::printf("  scope     : fingerprint blind to profiled biasTau (compressed for observability); A~B is reproducibility, NOT fidelity\n");

    SensorProfile prof;
    // Two independent SYNTHETIC instances of the SAME profile (different seed salts) -> must MATCH. Tolerance
    // 0.05 is ~8x the measured A~B seed-noise floor, so a ~>=5% sensor difference is detectable (not just gross).
    const SensorFingerprint A = computeFingerprint(prof, 0xA0A0u);
    const SensorFingerprint B = computeFingerprint(prof, 0xB0B0u);
    const TransferComparison ab = compareFingerprints(A, B, /*relTol=*/0.05, /*histTol=*/0.08);
    std::printf("  self-cons : A vs B (same profile, diff seed) maxRelDiff=%.4f histL1=%.4f -> %s\n",
                ab.maxRelDiff, ab.histL1, ab.match ? "MATCH (self-consistent)" : "mismatch");

    // A genuinely DIFFERENT sensor (perturbed profile) -> must MISMATCH (proves discriminating power).
    SensorProfile prof2 = prof;
    prof2.rgb.photonsPerDN = 15.0;          // half the gain -> doubles the shot slope
    prof2.depth.holeRateSpecular = 0.55;    // different dropout
    prof2.imu.gyroNoiseDensity = double(prof.imu.gyroNoiseDensity) * 1.6;   // noisier gyro
    const SensorFingerprint C = computeFingerprint(prof2, 0xC0C0u);
    const TransferComparison ac = compareFingerprints(A, C, 0.05, 0.08);
    std::printf("  discrim   : A vs C (perturbed profile) maxRelDiff=%.4f histL1=%.4f -> %s\n",
                ac.maxRelDiff, ac.histL1, ac.match ? "MATCH (BAD -- not discriminating)" : "mismatch (detects a different sensor)");

    const bool pass = ab.match && !ac.match;
    std::printf("  verdict   : self-consistent=%s discriminating=%s ; sim-to-real transfer remains UNVALIDATED -> %s\n",
                ab.match ? "yes" : "no", !ac.match ? "yes" : "no", pass ? "PASS (proxy only)" : "FAIL");
    std::printf("[SENSOR GATE REAL-TRANSFER] %s\n", pass ? "PASS" : "FAIL");
    return pass;
}

} // namespace krs::sensor
