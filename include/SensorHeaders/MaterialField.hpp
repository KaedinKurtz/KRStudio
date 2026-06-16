#pragma once
// MaterialField.hpp -- the SHARED material cause that correlates Layer-2 (reconstruction uncertainty) with
// Layer-3 (live sensor dropout). A specular or dark patch is BOTH a recon hole (high L2 sigma) AND a live
// stereo dropout (L3) -- because BOTH read this same MaterialSample, not independent draws. In Phase 2 only
// the L3 stereo-dropout consumer exists; Phase 4/5 add the L2 consumer reading the SAME field. Maps from the
// engine's MaterialComponent (metallic/roughness/specular/albedo) -- that wiring lands with composition.
#include "SensorProfile.hpp"
#include <algorithm>

namespace krs::sensor {

struct MaterialSample {
    double specular{0.0};      // 0 = Lambertian, 1 = mirror (specular highlights break stereo matching)
    double albedo{0.85};       // surface brightness 0..1 (dark surfaces absorb the IR pattern -> no match)
    double incidenceCos{1.0};  // cos(view angle); grazing incidence (->0) weakens the return
};

// The single shared driver in [0,1]: 0 = clean bright Lambertian, 1 = fully specular OR dark. Both the L3
// stereo dropout (Phase 2) and the L2 recon uncertainty (Phase 4) read THIS, so they correlate by construction.
inline double materialDrive(const MaterialSample& m) {
    const double specDrive = std::clamp(m.specular, 0.0, 1.0);                 // specular breaks matching
    const double darkDrive = std::clamp(1.0 - m.albedo / 0.5, 0.0, 1.0);      // dark below albedo 0.5
    return std::clamp(std::max(specDrive, darkDrive), 0.0, 1.0);
}

// Stereo-match dropout probability for a material. Bright Lambertian -> lambertRate; specular OR dark -> specularRate.
inline double stereoDropProb(const MaterialSample& m, double lambertRate, double specularRate) {
    const double drive = materialDrive(m);
    return lambertRate * (1.0 - drive) + specularRate * drive;
}
inline double stereoDropProb(const MaterialSample& m, const DepthProfile& p) {
    return stereoDropProb(m, double(p.holeRateLambert), double(p.holeRateSpecular));
}

} // namespace krs::sensor
