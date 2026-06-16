#include "DepthModel.hpp"
#include <cmath>
#include <algorithm>

namespace krs::sensor {

DepthModel DepthModel::fromProfile(const DepthProfile& p) {
    DepthModel m;
    const double kPi = 3.14159265358979;
    m.fx = (double(int(p.width)) * 0.5) / std::tan(0.5 * double(p.fovHdeg) * kPi / 180.0);  // 640 px
    m.baselineM = double(p.baselineMm) * 1e-3;
    m.subpixelErr = double(p.subpixelErr);
    m.minZm = double(p.minZmm) * 1e-3;
    m.maxZm = double(p.maxZm);
    m.speckleStdM = double(p.speckleStdMm) * 1e-3;
    m.flyingPixelRate = double(p.flyingPixelRate);
    m.holeRateLambert = double(p.holeRateLambert);
    m.holeRateSpecular = double(p.holeRateSpecular);
    return m;
}

double DepthModel::rangeSigma(double Zm) const {
    if (!quadraticRange) return constSigmaM;             // NEG-CTRL: flat, no Z dependence
    // propagate sub-pixel disparity error through Z = fx*b/d:  sigma_Z = Z^2 * sigma_d / (fx*b)
    return Zm * Zm * subpixelErr / (fx * baselineM);
}

double DepthModel::sample(double Zm, const MaterialSample& mat, std::mt19937_64& rng) const {
    if (Zm < minZm || Zm > maxZm) return DEPTH_INVALID;  // below min-Z (or beyond range) -> no depth

    if (materialHoles) {
        std::uniform_real_distribution<double> u(0.0, 1.0);
        // holes conditioned on the SHARED material field (Phase 4/5 read the same field -> L2/L3 correlate).
        if (u(rng) < stereoDropProb(mat, holeRateLambert, holeRateSpecular)) return DEPTH_INVALID;
    }

    double z;
    if (quadraticRange) {
        // PHYSICAL stereo: triangulate Z from a sub-pixel-noisy disparity. The quadratic Z^2 range noise
        // EMERGES from Z = fx*b/d (it is NOT injected as a coeff*Z^2 term) -- so the gate can validate the
        // emergent law against an independent hand-computed coefficient rather than the model's own formula.
        const double dTrue = fx * baselineM / Zm;
        std::normal_distribution<double> dn(0.0, subpixelErr);
        const double dNoisy = dTrue + dn(rng);
        if (dNoisy <= 1e-6) return DEPTH_INVALID;        // non-positive disparity -> no stereo match
        z = fx * baselineM / dNoisy;
    } else {
        std::normal_distribution<double> rn(0.0, constSigmaM);   // NEG-CTRL: flat Gaussian, no Z^2
        z = Zm + rn(rng);
    }
    if (addSpeckle) { std::normal_distribution<double> sp(0.0, speckleStdM); z += sp(rng); }
    return z;
}

double DepthModel::edgeSample(double zNearM, double zFarM, bool isEdge, const MaterialSample& mat, std::mt19937_64& rng) const {
    if (isEdge && flyingPixels) {
        std::uniform_real_distribution<double> u(0.0, 1.0);
        if (u(rng) < flyingPixelRate) {
            const double t = u(rng);                     // interpolate -> flying pixel at an intermediate depth
            const double zf = zNearM + t * (zFarM - zNearM);
            return sample(zf, mat, rng);
        }
    }
    return sample(zNearM, mat, rng);                      // foreground surface
}

} // namespace krs::sensor
