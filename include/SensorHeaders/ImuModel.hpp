#pragma once
// ImuModel.hpp -- synthetic BMI085 IMU with STATEFUL noise carried frame-to-frame (the defining feature; a
// memoryless Gaussian cannot reproduce it). Each inertial axis stacks: white noise (signal-independent IID) +
// bias instability (1st-order Gauss-Markov -- the Allan-variance floor, the VIO-killer) + rate random walk
// (integrated white -> +1/2 Allan slope). Plus deterministic systematic errors: scale factor, axis
// misalignment, temperature bias. State (biasGM, biasRW) persists across step() calls.
#include "SensorProfile.hpp"
#include <glm/glm.hpp>
#include <random>

namespace krs::sensor {

struct InertialAxis {
    double rateHz{400.0};
    double noiseDensity{0.0};   // continuous-time white-noise density -> discrete std = density*sqrt(rateHz)
    double biasInstab{0.0};     // Gauss-Markov steady-state std (the Allan floor)
    double biasTau{200.0};      // GM correlation time (s)
    double randomWalk{0.0};     // rate-random-walk density (the +1/2 Allan slope)
    // carried state:
    double biasGM{0.0};
    double biasRW{0.0};

    double dt() const { return 1.0 / rateHz; }
    void reset() { biasGM = 0.0; biasRW = 0.0; }

    // advance one sample around a true value; mutates state. Full stateful stack.
    double step(double trueVal, std::mt19937_64& rng);
    // NEG-CTRL: memoryless per-sample Gaussian (white only, no carried bias).
    double stepWhiteOnly(double trueVal, std::mt19937_64& rng) const;
};

struct ImuModel {
    InertialAxis accel, gyro;
    double scaleFactorErr{0.0};   // fractional scale error per axis
    double axisMisalignRad{0.0};  // small-angle cross-axis coupling
    double tempBiasCoeff{0.0};    // bias per degC
    double calibTempC{25.0};      // temperature at which bias is nulled

    static ImuModel fromProfile(const ImuProfile& p);

    // deterministic systematic error: scale (diagonal) + misalignment (off-diagonal) + temp bias offset.
    glm::dvec3 applySystematic(const glm::dvec3& trueVec, double tempC) const;
};

} // namespace krs::sensor
