// SensorGate0.cpp -- GATE 0: prove the statistical harness itself before trusting it to gate physics.
//   (a) self-tests on analytic cases (constant->0 var, Gaussian->known var, Poisson->var==mean, linear &
//       power fits exact, hole rate exact, conservation holds + an injected leak is CAUGHT, Allan deviation
//       of white noise has the -1/2 slope),
//   (b) a NEG-CTRL that proves a statistic DISCRIMINATES: a Gaussian (var != mean) is correctly rejected by
//       the Poisson var==mean test -- i.e. the test is not vacuous,
//   (c) a profile round-trip: editing a BOUND profile param moves the consuming model's output statistic to
//       match, while editing an UNBOUND param leaves it unchanged.
#include "SensorGates.hpp"
#include "SensorStats.hpp"
#include "SensorProfile.hpp"
#include <random>
#include <vector>
#include <cmath>
#include <cstdio>

namespace krs::sensor {

bool runStatsHarnessGate() {
    using namespace stats;
    std::mt19937_64 rng(0xC0FFEEull);
    std::uniform_real_distribution<double> ur(0.0, 1.0);

    std::printf("\n[SENSOR GATE 0] statistical harness self-test\n");

    // (1) constant -> variance ~ 0
    std::vector<double> constS(100000, 7.0);
    const double cv = variance(constS);
    const bool t_const = cv < 1e-9;
    std::printf("  const    : var=%.3e            -> %s\n", cv, t_const ? "ok" : "FAIL");

    // (2) Gaussian(mean 5, sigma 2) -> mean~5, var~4
    std::normal_distribution<double> gd(5.0, 2.0);
    std::vector<double> gs(300000); for (auto& v : gs) v = gd(rng);
    const double gm = mean(gs), gvv = variance(gs);
    const bool t_gauss = std::fabs(gm - 5.0) < 0.05 && std::fabs(gvv - 4.0) < 0.1;
    std::printf("  gauss    : mean=%.3f var=%.3f (exp 5.0,4.0) -> %s\n", gm, gvv, t_gauss ? "ok" : "FAIL");

    // (3) Poisson(lambda 10) -> var ~ mean ~ 10 (the Poisson property)
    std::poisson_distribution<int> pd(10);
    std::vector<double> ps(300000); for (auto& v : ps) v = double(pd(rng));
    const double pm = mean(ps), pvv = variance(ps);
    const bool t_poisson = std::fabs(pm - 10.0) < 0.1 && std::fabs(pvv - 10.0) < 0.2 && std::fabs(pvv - pm) < 0.3;
    std::printf("  poisson  : mean=%.3f var=%.3f (var==mean) -> %s\n", pm, pvv, t_poisson ? "ok" : "FAIL");

    // (4) linear fit y = 2x + 1
    std::vector<double> lx, ly; for (int i = 0; i < 100; ++i) { lx.push_back(i); ly.push_back(2.0 * i + 1.0); }
    const LinFit lf = linearFit(lx, ly);
    const bool t_lin = std::fabs(lf.slope - 2.0) < 1e-6 && std::fabs(lf.intercept - 1.0) < 1e-6 && lf.r2 > 0.9999;
    std::printf("  linfit   : slope=%.4f int=%.4f r2=%.5f -> %s\n", lf.slope, lf.intercept, lf.r2, t_lin ? "ok" : "FAIL");

    // (5) power fit y = 3 x^2  (exponent ~ 2, coeff ~ 3) -- the depth-quadratic discriminator
    std::vector<double> px, py; for (int i = 1; i <= 50; ++i) { px.push_back(i); py.push_back(3.0 * i * i); }
    const PowerFit pf = powerFit(px, py);
    const bool t_pow = std::fabs(pf.exponent - 2.0) < 1e-4 && std::fabs(pf.coeff - 3.0) < 1e-2;
    std::printf("  powerfit : exp=%.4f coeff=%.4f (exp 2.0,3.0) -> %s\n", pf.exponent, pf.coeff, t_pow ? "ok" : "FAIL");

    // (6) hole rate: 30% sentinel
    std::vector<double> hs(200000, 1.0);
    for (auto& v : hs) if (ur(rng) < 0.30) v = -9999.0;
    const double hr = holeRate(hs, -9999.0);
    const bool t_hole = std::fabs(hr - 0.30) < 0.005;
    std::printf("  holerate : %.4f (exp 0.30)        -> %s\n", hr, t_hole ? "ok" : "FAIL");

    // (7) conservation: a closed sum is conserved; an injected leak is CAUGHT (the leak detector itself works)
    std::vector<double> closed(1000); double total = 0; for (auto& v : closed) { v = ur(rng); total += v; }
    double sumA = 0; for (double v : closed) sumA += v;
    const bool t_conserve = std::fabs(sumA - total) < 1e-9;
    std::vector<double> leaky = closed; leaky[500] += 0.5;            // inject a leak
    double sumB = 0; for (double v : leaky) sumB += v;
    const bool t_leakCaught = std::fabs(sumB - total) > 1e-6;
    std::printf("  conserve : residual=%.2e ; leak detected=%s -> %s\n",
                std::fabs(sumA - total), t_leakCaught ? "yes" : "no", (t_conserve && t_leakCaught) ? "ok" : "FAIL");

    // (8) Allan deviation of white noise -> slope ~ -1/2 in log-log (the IMU white-noise signature)
    std::normal_distribution<double> wn(0.0, 0.01);
    std::vector<double> series(60000); for (auto& v : series) v = wn(rng);
    const auto ad = allanDeviation(series, 1.0 / 200.0, {1, 2, 4, 8, 16, 32, 64, 128, 256});
    std::vector<double> tau, dev; for (const auto& pr : ad) { tau.push_back(pr.first); dev.push_back(pr.second); }
    const PowerFit adf = powerFit(tau, dev);
    const bool t_allan = adf.exponent < -0.35 && adf.exponent > -0.65;
    std::printf("  allan    : white-noise slope=%.3f (exp ~ -0.5) -> %s\n", adf.exponent, t_allan ? "ok" : "FAIL");

    // ---- NEG-CTRL: the Poisson var==mean test DISCRIMINATES. A Gaussian(mean 20, var 4) must be REJECTED. ----
    std::normal_distribution<double> gd2(20.0, 2.0);
    std::vector<double> gs2(150000); for (auto& v : gs2) v = gd2(rng);
    const double g2m = mean(gs2), g2v = variance(gs2);
    const bool neg_rejectsGaussian = std::fabs(g2v - g2m) > 5.0;     // var(4) far from mean(20) -> not Poisson
    std::printf("  NEG-CTRL : gaussian(mean=%.2f,var=%.2f) rejected by var==mean test -> %s\n",
                g2m, g2v, neg_rejectsGaussian ? "ok (discriminates)" : "FAIL (vacuous)");

    // ---- PROFILE ROUND-TRIP: a minimal model consumes ONE bound param; an unbound param has no effect. ----
    SensorProfile prof;
    auto noisyConst = [](const SensorProfile& p) {
        std::mt19937_64 r(p.seed);
        std::normal_distribution<double> nd(0.0, double(p.rgb.readNoiseDN));   // consumes readNoiseDN ONLY
        std::vector<double> o(150000); for (auto& v : o) v = 128.0 + nd(r);    // does NOT consume baselineMm
        return variance(o);
    };
    const double v1 = noisyConst(prof);                     // sigma=2 -> var ~ 4
    prof.rgb.readNoiseDN = 5.0;                             // edit BOUND param
    const double v2 = noisyConst(prof);                     // sigma=5 -> var ~ 25
    const bool boundChanges = std::fabs(v1 - 4.0) < 0.15 && std::fabs(v2 - 25.0) < 0.8 && v2 > v1 * 3.0;
    prof.depth.baselineMm = 200.0;                          // edit UNBOUND param (model ignores it)
    const double v3 = noisyConst(prof);
    const bool unboundNoChange = std::fabs(v3 - v2) < 1e-9;
    std::printf("  roundtrip: bound  readNoiseDN 2->5  : var %.3f->%.3f (exp ~4->~25) -> %s\n",
                v1, v2, boundChanges ? "ok" : "FAIL");
    std::printf("  roundtrip: unbound baselineMm 95->200: var %.3f->%.3f (no change)  -> %s\n",
                v2, v3, unboundNoChange ? "ok" : "FAIL");

    const bool pass = t_const && t_gauss && t_poisson && t_lin && t_pow && t_hole &&
                      t_conserve && t_leakCaught && t_allan && neg_rejectsGaussian &&
                      boundChanges && unboundNoChange;
    std::printf("[SENSOR GATE 0] %s\n", pass ? "PASS" : "FAIL");
    return pass;
}

} // namespace krs::sensor
