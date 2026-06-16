// SensorGate3.cpp -- Phase 3 GATE IMU-ALLAN. Proves the IMU noise is STATEFUL, which a per-sample Gaussian
// cannot be:
//   (1) the Allan deviation of a long stationary series has the white-noise -1/2 slope at short tau AND a
//       bias-instability FLOOR (an interior minimum after which it rises with the rate random walk);
//   (2) the integrated output DRIFTS -- RMS |integral| is far larger than the white-only model's, and it
//       grows with time (the VIO-killer: a dead-reckon diverges);
//   (3) the deterministic systematic errors (scale / axis-misalignment / temperature bias) are recovered.
// NEG-CTRL (memoryless per-sample Gaussian): the Allan deviation is monotone -1/2 with NO interior floor, and
// the integrated drift is small. The Allan floor and the drift both REQUIRE carried state.
//
// NOTE: real BMI085 biasTau ~ 200-300 s; this gate uses a COMPRESSED tau so the floor is observable in a
// tractable series. The signature (slope + floor + drift) is what's gated, not the exact floor tau.
#include "SensorGates.hpp"
#include "SensorStats.hpp"
#include "SensorProfile.hpp"
#include "ImuModel.hpp"
#include <glm/glm.hpp>
#include <random>
#include <vector>
#include <cmath>
#include <cstdio>
#include <algorithm>

namespace krs::sensor {

bool runImuAllanGate() {
    using namespace stats;
    std::printf("\n[SENSOR GATE IMU-ALLAN] stateful bias: white slope + instability floor + integrated drift\n");

    // A gyro-like axis with a compressed bias correlation time so the Allan floor is observable.
    auto makeAxis = []() {
        InertialAxis ax;
        ax.rateHz = 400.0; ax.noiseDensity = 0.005;   // white discrete std = 0.005*sqrt(400) = 0.1
        ax.biasInstab = 0.02; ax.biasTau = 2.0;        // GM floor ~ 0.664*0.02 = 0.0133 near tau~2s
        ax.randomWalk = 0.005;                          // rate random walk -> +1/2 rise at long tau
        return ax;
    };
    const double dt = 1.0 / 400.0;
    const int N = 262144;

    // ---- stateful series for the Allan analysis ----
    InertialAxis axS = makeAxis();
    std::mt19937_64 rng(0xA11A4001ull);
    std::vector<double> series(N);
    for (int i = 0; i < N; ++i) series[i] = axS.step(0.0, rng);

    InertialAxis axW = makeAxis();
    std::vector<double> wseries(N);
    for (int i = 0; i < N; ++i) wseries[i] = axW.stepWhiteOnly(0.0, rng);

    const std::vector<int> clusters = { 1, 2, 4, 8, 16, 32, 64, 128, 256, 512, 1024, 2048, 4096, 8192, 16384, 32768, 65536 };
    const auto adS = allanDeviation(series, dt, clusters);
    const auto adW = allanDeviation(wseries, dt, clusters);

    // short-tau slope (white-noise -1/2): fit first 4 points (tau in [dt, 8*dt], well above the floor).
    std::vector<double> stau, sdevS, sdevW;
    for (size_t i = 0; i < adS.size() && i < 4; ++i) { stau.push_back(adS[i].first); sdevS.push_back(adS[i].second); }
    for (size_t i = 0; i < adW.size() && i < 4; ++i) sdevW.push_back(adW[i].second);
    const double slopeS = powerFit(stau, sdevS).exponent;
    const double slopeW = powerFit(stau, sdevW).exponent;

    // bias-instability floor: an INTERIOR minimum, with a rise afterwards (random walk).
    auto minInfo = [](const std::vector<std::pair<double, double>>& ad) {
        int mi = 0; double mv = ad[0].second;
        for (int i = 1; i < int(ad.size()); ++i) if (ad[i].second < mv) { mv = ad[i].second; mi = i; }
        return std::pair<int, double>{ mi, mv };
    };
    const auto mS = minInfo(adS);
    const auto mW = minInfo(adW);
    const double endS = adS.back().second, endW = adW.back().second;
    const int last = int(adS.size()) - 1;

    const bool t_slopeS    = std::fabs(slopeS + 0.5) < 0.07;                 // stateful white slope ~ -0.5
    const bool t_floor     = mS.first > 2 && mS.first < last - 1 && endS > mS.second * 1.3;  // interior min + rise
    // NEG: white keeps decreasing (min in the latter half) with NO significant rise after it (end ~ min). The
    // discriminator is the RISE: stateful rises >1.3x past its floor (random walk); white does not.
    const bool t_negNoFloor = mW.first > last / 2 && endW < mW.second * 1.3 && std::fabs(slopeW + 0.5) < 0.07;

    std::printf("  allan(SF): slope=%.3f minTau=%.3gs floor=%.4f endTau=%.3gs end=%.4f -> %s\n",
                slopeS, adS[mS.first].first, mS.second, adS[last].first, endS,
                (t_slopeS && t_floor) ? "ok (white -1/2 + interior floor + rise)" : "FAIL");
    std::printf("  allan(NEG): slope=%.3f minIdx=%d/%d end=%.4f min=%.4f -> %s\n",
                slopeW, mW.first, last, endW, mW.second, t_negNoFloor ? "ok (monotone -1/2, NO floor)" : "FAIL");

    // ---- integrated drift: RMS |integral| over R runs; stateful >> white, and it grows with time. ----
    const int R = 12, Nd = 120000;   // 300 s per run
    auto integ = [&](bool stateful, std::uint64_t seed, double& halfMag, double& endMag) {
        InertialAxis ax = makeAxis();
        std::mt19937_64 r(seed);
        double acc = 0.0; halfMag = 0.0;
        for (int i = 0; i < Nd; ++i) {
            acc += (stateful ? ax.step(0.0, r) : ax.stepWhiteOnly(0.0, r)) * dt;
            if (i == Nd / 2) halfMag = std::fabs(acc);
        }
        endMag = std::fabs(acc);
    };
    double sumS2 = 0, sumW2 = 0, growS = 0; int grew = 0;
    for (int k = 0; k < R; ++k) {
        double hS, eS, hW, eW;
        integ(true,  0xD1F70000ull + k, hS, eS);
        integ(false, 0xD1F78000ull + k, hW, eW);
        sumS2 += eS * eS; sumW2 += eW * eW; growS += eS;
        if (eS > hS) ++grew;                         // end magnitude exceeds half-time magnitude (growing)
    }
    const double rmsS = std::sqrt(sumS2 / R), rmsW = std::sqrt(sumW2 / R);
    const bool t_drift = rmsS > 3.0 * rmsW && grew >= (R * 2) / 3;
    std::printf("  drift    : stateful RMS=%.4f white RMS=%.4f (ratio %.1fx, exp >3) grew %d/%d -> %s\n",
                rmsS, rmsW, rmsW > 0 ? rmsS / rmsW : 0.0, grew, R, t_drift ? "ok (bias dead-reckon diverges)" : "FAIL");

    // ---- systematic errors (deterministic): scale / misalignment / temp bias recovered; ideal model = identity. ----
    ImuModel m;
    m.scaleFactorErr = 0.01; m.axisMisalignRad = 0.003; m.tempBiasCoeff = 0.001; m.calibTempC = 25.0;
    const glm::dvec3 ox = m.applySystematic({ 1.0, 0.0, 0.0 }, 25.0);      // at calib temp: no temp bias
    const glm::dvec3 ot = m.applySystematic({ 0.0, 0.0, 0.0 }, 35.0);      // +10 degC on a null input
    ImuModel ideal; // all-zero systematic
    const glm::dvec3 oi = ideal.applySystematic({ 1.0, 2.0, 3.0 }, 35.0);
    const bool t_scale  = std::fabs(ox.x - 1.01) < 1e-9;
    const bool t_misal  = std::fabs(ox.y + 0.003) < 1e-9 && std::fabs(ox.z - 0.003) < 1e-9;
    const bool t_temp   = std::fabs(ot.x - 0.001 * 10.0) < 1e-9;
    const bool t_negSys = (oi == glm::dvec3(1.0, 2.0, 3.0));
    std::printf("  system   : scale x=%.4f (1.01) misalign y=%.4f z=%.4f (-/+0.003) temp=%.4f (0.01); ideal=identity %s -> %s\n",
                ox.x, ox.y, ox.z, ot.x, t_negSys ? "yes" : "no",
                (t_scale && t_misal && t_temp && t_negSys) ? "ok" : "FAIL");

    const bool pass = t_slopeS && t_floor && t_negNoFloor && t_drift && t_scale && t_misal && t_temp && t_negSys;
    std::printf("[SENSOR GATE IMU-ALLAN] %s\n", pass ? "PASS" : "FAIL");
    return pass;
}

} // namespace krs::sensor
