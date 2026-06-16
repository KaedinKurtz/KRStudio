// SensorGate3.cpp -- Phase 3 GATE IMU-ALLAN. Proves the IMU noise is STATEFUL with a Gauss-Markov bias
// INSTABILITY specifically (not just "something rising at long tau"):
//   (1) the Allan deviation of a stationary series has the white-noise -1/2 slope at short tau AND a
//       bias-instability FLOOR -- proven by SUBTRACTION: removing the GM process drops the floor (min of the
//       full curve is >1.5x the min of a white+random-walk-only model). A static bias contributes ZERO Allan
//       variance, so the floor cannot come from a constant offset either.
//   (2) the integrated output DRIFTS far more than a memoryless model and grows with time;
//   (3) the deterministic systematic errors (scale / axis-misalignment / temp) are recovered on a GENERAL
//       vector (off-diagonal cross-coupling exercised), and ImuModel::fromProfile -- the SHIPPED path -- is
//       covered (field mapping + BMI085 unit conversions).
// NEG-CTRLS: (A) memoryless per-sample Gaussian -> monotone -1/2, no floor; (B) white+random-walk WITHOUT GM
// -> has an interior min from the random walk but its floor is far lower (no instability power) -> FAILS the
// GM-floor test. (B) is the strong impostor a naive "interior-min + rise" test could not exclude.
//
// NOTE: real BMI085 biasTau ~ 200-300 s; this gate uses a COMPRESSED tau so the floor is observable in a
// tractable series. The signature (slope + GM floor + drift) is gated, not the exact floor tau.
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

static double allanMin(const std::vector<std::pair<double, double>>& ad, int& idx) {
    idx = 0; double mv = ad[0].second;
    for (int i = 1; i < int(ad.size()); ++i) if (ad[i].second < mv) { mv = ad[i].second; idx = i; }
    return mv;
}

bool runImuAllanGate() {
    using namespace stats;
    std::printf("\n[SENSOR GATE IMU-ALLAN] stateful Gauss-Markov bias: white slope + GM floor + drift\n");

    // A gyro-like axis with a compressed bias correlation time so the Allan floor is observable.
    auto makeAxis = []() {
        InertialAxis ax;
        ax.rateHz = 400.0; ax.noiseDensity = 0.005;   // white discrete std = 0.005*sqrt(400) = 0.1
        ax.biasInstab = 0.02; ax.biasTau = 2.0;        // GM floor ~ 0.664*0.02 = 0.0133 near tau~2s
        ax.randomWalk = 0.005;                          // rate random walk -> +1/2 rise at long tau
        return ax;
    };
    const double dt = 1.0 / 400.0;
    const int N = 524288;
    // clusters capped at 32768 -> K = N/m >= 16 (drops the under-sampled K<16 tail that made the curve flaky).
    const std::vector<int> clusters = { 1, 2, 4, 8, 16, 32, 64, 128, 256, 512, 1024, 2048, 4096, 8192, 16384, 32768 };

    auto genAllan = [&](bool gm, bool rw, std::uint64_t seed) {
        InertialAxis ax = makeAxis(); ax.enableGM = gm; ax.enableRW = rw;
        std::mt19937_64 r(seed);
        std::vector<double> s(N);
        for (int i = 0; i < N; ++i) s[i] = ax.step(0.0, r);
        return allanDeviation(s, dt, clusters);
    };
    const auto adFull = genAllan(true,  true,  0xA11A4001ull);   // white + GM + RW
    const auto adNoGM = genAllan(false, true,  0xA11A4002ull);   // white + RW (no instability) -- impostor B
    const auto adWhite = genAllan(false, false, 0xA11A4003ull);  // white only -- neg-ctrl A

    // short-tau white slope (first 4 points, tau in [dt, 8dt], well above the floor).
    std::vector<double> stau, devF;
    for (int i = 0; i < 4; ++i) { stau.push_back(adFull[i].first); devF.push_back(adFull[i].second); }
    const double slopeF = powerFit(stau, devF).exponent;

    int iF, iNoGM, iW;
    const double minF = allanMin(adFull, iF);
    const double minNoGM = allanMin(adNoGM, iNoGM);
    const double minW = allanMin(adWhite, iW);
    const double endF = adFull.back().second, endW = adWhite.back().second;
    const int last = int(adFull.size()) - 1;

    const bool t_slope   = std::fabs(slopeF + 0.5) < 0.07;                         // white -1/2 at short tau
    const bool t_gmFloor = minF > 1.5 * minNoGM && iF > 2 && iF < last;            // GM ADDS the floor (interior)
    const bool t_rise    = endF > minF * 1.3;                                      // random walk rises past floor
    const bool t_negNoGM = minNoGM < minF / 1.5;                                   // impostor B: floor too low
    const bool t_negWhite = iW > last / 2 && endW < minW * 1.3 && std::fabs(powerFit(stau,
                              std::vector<double>{adWhite[0].second, adWhite[1].second, adWhite[2].second, adWhite[3].second}).exponent + 0.5) < 0.07;

    std::printf("  allan    : slope=%.3f  minFull=%.4f (tau=%.3gs) minNoGM=%.4f ratio=%.2f end=%.4f -> %s\n",
                slopeF, minF, adFull[iF].first, minNoGM, minF / minNoGM, endF,
                (t_slope && t_gmFloor && t_rise) ? "ok (white -1/2 + GM floor + RW rise)" : "FAIL");
    std::printf("  NEG-B    : white+RW(no GM) minNoGM=%.4f < minFull/1.5=%.4f -> %s\n",
                minNoGM, minF / 1.5, t_negNoGM ? "ok (no instability -> floor collapses)" : "FAIL");
    std::printf("  NEG-A    : white-only minIdx=%d/%d end=%.4f min=%.4f -> %s\n",
                iW, last, endW, minW, t_negWhite ? "ok (monotone -1/2, NO floor)" : "FAIL");

    // ---- integrated drift: stateful >> white, and RMS grows with time (T/4 < T/2 < T). ----
    const int R = 12, Nd = 160000;
    auto driftRuns = [&](bool gm, bool rw, std::uint64_t base, double& rqMid, double& rmsEnd) {
        double s2q = 0, s2e = 0;
        for (int k = 0; k < R; ++k) {
            InertialAxis ax = makeAxis(); ax.enableGM = gm; ax.enableRW = rw;
            std::mt19937_64 r(base + k);
            double acc = 0.0, q = 0.0;
            for (int i = 0; i < Nd; ++i) { acc += ax.step(0.0, r) * dt; if (i == Nd / 4) q = acc; }
            s2q += q * q; s2e += acc * acc;
        }
        rqMid = std::sqrt(s2q / R); rmsEnd = std::sqrt(s2e / R);
    };
    double sQ, sE, wQ, wE;
    driftRuns(true,  true,  0xD1F70000ull, sQ, sE);
    driftRuns(false, false, 0xD1F78000ull, wQ, wE);
    const bool t_drift = sE > 3.0 * wE && sE > sQ;     // stateful diverges; grows from T/4 to T
    std::printf("  drift    : stateful RMS=%.3f (T/4=%.3f) white RMS=%.3f (ratio %.1fx, grows) -> %s\n",
                sE, sQ, wE, wE > 0 ? sE / wE : 0.0, t_drift ? "ok (bias dead-reckon diverges)" : "FAIL");

    // ---- fromProfile coverage (the SHIPPED path): field mapping + BMI085 unit conversions. ----
    SensorProfile prof;
    const ImuModel mp = ImuModel::fromProfile(prof.imu);
    const double kPi = 3.14159265358979;
    const double expGyroND = 0.014 * kPi / 180.0;        // 0.014 dps/sqrt(Hz) -> rad/s/sqrt(Hz) = 2.4435e-4
    const double expAccelND = 160e-6 * 9.80665;          // 160 ug/sqrt(Hz) -> m/s^2/sqrt(Hz) = 1.5691e-3
    const bool t_fromProfile =
        std::fabs(mp.gyro.noiseDensity - expGyroND) < 1e-9 &&
        std::fabs(mp.accel.noiseDensity - expAccelND) < 1e-9 &&
        std::fabs(mp.gyro.biasInstab - 0.0005) < 1e-12 && std::fabs(mp.accel.biasInstab - 0.02) < 1e-12 &&
        std::fabs(mp.gyro.rateHz - 400.0) < 1e-9 && std::fabs(mp.accel.rateHz - 200.0) < 1e-9 &&
        mp.gyro.noiseDensity != mp.accel.noiseDensity;   // not swapped accel<->gyro
    // physical cross-check: white std at tau=dt from the profiled gyro == noiseDensity*sqrt(rate).
    InertialAxis gw = mp.gyro; gw.enableGM = false; gw.enableRW = false;
    std::mt19937_64 gr(0x6789A55ull);
    std::vector<double> gs(200000); for (auto& v : gs) v = gw.step(0.0, gr);
    const double whiteStd = stddev(gs), expWhiteStd = expGyroND * std::sqrt(400.0);
    const bool t_profPhys = std::fabs(whiteStd - expWhiteStd) < 0.05 * expWhiteStd;
    std::printf("  profile  : gyroND=%.4e (exp %.4e) accelND=%.4e (exp %.4e) whiteStd=%.4e/%.4e -> %s\n",
                mp.gyro.noiseDensity, expGyroND, mp.accel.noiseDensity, expAccelND, whiteStd, expWhiteStd,
                (t_fromProfile && t_profPhys) ? "ok (shipped path mapped + units)" : "FAIL");

    // ---- systematic errors on a GENERAL vector (exercises off-diagonal cross-coupling) + temp bias. ----
    ImuModel m; m.scaleFactorErr = 0.01; m.axisMisalignRad = 0.003; m.tempBiasCoeff = 0.001; m.calibTempC = 25.0;
    const glm::dvec3 o = m.applySystematic({ 1.0, 2.0, 3.0 }, 35.0);  // +10 degC; hand-computed below
    // s=1.01, a=0.003, tempBias=0.001*10=0.01:
    //  x = 1.01*1 + 0.003*2 - 0.003*3 + 0.01 = 1.017
    //  y = -0.003*1 + 1.01*2 + 0.003*3 + 0.01 = 2.036
    //  z = 0.003*1 - 0.003*2 + 1.01*3 + 0.01 = 3.037
    const bool t_sys = std::fabs(o.x - 1.017) < 1e-9 && std::fabs(o.y - 2.036) < 1e-9 && std::fabs(o.z - 3.037) < 1e-9;
    ImuModel ideal;
    const glm::dvec3 oi = ideal.applySystematic({ 1.0, 2.0, 3.0 }, 35.0);
    const bool t_negSys = (oi == glm::dvec3(1.0, 2.0, 3.0));
    std::printf("  system   : (1,2,3)@35C -> (%.4f,%.4f,%.4f) vs hand (1.017,2.036,3.037); ideal=identity %s -> %s\n",
                o.x, o.y, o.z, t_negSys ? "yes" : "no", (t_sys && t_negSys) ? "ok" : "FAIL");

    const bool pass = t_slope && t_gmFloor && t_rise && t_negNoGM && t_negWhite && t_drift &&
                      t_fromProfile && t_profPhys && t_sys && t_negSys;
    std::printf("[SENSOR GATE IMU-ALLAN] %s\n", pass ? "PASS" : "FAIL");
    return pass;
}

} // namespace krs::sensor
