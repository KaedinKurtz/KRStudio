#pragma once
// DepthModel.hpp -- synthetic STEREO depth (D456-style). Z is triangulated from disparity; the range noise is
// DERIVED by propagating a sub-pixel disparity error through Z = fx*baseline/d, which yields the defining
// quadratic sigma_Z ~ Z^2 law (not a free knob). Plus the structured artifacts a real stereo sensor shows and
// a clean+Gaussian model does NOT: material-conditioned dropouts, edge flying pixels, a per-resolution min-Z.
#include "SensorProfile.hpp"
#include "MaterialField.hpp"
#include <random>

namespace krs::sensor {

constexpr double DEPTH_INVALID = 0.0;   // D456 convention: 0 == no depth (hole / below min-Z)

struct DepthModel {
    double fx{640.0};          // depth focal length (px), from (W/2)/tan(fovH/2)
    double baselineM{0.095};   // stereo baseline (m)
    double subpixelErr{0.1};   // disparity matching error (px) -> the source of the Z^2 range noise
    double minZm{0.52};        // per-resolution min range (m); true Z below this -> invalid
    double maxZm{6.0};
    double speckleStdM{0.003}; // IR-speckle additive floor (m)

    bool   quadraticRange{true}; // false => the NEG-CTRL: constant range sigma (no Z^2)
    bool   materialHoles{true};  // false => the NEG-CTRL: never drop a match
    bool   flyingPixels{true};   // false => the NEG-CTRL: no flying pixels at edges
    bool   addSpeckle{true};
    double constSigmaM{0.02};    // used when !quadraticRange (the clean-Gaussian neg-ctrl)
    double flyingPixelRate{0.25};
    double holeRateLambert{0.01};
    double holeRateSpecular{0.35};

    static DepthModel fromProfile(const DepthProfile& p);

    // analytic range noise std at a true depth (m). Quadratic when enabled, else constant.
    double rangeSigma(double Zm) const;

    // one stereo depth sample for a surface point at true depth Zm with the given material.
    // Returns a noisy Z (m), or DEPTH_INVALID for a hole / below-min-Z pixel.
    double sample(double Zm, const MaterialSample& mat, std::mt19937_64& rng) const;

    // depth for a pixel that may straddle a near/far discontinuity. At an edge a fraction (flyingPixelRate)
    // of pixels interpolate to an INTERMEDIATE depth between zNear and zFar (the flying-pixel artifact).
    double edgeSample(double zNearM, double zFarM, bool isEdge, const MaterialSample& mat, std::mt19937_64& rng) const;
};

} // namespace krs::sensor
