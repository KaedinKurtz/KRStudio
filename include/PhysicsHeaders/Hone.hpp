#pragma once
// ===========================================================================
// HONE (krs::hone) -- population-based parameter estimation through a digital
// twin, with EMPIRICAL observability weighting and dynamic-parameter detection.
//
// The idea: a physical parameter (payload mass, CoM offset, friction...) is a
// Belief {value, sigma, provenance}. An EXCITATION (lift, tilt, shake) plus the
// digital twin gives a ForwardModel: hypothesized parameter -> predicted
// measurement vector (e.g. lift_weigh -> N samples of the FT sensor Fz). The
// hone() loop runs a Cross-Entropy Method over the hypothesis population:
// sample from the current (mean,sigma), predict, weight by Gaussian likelihood
// of the REAL measurement, keep elites, refit. Converges in tens of rounds.
//
// OBSERVABILITY is EMPIRICAL, not analytic: draw the population from the PRIOR,
// run the twin, and measure the RMS spread of the predictions across hypotheses
// in units of sensor noise. >>1 = the excitation discriminates hypotheses; ~0 =
// the sensor is blind to the parameter. Honesty is enforced structurally: the
// posterior sigma is floored by prior_sigma / max(1, observability), so an
// uninformative excitation can NEVER fake confidence (likelihoods ~uniform =>
// sigma must not collapse). expectedObservability() scores an excitation
// BEFORE moving, so a planner can rank candidate excitations.
//
// DYNAMIC DETECTION (water-in-the-glass): if the best static hypothesis still
// misfits (residual RMS >> noise) AND fitting the first half of the measurement
// alone vs the second half alone yields parameter values that disagree by more
// than 3 posterior sigma, the "parameter" is drifting DURING the excitation --
// report Prov::Dynamic, converged=false, and never a confident static value.
//
// Pure CPU, std + <random> only, fixed seed => bit-deterministic.
// Gate: runHoneGate() (env KRS_HONE_SELFTEST).
// ===========================================================================
#include <functional>
#include <vector>

namespace krs::hone {

// Provenance of a belief: where the number came from / how much to trust it.
enum class Prov { Unknown, Prior, Estimated, Measured, Dynamic, Suspect };

struct Belief {
    double value = 0.0;
    double sigma = 1e9;          // 1-sigma uncertainty; huge = "no idea"
    Prov   prov  = Prov::Unknown;
};

// A forward model: the TWIN. Given a hypothesized parameter value, predict the
// measurement vector an excitation would produce (e.g. lift_weigh -> N samples
// of the FT sensor Fz).
using ForwardModel = std::function<std::vector<double>(double)>;

struct HoneConfig {
    int population = 64;        // hypotheses per round
    int elites = 12;            // CEM elite count
    int maxRounds = 30;         // "global convergence in tens of samples"
    double sigmaFloor = 1e-5;   // never report tighter than this
    double measNoiseStd = 1e-3; // assumed sensor noise (likelihood scale)
    unsigned seed = 42;         // deterministic
    double dynamicResidualFactor = 8.0; // best-fit residual > factor*noise => cannot be static
};

struct HoneResult {
    Belief posterior;              // value = elite mean, sigma = elite std (>= sigmaFloor), prov set
    bool converged = false;        // sigma shrank meaningfully AND best residual ~ noise level
    bool dynamicSuspected = false; // parameter drifted during the excitation (see header banner)
    int rounds = 0;
    double observability = 0.0;    // EMPIRICAL info: RMS spread of the PRIOR population predictions / measNoiseStd
    std::vector<double> meanHistory;  // population mean per round (the converging cloud, for plotting)
    std::vector<double> sigmaHistory;
};

// Score an excitation BEFORE moving: draw a population from the prior, run the
// model, return the RMS spread of predictions across hypotheses divided by
// measNoiseStd (>>1 = informative, ~0 = blind).
double expectedObservability(const Belief& prior, const ForwardModel& model, const HoneConfig& cfg);

// The honing loop: CEM over rounds. Each round: sample population from current
// (mean,sigma) (truncated at +-4 sigma, deterministic RNG); predict via model;
// weight by Gaussian likelihood of realMeasurement given prediction (residual
// RMS vs measNoiseStd); pick elites; refit mean/sigma. Posterior sigma is
// floored by prior_sigma / max(1, observability) -- an uninformative excitation
// cannot fake confidence. converged requires best-elite residual RMS <=
// 3*measNoiseStd. Dynamic detection per the banner above.
HoneResult hone(const Belief& prior, const ForwardModel& model,
                const std::vector<double>& realMeasurement, const HoneConfig& cfg);

// Headless self-test gate (env KRS_HONE_SELFTEST). Prints [hone] lines,
// per-check PASS/FAIL and a final ALL PASS / FAILURES PRESENT.
bool runHoneGate();

} // namespace krs::hone
