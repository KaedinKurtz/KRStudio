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
    std::vector<double> rsig, rvar; double rgbConsErr = 0, rgbBiasErr = 0;
    const int M = 80000;
    for (double S = 30.0; S <= 200.0; S += 17.0) {
        std::vector<double> o(M); for (auto& v : o) v = rgb.apply(S, rR);
        rsig.push_back(S); rvar.push_back(variance(o));
        const double mn = mean(o);
        rgbConsErr = std::max(rgbConsErr, std::fabs(mn - S));               // unbiased -> ~0
        rgbBiasErr = std::max(rgbBiasErr, std::fabs(mn + 5.0 - S));         // a +5 DN pedestal would NOT conserve
    }
    const double rgbSlope = linearFit(rsig, rvar).slope;
    const bool t_rgb = std::fabs(rgbSlope - 1.0 / double(prof.rgb.photonsPerDN)) < 0.05 / double(prof.rgb.photonsPerDN);
    std::printf("  rgb       : var-slope=%.5f (exp %.5f) ; conservation err=%.3f DN (biased would be %.2f) -> %s\n",
                rgbSlope, 1.0 / double(prof.rgb.photonsPerDN), rgbConsErr, rgbBiasErr, t_rgb ? "ok" : "FAIL");

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

    // ---- CONSERVATION verdict + DETERMINISM (same seed -> byte-identical RGB stream). ----
    const bool t_conserve = rgbConsErr < 0.5 && rgbBiasErr > 1.0 && depConsErr < 0.005;
    std::printf("  conserve  : unbiased (rgb %.3f<0.5, depth %.4f<0.005) ; biased pedestal FAILS (%.2f) -> %s\n",
                rgbConsErr, depConsErr, rgbBiasErr, t_conserve ? "ok (true signal preserved)" : "FAIL");
    std::mt19937_64 da(sRgb), db(sRgb); bool identical = true;
    for (int i = 0; i < 5000; ++i) if (rgb.apply(120.0, da) != rgb.apply(120.0, db)) { identical = false; break; }
    std::printf("  determ    : same-seed RGB stream byte-identical=%s -> %s\n", identical ? "yes" : "no", identical ? "ok" : "FAIL");

    const bool pass = t_rgb && t_depth && t_imu && t_conserve && identical;
    std::printf("[SENSOR GATE E2E] %s\n", pass ? "PASS" : "FAIL");
    return pass;
}

bool runRealTransferGate() {
    std::printf("\n[SENSOR GATE REAL-TRANSFER] transfer harness -- SELF-CONSISTENCY ONLY (NOT real hardware)\n");
    std::printf("  HONESTY  : %s\n", transferHonestyLabel());

    SensorProfile prof;
    // Two independent SYNTHETIC instances of the SAME profile (different seed salts) -> must MATCH.
    const SensorFingerprint A = computeFingerprint(prof, 0xA0A0u);
    const SensorFingerprint B = computeFingerprint(prof, 0xB0B0u);
    const TransferComparison ab = compareFingerprints(A, B, /*relTol=*/0.12, /*histTol=*/0.15);
    std::printf("  self-cons : A vs B (same profile, diff seed) maxRelDiff=%.4f histL1=%.4f -> %s\n",
                ab.maxRelDiff, ab.histL1, ab.match ? "MATCH (self-consistent)" : "mismatch");

    // A genuinely DIFFERENT sensor (perturbed profile) -> must MISMATCH (proves discriminating power).
    SensorProfile prof2 = prof;
    prof2.rgb.photonsPerDN = 15.0;          // half the gain -> doubles the shot slope
    prof2.depth.holeRateSpecular = 0.55;    // different dropout
    prof2.imu.gyroNoiseDensity = double(prof.imu.gyroNoiseDensity) * 1.6;   // noisier gyro
    const SensorFingerprint C = computeFingerprint(prof2, 0xC0C0u);
    const TransferComparison ac = compareFingerprints(A, C, 0.12, 0.15);
    std::printf("  discrim   : A vs C (perturbed profile) maxRelDiff=%.4f histL1=%.4f -> %s\n",
                ac.maxRelDiff, ac.histL1, ac.match ? "MATCH (BAD -- not discriminating)" : "mismatch (detects a different sensor)");

    const bool pass = ab.match && !ac.match;
    std::printf("  verdict   : self-consistent=%s discriminating=%s ; sim-to-real transfer remains UNVALIDATED -> %s\n",
                ab.match ? "yes" : "no", !ac.match ? "yes" : "no", pass ? "PASS (proxy only)" : "FAIL");
    std::printf("[SENSOR GATE REAL-TRANSFER] %s\n", pass ? "PASS" : "FAIL");
    return pass;
}

} // namespace krs::sensor
