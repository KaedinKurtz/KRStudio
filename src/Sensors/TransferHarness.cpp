#include "TransferHarness.hpp"
#include "SensorStats.hpp"
#include "RgbNoise.hpp"
#include "DepthModel.hpp"
#include "MaterialField.hpp"
#include "ImuModel.hpp"
#include <random>
#include <cmath>
#include <algorithm>

namespace krs::sensor {

SensorFingerprint computeFingerprint(const SensorProfile& prof, std::uint64_t seedSalt) {
    using namespace stats;
    SensorFingerprint fp;
    const std::uint64_t seed = prof.seed ^ seedSalt;

    // ---- RGB: variance-vs-signal slope (1/photonsPerDN) + read floor; histogram at a fixed mid signal. ----
    {
        const RgbNoiseModel rgb = RgbNoiseModel::fromRgb(prof.rgb);
        std::mt19937_64 r(seed ^ 0xC0B0u);
        std::vector<double> sig, var;
        const int M = 60000;
        for (double S = 20.0; S <= 200.0; S += 20.0) {
            std::vector<double> o(M); for (auto& v : o) v = rgb.apply(S, r);
            sig.push_back(S); var.push_back(variance(o));
        }
        const LinFit f = linearFit(sig, var);
        fp.rgbShotSlope = f.slope; fp.rgbReadFloor = f.intercept;
        std::vector<double> mid(M); for (auto& v : mid) v = rgb.apply(110.0, r);
        fp.rgbHist = histogram(mid, 80.0, 140.0, 24);
    }

    // ---- Depth: sigma-vs-Z power-fit exponent + specular hole rate. ----
    {
        DepthModel dep = DepthModel::fromProfile(prof.depth);
        dep.addSpeckle = false; dep.materialHoles = false; dep.flyingPixels = false;
        std::mt19937_64 r(seed ^ 0xDEF7u);
        const MaterialSample bright{ 0.0, 0.85, 1.0 }, spec{ 1.0, 0.85, 1.0 };
        std::vector<double> Zs, sg; const int N = 40000;
        for (double Z = 1.0; Z <= 6.0; Z += 0.5) {
            std::vector<double> e(N); for (int i = 0; i < N; ++i) e[i] = dep.sample(Z, bright, r) - Z;
            Zs.push_back(Z); sg.push_back(stddev(e));
        }
        fp.depthQuadExp = powerFit(Zs, sg).exponent;
        DepthModel deph = DepthModel::fromProfile(prof.depth);
        int holes = 0; const int T = 40000;
        for (int i = 0; i < T; ++i) if (deph.sample(2.0, spec, r) == DEPTH_INVALID) ++holes;
        fp.depthHoleSpec = double(holes) / T;
    }

    // ---- IMU: white-noise std at tau=dt + stateful integrated-drift RMS. ----
    {
        const ImuModel imu = ImuModel::fromProfile(prof.imu);
        InertialAxis gw = imu.gyro; gw.enableGM = false; gw.enableRW = false;
        std::mt19937_64 r(seed ^ 0x111Au);
        std::vector<double> w(120000); for (auto& v : w) v = gw.step(0.0, r);
        fp.imuWhiteStd = stddev(w);
        const double dt = 1.0 / double(prof.imu.gyroRateHz);
        // bias-instability floor: a STABLE statistic (one long series, many clusters) -- far more reproducible
        // across seeds than an integrated-drift RMS over a few runs.
        InertialAxis ax = imu.gyro; ax.biasTau = 2.0;       // compressed tau so the floor is observable
        std::mt19937_64 rr(seed ^ 0xF100u);
        std::vector<double> s(200000); for (auto& v : s) v = ax.step(0.0, rr);
        const auto ad = allanDeviation(s, dt, { 1, 2, 4, 8, 16, 32, 64, 128, 256, 512, 1024, 2048, 4096 });
        // MEAN of the Allan deviation across the floor region (tau ~ 0.4..6 s) -- averaging several stable
        // estimates is far more reproducible than a min-of-noisy-estimates.
        double sum = 0; int cnt = 0;
        for (const auto& p : ad) if (p.first > 0.4 && p.first < 6.0) { sum += p.second; ++cnt; }
        fp.imuAllanFloor = cnt ? sum / cnt : 0.0;
    }
    return fp;
}

TransferComparison compareFingerprints(const SensorFingerprint& a, const SensorFingerprint& b,
                                       double relTol, double histTol) {
    using namespace stats;
    auto rel = [](double x, double y) { const double d = std::max(std::fabs(x), std::fabs(y)); return d > 1e-12 ? std::fabs(x - y) / d : 0.0; };
    TransferComparison c;
    c.maxRelDiff = std::max({ rel(a.rgbShotSlope, b.rgbShotSlope), rel(a.rgbReadFloor, b.rgbReadFloor),
                              rel(a.depthQuadExp, b.depthQuadExp), rel(a.depthHoleSpec, b.depthHoleSpec),
                              rel(a.imuWhiteStd, b.imuWhiteStd), rel(a.imuAllanFloor, b.imuAllanFloor) });
    // L1 distance between normalized histograms.
    double sa = 0, sb = 0; for (int x : a.rgbHist) sa += x; for (int x : b.rgbHist) sb += x;
    double l1 = 0; const size_t n = std::min(a.rgbHist.size(), b.rgbHist.size());
    for (size_t i = 0; i < n; ++i) l1 += std::fabs((sa > 0 ? a.rgbHist[i] / sa : 0.0) - (sb > 0 ? b.rgbHist[i] / sb : 0.0));
    c.histL1 = l1;
    c.match = c.maxRelDiff < relTol && c.histL1 < histTol;
    return c;
}

const char* transferHonestyLabel() {
    return "self-consistent, NOT validated against real hardware -- awaiting operator D456 capture "
           "(gated vs a SECOND SYNTHETIC instance; real sim-to-real transfer remains UNVALIDATED)";
}

} // namespace krs::sensor
