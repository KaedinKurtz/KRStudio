#pragma once
// CameraModel.hpp -- the RGB camera GEOMETRY: pinhole intrinsics K derived from focal length + sensor pixel
// pitch (NOT hand-set fx), and Brown-Conrady lens distortion (k1,k2,k3 radial; p1,p2 tangential). Pure CPU
// double math. project() maps an undistorted normalized ray -> distorted pixel; unproject() inverts it
// (iterative undistort). The pinhole-only path (zero distortion) is the FAILING negative control for GATE
// INTRINSICS -- it diverges from the lens toward the image edges.
#include "SensorProfile.hpp"
#include <glm/glm.hpp>

namespace krs::sensor {

struct Intrinsics {
    double fx{0}, fy{0}, cx{0}, cy{0};
    // fx = focalMm / pixelPitch_mm (px). fy = fx (square pixels, D456 RGB). principal point at sensor centre.
    static Intrinsics fromRgb(const RgbProfile& p);
};

struct Distortion {
    double k1{0}, k2{0}, k3{0}, p1{0}, p2{0};
    static Distortion fromRgb(const RgbProfile& p);
    static Distortion none() { return Distortion{}; }   // pinhole (the neg-ctrl)
    bool isPinhole() const { return k1 == 0 && k2 == 0 && k3 == 0 && p1 == 0 && p2 == 0; }
};

// Brown-Conrady forward: undistorted normalized (x,y) -> distorted normalized (xd,yd).
glm::dvec2 distortNormalized(const glm::dvec2& nrm, const Distortion& d);
// inverse: distorted normalized -> undistorted normalized, by fixed-point iteration.
glm::dvec2 undistortNormalized(const glm::dvec2& dist, const Distortion& d, int iters = 20);

// undistorted normalized ray -> distorted pixel.
glm::dvec2 project(const glm::dvec2& nrm, const Intrinsics& K, const Distortion& d);
// distorted pixel -> undistorted normalized ray.
glm::dvec2 unproject(const glm::dvec2& px, const Intrinsics& K, const Distortion& d, int iters = 20);

} // namespace krs::sensor
