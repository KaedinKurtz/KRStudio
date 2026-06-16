// SensorGate0.cpp -- GATE 0: prove the statistical harness itself before trusting it to gate physics.
//   (a) self-tests on analytic cases, each anchored to an INDEPENDENT analytic constant (never to the
//       function under test): constant->0 var; Gaussian->known var/std; Poisson->var==mean; Bessel (n-1)
//       verified on a textbook sample (rejects the n divisor); linear & power fits exact; hole rate exact;
//       histogram bins a uniform evenly; autocorr lag0==1 & white-noise lag1~0; conservation residual ~0 on
//       a closed system and == the leak on a leaky one (via the REAL harness primitive); Allan deviation of
//       white noise has BOTH the -1/2 slope AND the correct level (sigma at tau=dt) with a good fit.
//   (b) a NEG-CTRL that exercises the SAME discriminator the positive test uses: one isPoissonByVarMean()
//       predicate ACCEPTS the Poisson stream and REJECTS the Gaussian -> the statistic discriminates.
//   (c) a profile round-trip as a 5-point REGRESSION: sweeping a bound param (readNoiseDN) makes the measured
//       variance track sigma^2 with slope 1.0 +- 0.02 (a multiplicative variance bug as small as 3% fails);
//       an unbound param leaves the deterministic output bit-identical.
// Hardened after the Phase-0 adversarial review (vacuous conservation row, slope-only Allan, inline neg-ctrl,
// round-trip theater, unverified Bessel -- all closed here).
#include "SensorGates.hpp"
#include "SensorStats.hpp"
#include "SensorProfile.hpp"
#include <random>
#include <vector>
#include <cmath>
#include <cstdio>
#include <algorithm>

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

    // (2) Bessel (n-1) verified explicitly: textbook sample {2,4,4,4,5,5,7,9} -> sample var 32/7=4.5714,
    //     population var 32/8=4.0. The harness MUST report 4.5714 (n-1), not 4.0 (n).
    const std::vector<double> bessel{2, 4, 4, 4, 5, 5, 7, 9};
    const double bv = variance(bessel);
    const bool t_bessel = std::fabs(bv - 32.0 / 7.0) < 1e-9 && std::fabs(bv - 4.0) > 0.5;
    std::printf("  bessel   : var=%.5f (n-1 exp 4.5714, NOT 4.0) -> %s\n", bv, t_bessel ? "ok" : "FAIL");

    // (3) Gaussian(mean 5, sigma 2) -> mean~5, var~4, std~2
    std::normal_distribution<double> gd(5.0, 2.0);
    std::vector<double> gs(300000); for (auto& v : gs) v = gd(rng);
    const double gm = mean(gs), gvv = variance(gs), gsd = stddev(gs);
    const bool t_gauss = std::fabs(gm - 5.0) < 0.05 && std::fabs(gvv - 4.0) < 0.1 && std::fabs(gsd - 2.0) < 0.02;
    std::printf("  gauss    : mean=%.3f var=%.3f std=%.3f (exp 5,4,2) -> %s\n", gm, gvv, gsd, t_gauss ? "ok" : "FAIL");

    // (4) Poisson(lambda 10) accepted by the var==mean discriminator (reused below as the neg-ctrl).
    auto isPoissonByVarMean = [](const std::vector<double>& s, double relTol) {
        const double m = mean(s), v = variance(s);
        return m > 0.0 && std::fabs(v - m) < relTol * m;     // var==mean to within relTol of the mean
    };
    std::poisson_distribution<int> pd(10);
    std::vector<double> ps(300000); for (auto& v : ps) v = double(pd(rng));
    const double pm = mean(ps), pvv = variance(ps);
    const bool t_poisson = std::fabs(pm - 10.0) < 0.1 && isPoissonByVarMean(ps, 0.05);
    std::printf("  poisson  : mean=%.3f var=%.3f (var==mean accepted) -> %s\n", pm, pvv, t_poisson ? "ok" : "FAIL");

    // (5) linear fit y = 2x + 1
    std::vector<double> lx, ly; for (int i = 0; i < 100; ++i) { lx.push_back(i); ly.push_back(2.0 * i + 1.0); }
    const LinFit lf = linearFit(lx, ly);
    const bool t_lin = std::fabs(lf.slope - 2.0) < 1e-6 && std::fabs(lf.intercept - 1.0) < 1e-6 && lf.r2 > 0.9999;
    std::printf("  linfit   : slope=%.4f int=%.4f r2=%.5f -> %s\n", lf.slope, lf.intercept, lf.r2, t_lin ? "ok" : "FAIL");

    // (6) power fit y = 3 x^2  (exponent ~ 2, coeff ~ 3) -- the depth-quadratic discriminator
    std::vector<double> px, py; for (int i = 1; i <= 50; ++i) { px.push_back(i); py.push_back(3.0 * i * i); }
    const PowerFit pf = powerFit(px, py);
    const bool t_pow = std::fabs(pf.exponent - 2.0) < 1e-4 && std::fabs(pf.coeff - 3.0) < 1e-2 && pf.r2 > 0.9999;
    std::printf("  powerfit : exp=%.4f coeff=%.4f r2=%.5f (exp 2,3) -> %s\n", pf.exponent, pf.coeff, pf.r2, t_pow ? "ok" : "FAIL");

    // (7) rms of {3,4} = sqrt(12.5)
    const std::vector<double> rv{3.0, 4.0};
    const double rmsv = rms(rv);
    const bool t_rms = std::fabs(rmsv - std::sqrt(12.5)) < 1e-12;
    std::printf("  rms      : %.5f (exp %.5f)        -> %s\n", rmsv, std::sqrt(12.5), t_rms ? "ok" : "FAIL");

    // (8) histogram: 200k uniform[0,1) into 10 bins -> each ~20000, every bin within 3%
    std::vector<double> uni(200000); for (auto& v : uni) v = ur(rng);
    const auto hh = histogram(uni, 0.0, 1.0, 10);
    int hmin = *std::min_element(hh.begin(), hh.end()), hmax = *std::max_element(hh.begin(), hh.end());
    const bool t_hist = hh.size() == 10 && hmin > 19400 && hmax < 20600;   // 20000 +- 3%
    std::printf("  hist     : 10 bins min=%d max=%d (exp ~20000+-3%%) -> %s\n", hmin, hmax, t_hist ? "ok" : "FAIL");

    // (9) hole rate: 30% sentinel
    std::vector<double> hs(200000, 1.0);
    for (auto& v : hs) if (ur(rng) < 0.30) v = -9999.0;
    const double hr = holeRate(hs, -9999.0);
    const bool t_hole = std::fabs(hr - 0.30) < 0.005;
    std::printf("  holerate : %.4f (exp 0.30)        -> %s\n", hr, t_hole ? "ok" : "FAIL");

    // (10) conservation via the REAL harness primitive: closed system residual ~0; injected leak == 0.5.
    std::vector<double> before(2000); for (auto& v : before) v = ur(rng);
    std::vector<double> after = before;                                    // closed: nothing changes
    const double resClosed = conservationResidual(before, after);
    std::vector<double> leaked = before; leaked[1000] += 0.5;              // a unit of mass leaks in
    const double resLeak = conservationResidual(before, leaked);
    const bool t_conserve = std::fabs(resClosed) < 1e-9 && std::fabs(resLeak - 0.5) < 1e-9;
    std::printf("  conserve : residual closed=%.2e leak=%.4f (exp 0,0.5) -> %s\n", resClosed, resLeak, t_conserve ? "ok" : "FAIL");

    // (11) autocorrelation: lag0 == 1 always; white noise decorrelated at lag1 (~0)
    std::normal_distribution<double> wn(0.0, 0.01);
    std::vector<double> series(60000); for (auto& v : series) v = wn(rng);
    const double ac0 = autocorr(series, 0), ac1 = autocorr(series, 1);
    const bool t_acorr = std::fabs(ac0 - 1.0) < 1e-9 && std::fabs(ac1) < 0.02;
    std::printf("  autocorr : lag0=%.4f lag1=%.4f (exp 1, ~0) -> %s\n", ac0, ac1, t_acorr ? "ok" : "FAIL");

    // (12) Allan deviation of white noise -> BOTH the level (sigma at tau=dt) AND the -1/2 slope, good fit.
    const double dt = 1.0 / 200.0;
    const auto ad = allanDeviation(series, dt, {1, 2, 4, 8, 16, 32, 64, 128, 256});
    std::vector<double> tau, dev; for (const auto& pr : ad) { tau.push_back(pr.first); dev.push_back(pr.second); }
    const double allanLevel = ad.empty() ? -1.0 : ad.front().second;       // at m=1, sigma_A == input sigma
    const PowerFit adf = powerFit(tau, dev);
    const bool t_allan = std::fabs(allanLevel - 0.01) < 0.001 &&            // LEVEL: catches missing-sqrt / scale bug
                         adf.exponent < -0.35 && adf.exponent > -0.65 &&    // SLOPE: white-noise -1/2 law
                         adf.r2 > 0.9;
    std::printf("  allan    : level(tau=dt)=%.5f (exp 0.01) slope=%.3f r2=%.3f -> %s\n",
                allanLevel, adf.exponent, adf.r2, t_allan ? "ok" : "FAIL");

    // ---- NEG-CTRL: the SAME isPoissonByVarMean() that accepted Poisson must REJECT a Gaussian(20,var 4). ----
    std::normal_distribution<double> gd2(20.0, 2.0);
    std::vector<double> gs2(150000); for (auto& v : gs2) v = gd2(rng);
    const double g2m = mean(gs2), g2v = variance(gs2);
    const bool neg_rejectsGaussian = !isPoissonByVarMean(gs2, 0.05);       // var(4) != mean(20) -> rejected
    std::printf("  NEG-CTRL : gaussian(mean=%.2f,var=%.2f) rejected by same var==mean discriminator -> %s\n",
                g2m, g2v, neg_rejectsGaussian ? "ok (discriminates)" : "FAIL (vacuous)");

    // ---- PROFILE ROUND-TRIP (5-point regression): a model consuming readNoiseDN ONLY. Sweeping the bound
    //      param makes measured var track sigma^2 with slope 1.0 (a 3% variance bug -> slope 1.03 -> FAIL).
    //      An unbound param leaves the deterministic output bit-identical. (The decisive multi-param decoupling
    //      test lands in Phase 1 against the real camera model; here the model is minimal by necessity.) ----
    auto modelVar = [](const SensorProfile& p) {
        std::mt19937_64 r(p.seed);
        std::normal_distribution<double> nd(0.0, double(p.rgb.readNoiseDN));   // consumes readNoiseDN ONLY
        std::vector<double> o(200000); for (auto& v : o) v = 128.0 + nd(r);    // never reads baselineMm
        return variance(o);
    };
    SensorProfile prof;
    std::vector<double> sig2, measVar;
    for (double s : {1.0, 2.0, 3.0, 4.0, 5.0}) { prof.rgb.readNoiseDN = s; sig2.push_back(s * s); measVar.push_back(modelVar(prof)); }
    const LinFit rt = linearFit(sig2, measVar);
    const bool boundTracks = std::fabs(rt.slope - 1.0) < 0.02 && std::fabs(rt.intercept) < 0.3 && rt.r2 > 0.999;
    std::printf("  roundtrip: bound  var = %.4f*sigma^2 + %.3f  r2=%.4f (slope exp 1.00) -> %s\n",
                rt.slope, rt.intercept, rt.r2, boundTracks ? "ok" : "FAIL");
    prof.rgb.readNoiseDN = 3.0; const double a = modelVar(prof);
    prof.depth.baselineMm = 250.0; const double b = modelVar(prof);        // unbound edit
    const bool unboundNoChange = std::fabs(a - b) < 1e-12;
    std::printf("  roundtrip: unbound baselineMm 95->250: var %.4f->%.4f (bit-identical) -> %s\n",
                a, b, unboundNoChange ? "ok" : "FAIL");

    const bool pass = t_const && t_bessel && t_gauss && t_poisson && t_lin && t_pow && t_rms && t_hist &&
                      t_hole && t_conserve && t_acorr && t_allan && neg_rejectsGaussian &&
                      boundTracks && unboundNoChange;
    std::printf("[SENSOR GATE 0] %s\n", pass ? "PASS" : "FAIL");
    return pass;
}

} // namespace krs::sensor
