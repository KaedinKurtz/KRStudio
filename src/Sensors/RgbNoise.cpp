#include "RgbNoise.hpp"
#include <algorithm>
#include <cmath>

namespace krs::sensor {

RgbNoiseModel RgbNoiseModel::fromRgb(const RgbProfile& p) {
    RgbNoiseModel m;
    m.photonsPerDN = double(p.photonsPerDN);
    m.readNoiseDN  = double(p.readNoiseDN);
    m.fullScaleDN  = double(p.fullScaleDN);
    m.bitDepth     = int(p.bitDepth);
    m.signalDependent = true;
    return m;
}

double RgbNoiseModel::shotElectrons(double signalDN, std::mt19937_64& rng) const {
    const double S = std::clamp(signalDN, 0.0, fullScaleDN);
    const double e = S * photonsPerDN;                       // mean electron count
    if (e < 1e-9) return 0.0;
    if (gaussianShot) {                                      // neg-ctrl B: continuous, NOT Poisson
        std::normal_distribution<double> g(e, std::sqrt(e));
        return g(rng);
    }
    std::poisson_distribution<long long> pois(e);            // real: integer Poisson count
    return double(pois(rng));
}

double RgbNoiseModel::apply(double signalDN, std::mt19937_64& rng) const {
    double S = std::clamp(signalDN, 0.0, fullScaleDN);
    double noisy;

    if (signalDependent) {
        // shot noise: electrons ~ Poisson(S*photonsPerDN); back to DN gives variance = S/photonsPerDN.
        const double shotDN = shotElectrons(S, rng) / photonsPerDN;
        // read noise: signal-independent Gaussian floor.
        std::normal_distribution<double> rd(0.0, readNoiseDN);
        noisy = shotDN + rd(rng);
    } else {
        // NEG-CTRL: a single fixed-variance Gaussian, independent of S (no Poisson signal dependence).
        std::normal_distribution<double> g(0.0, fixedSigmaDN);
        noisy = S + g(rng);
    }

    // quantize to the sensor bit depth, then clip to the valid DN range.
    const double maxLevel = double((1 << bitDepth) - 1);                 // 8-bit -> 255
    const double scale = maxLevel / fullScaleDN;                         // DN -> level (1:1 when fullScale==255)
    double q = std::round(noisy * scale) / scale;
    return std::clamp(q, 0.0, fullScaleDN);
}

} // namespace krs::sensor
