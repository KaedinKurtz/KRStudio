// SensorGate1.cpp -- Phase 1 RGB gates.
//
// GATE INTRINSICS: K is derived from focal length + pixel pitch (cross-checked vs the FOV formula), and a
//   >=50-point grid spanning the full FOV (incl. corners) round-trips undistorted->pixel->undistorted to
//   <0.5px. NEG-CTRL: a pinhole model (zero distortion) unprojecting the SAME lens-distorted pixels has
//   error that is ~0 on-axis but GROWS toward the corners and exceeds 2px -- proving the distortion model
//   does real work and the gate is sensitive specifically to the edge behavior a clean pinhole gets wrong.
//
// GATE NOISE-STATS: sweeping a constant signal S and measuring per-pixel variance, the shot+read stack is
//   AFFINE in S: slope == 1/photonsPerDN (Poisson, signal-dependent), intercept == readNoiseDN^2 (the read
//   floor). NEG-CTRL: a fixed-Gaussian with the SAME total variance at mid-signal has slope ~0 -- it FAILS
//   the signal-dependence test. The slope is the discriminator; matching at one operating point is not enough.
#include "SensorGates.hpp"
#include "SensorStats.hpp"
#include "SensorProfile.hpp"
#include "CameraModel.hpp"
#include "RgbNoise.hpp"
#include <glm/glm.hpp>
#include <random>
#include <vector>
#include <cmath>
#include <cstdio>

namespace krs::sensor {

bool runRgbIntrinsicsGate() {
    using namespace stats;
    std::printf("\n[SENSOR GATE INTRINSICS] pinhole K + Brown-Conrady round-trip\n");

    SensorProfile prof;
    const Intrinsics K = Intrinsics::fromRgb(prof.rgb);
    const Distortion D = Distortion::fromRgb(prof.rgb);
    const Distortion P = Distortion::none();   // pinhole neg-ctrl

    // K cross-check: fx from focal+pitch must match (W/2)/tan(fovH/2) from the datasheet FOV.
    const double fxFromFov = (double(int(prof.rgb.width)) * 0.5) / std::tan(0.5 * double(prof.rgb.fovHdeg) * 3.14159265358979 / 180.0);
    const bool t_kmatch = std::fabs(K.fx - fxFromFov) < 0.5 && std::fabs(K.fx - 640.0) < 1.0;
    std::printf("  K        : fx=%.3f fy=%.3f cx=%.1f cy=%.1f  (fx_from_FOV=%.3f) -> %s\n",
                K.fx, K.fy, K.cx, K.cy, fxFromFov, t_kmatch ? "ok" : "FAIL");

    // >=50-point grid over the full FOV (normalized half-extents = tan(halfFOV)), corners included.
    const double xmax = std::tan(0.5 * double(prof.rgb.fovHdeg) * 3.14159265358979 / 180.0);
    const double ymax = std::tan(0.5 * double(prof.rgb.fovVdeg) * 3.14159265358979 / 180.0);
    const int N = 9;   // 9x9 = 81 points
    double maxFull = 0.0, maxPinholeCorner = 0.0, maxPinholeCenter = 0.0, cornerR = 0.0;
    int count = 0;
    for (int iy = 0; iy < N; ++iy) {
        for (int ix = 0; ix < N; ++ix) {
            const double nx = -xmax + 2.0 * xmax * ix / (N - 1);
            const double ny = -ymax + 2.0 * ymax * iy / (N - 1);
            const glm::dvec2 nTrue{ nx, ny };
            const glm::dvec2 px = project(nTrue, K, D);                       // true lens pixel
            const glm::dvec2 nFull = unproject(px, K, D);                     // full-model recovery
            const glm::dvec2 nPin  = unproject(px, K, P);                     // pinhole recovery (neg-ctrl)
            const double errFull = K.fx * glm::length(nTrue - nFull);        // round-trip error, px
            const double errPin  = K.fx * glm::length(nTrue - nPin);         // pinhole error, px
            maxFull = std::max(maxFull, errFull);
            const double rad = std::sqrt(nx * nx + ny * ny);
            if (rad < 0.15 * std::sqrt(xmax * xmax + ymax * ymax)) maxPinholeCenter = std::max(maxPinholeCenter, errPin);
            if (ix == 0 && iy == 0) { maxPinholeCorner = std::max(maxPinholeCorner, errPin); cornerR = rad; }
            if ((ix == 0 || ix == N - 1) && (iy == 0 || iy == N - 1)) maxPinholeCorner = std::max(maxPinholeCorner, errPin);
            ++count;
        }
    }
    const bool t_roundtrip = count >= 50 && maxFull < 0.5;
    const bool t_negEdge   = maxPinholeCorner > 2.0;       // pinhole diverges at the corners
    const bool t_negCenter = maxPinholeCenter < 0.5;       // ...but agrees on-axis (distortion vanishes there)
    std::printf("  roundtrip: %d pts, full-model max err=%.4f px (<0.5) -> %s\n", count, maxFull, t_roundtrip ? "ok" : "FAIL");
    std::printf("  NEG-CTRL : pinhole corner err=%.3f px (>2) center err=%.4f px (<0.5) -> %s\n",
                maxPinholeCorner, maxPinholeCenter, (t_negEdge && t_negCenter) ? "ok (distortion does real work)" : "FAIL");

    const bool pass = t_kmatch && t_roundtrip && t_negEdge && t_negCenter;
    std::printf("[SENSOR GATE INTRINSICS] %s\n", pass ? "PASS" : "FAIL");
    return pass;
}

bool runRgbNoiseStatsGate() {
    using namespace stats;
    std::printf("\n[SENSOR GATE NOISE-STATS] shot(Poisson)+read(Gaussian) signal dependence\n");

    SensorProfile prof;
    const RgbNoiseModel real = RgbNoiseModel::fromRgb(prof.rgb);
    const double expSlope = 1.0 / double(prof.rgb.photonsPerDN);          // 1/30 = 0.0333
    const double expIntercept = double(prof.rgb.readNoiseDN) * double(prof.rgb.readNoiseDN);  // 4.0

    std::mt19937_64 rng(prof.seed);
    const int M = 200000;
    std::vector<double> sig, varReal, varNeg;

    // mid-signal operating point where the fixed-Gaussian neg-ctrl is matched to the real model's variance.
    const double Smid = 105.0;
    const double matchedVar = Smid * expSlope + expIntercept;            // real total var at Smid
    RgbNoiseModel neg = real; neg.signalDependent = false; neg.fixedSigmaDN = std::sqrt(matchedVar);

    for (double S = 10.0; S <= 200.0; S += 19.0) {
        std::vector<double> rr(M), nn(M);
        for (int i = 0; i < M; ++i) rr[i] = real.apply(S, rng);
        for (int i = 0; i < M; ++i) nn[i] = neg.apply(S, rng);
        sig.push_back(S);
        varReal.push_back(variance(rr));
        varNeg.push_back(variance(nn));
    }

    const LinFit fitReal = linearFit(sig, varReal);
    const LinFit fitNeg  = linearFit(sig, varNeg);

    // dark frame at S=0: read noise CLIPS at 0 (DN cannot be negative), so the dark variance is the
    // RECTIFIED-Gaussian variance sigma^2*(pi-1)/(2pi) (~1.36), NOT read^2. The true read floor is the
    // photon-transfer-curve INTERCEPT above (4.089 ~ read^2 = 4.0) -- the canonical PTC read-noise measure.
    // Asserting the rectified value proves the negative-DN clip is physical, not a leak.
    std::vector<double> dark(M); for (int i = 0; i < M; ++i) dark[i] = real.apply(0.0, rng);
    const double darkVar = variance(dark);
    const double kPi = 3.14159265358979;
    const double sig0 = double(prof.rgb.readNoiseDN);
    const double expDark = sig0 * sig0 * (kPi - 1.0) / (2.0 * kPi) + 1.0 / 12.0;    // rectified read + quant

    const bool t_slope     = std::fabs(fitReal.slope - expSlope) < 0.15 * expSlope && fitReal.r2 > 0.98;
    const bool t_intercept = std::fabs(fitReal.intercept - expIntercept) < 0.7;     // PTC read floor: read^2 (+ ~1/12)
    const bool t_dark      = std::fabs(darkVar - expDark) < 0.30;                    // clipped (rectified) dark floor
    const bool t_negFlat   = std::fabs(fitNeg.slope) < 0.20 * expSlope;             // fixed-Gaussian ~ flat
    const bool t_separates = fitReal.slope > 3.0 * std::fabs(fitNeg.slope);         // slope discriminates

    std::printf("  real     : var = %.5f*S + %.3f  r2=%.4f (exp slope %.5f int(read^2) %.2f) -> %s\n",
                fitReal.slope, fitReal.intercept, fitReal.r2, expSlope, expIntercept,
                (t_slope && t_intercept) ? "ok" : "FAIL");
    std::printf("  dark(S=0): var=%.3f (clipped rect-gauss exp %.3f; PTC intercept %.3f IS the read floor) -> %s\n",
                darkVar, expDark, fitReal.intercept, t_dark ? "ok" : "FAIL");
    std::printf("  NEG-CTRL : fixed-gaussian slope=%.5f (exp ~0; matched var %.2f at S=%.0f) -> %s\n",
                fitNeg.slope, matchedVar, Smid, (t_negFlat && t_separates) ? "ok (no signal dependence)" : "FAIL");

    const bool pass = t_slope && t_intercept && t_dark && t_negFlat && t_separates;
    std::printf("[SENSOR GATE NOISE-STATS] %s\n", pass ? "PASS" : "FAIL");
    return pass;
}

} // namespace krs::sensor
