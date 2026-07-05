// ===========================================================================
// HONE (krs::hone) -- implementation + gate. See Hone.hpp banner.
// CEM parameter honing through a digital twin, empirical observability
// weighting (an excitation the sensor is blind to can never fake confidence),
// and dynamic-parameter detection (water sloshing in the glass is NOT a mass).
// Pure CPU: std + <random>, fixed seeds, bit-deterministic.
// ===========================================================================
#include "Hone.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <random>
#include <utility>

namespace krs::hone {

namespace {

// Draw n hypotheses from N(mean, sigma) truncated (clamped) at +-4 sigma.
std::vector<double> samplePopulation(std::mt19937& rng, double mean, double sigma, int n) {
    std::normal_distribution<double> dist(mean, sigma);
    std::vector<double> out(static_cast<size_t>(n));
    for (double& v : out) {
        const double x = dist(rng);
        v = std::min(mean + 4.0 * sigma, std::max(mean - 4.0 * sigma, x));
    }
    return out;
}

// Residual RMS between a prediction and the real measurement (min common length).
double residualRms(const std::vector<double>& pred, const std::vector<double>& meas) {
    const size_t n = std::min(pred.size(), meas.size());
    if (n == 0) return std::numeric_limits<double>::infinity();
    double s = 0.0;
    for (size_t i = 0; i < n; ++i) { const double d = pred[i] - meas[i]; s += d * d; }
    return std::sqrt(s / double(n));
}

// EMPIRICAL spread of predictions across hypotheses: per-sample std across the
// population, RMS over samples. This is the raw "how much does the sensor see
// the parameter" signal, in measurement units.
double predictionSpreadRms(const std::vector<std::vector<double>>& preds) {
    if (preds.empty()) return 0.0;
    size_t n = preds[0].size();
    for (const auto& p : preds) n = std::min(n, p.size());
    if (n == 0) return 0.0;
    const double P = double(preds.size());
    double sumVar = 0.0;
    for (size_t j = 0; j < n; ++j) {
        double mu = 0.0;
        for (const auto& p : preds) mu += p[j];
        mu /= P;
        double var = 0.0;
        for (const auto& p : preds) { const double d = p[j] - mu; var += d * d; }
        sumVar += var / P;
    }
    return std::sqrt(sumVar / double(n));
}

// The CEM loop proper. doDynamicCheck=false is used for the half-fits inside
// the dynamic detection (no recursion).
HoneResult honeCore(const Belief& prior, const ForwardModel& model,
                    const std::vector<double>& meas, const HoneConfig& cfg,
                    bool doDynamicCheck) {
    HoneResult r;
    std::mt19937 rng(cfg.seed);
    const int P = std::max(2, cfg.population);
    const int E = std::min(std::max(1, cfg.elites), P);
    const double noise = std::max(cfg.measNoiseStd, 1e-300);

    double mean  = prior.value;
    double sigma = std::max(prior.sigma, cfg.sigmaFloor);
    double obsFloor = sigma;                 // refined after round 1's empirical observability
    double bestVal = mean;
    double bestRes = std::numeric_limits<double>::infinity();

    for (int round = 0; round < std::max(1, cfg.maxRounds); ++round) {
        const std::vector<double> pop = samplePopulation(rng, mean, sigma, P);
        std::vector<std::vector<double>> preds(pop.size());
        for (size_t i = 0; i < pop.size(); ++i) preds[i] = model(pop[i]);

        if (round == 0) {
            // Round 1 population IS a prior draw -> empirical observability.
            r.observability = predictionSpreadRms(preds) / noise;
            // HONESTY FLOOR: if the excitation barely discriminates hypotheses
            // (obs ~ 0) the likelihoods are ~uniform and elite refits would
            // still shrink sigma by pure selection -- forbid that structurally.
            obsFloor = prior.sigma / std::max(1.0, r.observability);
        }

        // Gaussian likelihood of the real measurement given each prediction:
        // logL_i = -0.5*N*(rms_i/noise)^2 -- monotone in residual RMS, so elite
        // selection by ascending residual == selection by descending likelihood.
        std::vector<std::pair<double, int>> scored(pop.size());
        for (size_t i = 0; i < pop.size(); ++i)
            scored[i] = { residualRms(preds[i], meas), int(i) };
        std::sort(scored.begin(), scored.end()); // ties broken by index: deterministic

        if (scored[0].first < bestRes) { bestRes = scored[0].first; bestVal = pop[size_t(scored[0].second)]; }

        double em = 0.0;
        for (int e = 0; e < E; ++e) em += pop[size_t(scored[size_t(e)].second)];
        em /= double(E);
        double ev = 0.0;
        for (int e = 0; e < E; ++e) { const double d = pop[size_t(scored[size_t(e)].second)] - em; ev += d * d; }
        ev = std::sqrt(ev / double(E));

        mean  = em;
        sigma = std::max(std::max(ev, cfg.sigmaFloor), obsFloor);
        r.meanHistory.push_back(mean);
        r.sigmaHistory.push_back(sigma);
        r.rounds = round + 1;

        // Early stop: sigma pinned at its floor and the fit already explains
        // the data at noise level -- more rounds buy nothing.
        if (sigma <= std::max(cfg.sigmaFloor, obsFloor) * (1.0 + 1e-12) &&
            bestRes <= 3.0 * cfg.measNoiseStd)
            break;
    }

    r.posterior.value = mean;
    r.posterior.sigma = sigma;
    const bool shrank = sigma <= 0.5 * prior.sigma;
    r.converged = shrank && bestRes <= 3.0 * cfg.measNoiseStd;

    // ---- DYNAMIC DETECTION (water-in-the-glass) ----
    // The best static hypothesis STILL misfits badly -> maybe nothing static
    // can fit. Confirm by fitting each half of the record alone: if the two
    // half-fits disagree by > 3*posterior sigma the parameter drifted DURING
    // the excitation. Never report a confident static value for it.
    if (doDynamicCheck && meas.size() >= 4 &&
        bestRes > cfg.dynamicResidualFactor * cfg.measNoiseStd) {
        const size_t half = meas.size() / 2;
        ForwardModel firstHalf = [model, half](double v) {
            std::vector<double> f = model(v);
            if (f.size() > half) f.resize(half);
            return f;
        };
        ForwardModel secondHalf = [model, half](double v) {
            std::vector<double> f = model(v);
            const size_t b = std::min(f.size(), half);
            return std::vector<double>(f.begin() + long(b), f.end());
        };
        const std::vector<double> m1(meas.begin(), meas.begin() + long(half));
        const std::vector<double> m2(meas.begin() + long(half), meas.end());
        const HoneResult rA = honeCore(prior, firstHalf,  m1, cfg, false);
        const HoneResult rB = honeCore(prior, secondHalf, m2, cfg, false);
        const double drift = std::abs(rA.posterior.value - rB.posterior.value);
        if (drift > 3.0 * r.posterior.sigma) {
            r.dynamicSuspected = true;
            r.converged = false;
            // Honesty: the reported spread must at least cover the drift.
            r.posterior.sigma = std::max(r.posterior.sigma, 0.5 * drift);
        }
    }

    if (r.dynamicSuspected)                      r.posterior.prov = Prov::Dynamic;
    else if (r.converged)                        r.posterior.prov = Prov::Estimated;
    else if (r.posterior.sigma >= 0.9 * prior.sigma) r.posterior.prov = Prov::Prior;   // learned ~nothing
    else                                         r.posterior.prov = Prov::Suspect;     // shrank but misfits
    (void)bestVal;
    return r;
}

} // namespace

double expectedObservability(const Belief& prior, const ForwardModel& model, const HoneConfig& cfg) {
    std::mt19937 rng(cfg.seed);
    const int P = std::max(2, cfg.population);
    const double sigma = std::max(prior.sigma, cfg.sigmaFloor);
    const std::vector<double> pop = samplePopulation(rng, prior.value, sigma, P);
    std::vector<std::vector<double>> preds(pop.size());
    for (size_t i = 0; i < pop.size(); ++i) preds[i] = model(pop[i]);
    return predictionSpreadRms(preds) / std::max(cfg.measNoiseStd, 1e-300);
}

HoneResult hone(const Belief& prior, const ForwardModel& model,
                const std::vector<double>& realMeasurement, const HoneConfig& cfg) {
    return honeCore(prior, model, realMeasurement, cfg, true);
}

// ===========================================================================
// GATE (env KRS_HONE_SELFTEST) -- physically meaningful toys, 6DoF-FT flavor.
// ===========================================================================
bool runHoneGate() {
    using std::printf;
    setvbuf(stdout, nullptr, _IONBF, 0);
    printf("[hone] GATE HONE -- CEM twin-honing: lift-weigh mass, tilt-settle CoM, observability honesty, dynamic slosh, determinism\n");
    bool allOk = true;

    const double G = 9.81;
    const double mTruth = 0.372;                     // kg, truth for the lift
    const HoneConfig cfg;                            // defaults: pop 64, elites 12, 30 rounds, noise 1e-3, seed 42
    std::mt19937 noiseRng(1234);                     // harness sensor noise, fixed seed
    std::normal_distribution<double> sensorNoise(0.0, cfg.measNoiseStd);

    // The lift-weigh excitation: hold still, read N=50 Fz samples. Twin: Fz = m*g.
    const ForwardModel liftModel = [G](double m) { return std::vector<double>(50, m * G); };
    std::vector<double> liftMeas(50);
    for (double& s : liftMeas) s = mTruth * G + sensorNoise(noiseRng);
    const Belief liftPrior{ 0.6, 0.25, Prov::Prior };

    // ---- (1) LIFT-WEIGH: hone mass from Fz ----
    HoneResult r1 = hone(liftPrior, liftModel, liftMeas, cfg);
    {
        const double err = std::abs(r1.posterior.value - mTruth);
        const bool ok = r1.converged && err < 5e-3 && r1.posterior.sigma < 0.01 &&
                        r1.rounds <= cfg.maxRounds && r1.observability > 100.0;
        printf("[hone]   LIFT-WEIGH: m=%.4f kg (truth %.3f, err %.1e) sigma=%.2e rounds=%d conv:%d observability=%.0f (>>1)  %s\n",
               r1.posterior.value, mTruth, err, r1.posterior.sigma, r1.rounds,
               int(r1.converged), r1.observability, ok ? "PASS" : "FAIL");
        allOk = allOk && ok;
    }

    // ---- (2) TILT-SETTLE CoM: torque about the EE at K=8 tilt angles, mass KNOWN ----
    {
        const double rTruth = 0.043;                 // m, CoM offset truth
        std::vector<double> theta(8);
        for (int k = 0; k < 8; ++k) theta[size_t(k)] = 0.15 * double(k + 1);   // 0.15 .. 1.20 rad
        const ForwardModel tiltModel = [G, mTruth, theta](double r) {
            std::vector<double> tau(theta.size());
            for (size_t k = 0; k < theta.size(); ++k) tau[k] = mTruth * G * r * std::sin(theta[k]);
            return tau;
        };
        std::vector<double> tiltMeas = tiltModel(rTruth);
        for (double& s : tiltMeas) s += sensorNoise(noiseRng);
        const Belief comPrior{ 0.15, 0.06, Prov::Prior };
        const HoneResult r2 = hone(comPrior, tiltModel, tiltMeas, cfg);
        const double err = std::abs(r2.posterior.value - rTruth);
        const bool ok = err < 2e-3;
        printf("[hone]   TILT-SETTLE: CoM r=%.4f m (truth %.3f, err %.1e) sigma=%.2e conv:%d observability=%.0f  %s\n",
               r2.posterior.value, rTruth, err, r2.posterior.sigma, int(r2.converged), r2.observability,
               ok ? "PASS" : "FAIL");
        allOk = allOk && ok;
    }

    // ---- (3) OBSERVABILITY HONESTY + NEG-CTRL: a sensor blind to the parameter ----
    {
        // Excitation whose model IGNORES the parameter entirely (e.g. reading Fz
        // while the payload rests on a table): constant prediction, any m fits.
        const double kConst = mTruth * G;
        const ForwardModel blindModel = [kConst](double /*m*/) { return std::vector<double>(50, kConst); };
        std::vector<double> blindMeas(50);
        for (double& s : blindMeas) s = kConst + sensorNoise(noiseRng);
        const HoneResult r3 = hone(liftPrior, blindModel, blindMeas, cfg);

        const bool obsZero    = r3.observability < 1e-9;
        const bool sigmaHonest = r3.posterior.sigma >= 0.9 * liftPrior.sigma;   // shrank < ~10%
        const double eoLift  = expectedObservability(liftPrior, liftModel, cfg);
        const double eoBlind = expectedObservability(liftPrior, blindModel, cfg);
        const bool ranksOk = eoLift > 100.0 && eoBlind < 1e-9;
        const bool ok = obsZero && sigmaHonest && ranksOk;
        printf("[hone]   OBSERVABILITY: blind excitation obs=%.2e sigma %.3f -> %.3f (shrink %.1f%%, honest:%d); "
               "expectedObservability lift=%.0f vs blind=%.2e (ranks:%d)  %s\n",
               r3.observability, liftPrior.sigma, r3.posterior.sigma,
               100.0 * (1.0 - r3.posterior.sigma / liftPrior.sigma), int(sigmaHonest),
               eoLift, eoBlind, int(ranksOk), ok ? "PASS" : "FAIL");
        // NEG-CTRL: the blind excitation must NOT collapse sigma. If it had, the
        // honesty floor would be vacuous and every check above meaningless.
        printf("[hone]   NEG-CTRL blind excitation refuses confidence (sigma stays %.3f of prior): %s\n",
               r3.posterior.sigma / liftPrior.sigma, sigmaHonest ? "REJECTS(non-vacuous)" : "VACUOUS!");
        allOk = allOk && ok;
    }

    // ---- (4) DYNAMIC MASS: water slosh -- m(t) = 0.372 + 0.08*sin(2*pi*3*t) ----
    {
        std::vector<double> dynMeas(50);
        for (int k = 0; k < 50; ++k) {
            const double t = double(k) / 50.0;                                  // 1 s record, 3 slosh periods
            const double mt = mTruth + 0.08 * std::sin(2.0 * 3.14159265358979323846 * 3.0 * t);
            dynMeas[size_t(k)] = mt * G + sensorNoise(noiseRng);
        }
        const HoneResult r4 = hone(liftPrior, liftModel, dynMeas, cfg);
        const bool ok = r4.dynamicSuspected && r4.posterior.prov == Prov::Dynamic && !r4.converged;
        printf("[hone]   DYNAMIC-MASS: slosh record -> dynamicSuspected:%d prov==Dynamic:%d converged:%d  %s\n",
               int(r4.dynamicSuspected), int(r4.posterior.prov == Prov::Dynamic), int(r4.converged),
               ok ? "PASS" : "FAIL");
        printf("[hone]     avoided: naive static best-fit would have reported m=%.4f kg (a misleading average "
               "of a 0.29..0.45 kg slosh); reported sigma inflated to %.3f\n",
               r4.posterior.value, r4.posterior.sigma);
        allOk = allOk && ok;
    }

    // ---- (5) DETERMINISM: identical calls -> bit-identical posteriors ----
    {
        const HoneResult ra = hone(liftPrior, liftModel, liftMeas, cfg);
        const HoneResult rb = hone(liftPrior, liftModel, liftMeas, cfg);
        const bool bitsVal   = std::memcmp(&ra.posterior.value, &rb.posterior.value, sizeof(double)) == 0;
        const bool bitsSigma = std::memcmp(&ra.posterior.sigma, &rb.posterior.sigma, sizeof(double)) == 0;
        const bool histEq = ra.meanHistory == rb.meanHistory && ra.sigmaHistory == rb.sigmaHistory &&
                            ra.rounds == rb.rounds;
        // Also bit-identical to the first run in check (1): same inputs everywhere.
        const bool matchesR1 = std::memcmp(&ra.posterior.value, &r1.posterior.value, sizeof(double)) == 0;
        const bool ok = bitsVal && bitsSigma && histEq && matchesR1;
        printf("[hone]   DETERMINISM: two hone() calls bit-identical (value:%d sigma:%d history:%d, matches run#1:%d)  %s\n",
               int(bitsVal), int(bitsSigma), int(histEq), int(matchesR1), ok ? "PASS" : "FAIL");
        allOk = allOk && ok;
    }

    printf("[hone] %s\n", allOk
        ? "ALL PASS (mass honed in tens of rounds; CoM recovered; blind excitation stays humble; slosh flagged Dynamic; deterministic)"
        : "FAILURES PRESENT");
    return allOk;
}

} // namespace krs::hone
