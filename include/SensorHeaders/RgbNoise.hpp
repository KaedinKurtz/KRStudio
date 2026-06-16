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
    bool   signalDependent{true};// false => the FIXED-GAUSSIAN neg-ctrl A (read+shot collapsed to a constant)
    bool   gaussianShot{false};  // true => signal-DEPENDENT GAUSSIAN shot (neg-ctrl B): matches the affine
                                 //         photon-transfer curve but is NOT Poisson (continuous, not integer)
    double fixedSigmaDN{0.0};    // used only when !signalDependent
    double biasDN{0.0};          // a DC pedestal added to every pixel -- a BIASED model (breaks signal conservation)

    static RgbNoiseModel fromRgb(const RgbProfile& p);

    // The raw shot stage: electron count for a clean signal S. Poisson (integer) normally; a continuous
    // Gaussian(e, sqrt(e)) when gaussianShot (the neg-ctrl B that fools the affine test). Used BY apply(),
    // and probed directly by the gate to test the DEFINING Poisson property (var==mean AND integer counts).
    double shotElectrons(double signalDN, std::mt19937_64& rng) const;

    // Apply the noise stack to one clean signal S (DN). Returns the quantized, clipped noisy DN.
    double apply(double signalDN, std::mt19937_64& rng) const;
};

} // namespace krs::sensor
