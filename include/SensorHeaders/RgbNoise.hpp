#pragma once
// RgbNoise.hpp -- Layer-3 (live, per-frame) RGB sensor noise: the physically-ordered stack
//   shot (Poisson, signal-DEPENDENT) -> read (Gaussian, signal-INDEPENDENT floor) -> quantize (bit depth).
// Per-pixel variance(S) is AFFINE in the clean signal S: var = S/photonsPerDN + readNoiseDN^2 (+ ~1/12
// quantization). The slope 1/photonsPerDN is the Poisson signature; a naive fixed-Gaussian (the neg-ctrl)
// has slope ~0. The model is stateless given an RNG -- the caller owns the seed (determinism per seed).
#include "SensorProfile.hpp"
#include <random>

namespace krs::sensor {

struct RgbNoiseModel {
    double photonsPerDN{30.0};   // electrons per DN (gain): shot variance in DN = S / photonsPerDN
    double readNoiseDN{2.0};     // read-noise std (DN), signal-independent dark floor
    double fullScaleDN{255.0};   // clipping ceiling
    int    bitDepth{8};          // quantization levels = 2^bitDepth - 1
    bool   signalDependent{true};// false => the FIXED-GAUSSIAN neg-ctrl (read+shot collapsed to a constant)
    double fixedSigmaDN{0.0};    // used only when !signalDependent

    static RgbNoiseModel fromRgb(const RgbProfile& p);

    // Apply the noise stack to one clean signal S (DN). Returns the quantized, clipped noisy DN.
    double apply(double signalDN, std::mt19937_64& rng) const;
};

} // namespace krs::sensor
