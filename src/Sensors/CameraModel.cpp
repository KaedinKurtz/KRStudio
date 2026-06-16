#include "CameraModel.hpp"

namespace krs::sensor {

Intrinsics Intrinsics::fromRgb(const RgbProfile& p) {
    Intrinsics k;
    const double pitchMm = double(p.pixelUm) * 1e-3;     // micron -> mm
    k.fx = double(p.focalMm) / pitchMm;                  // focal length in pixels
    k.fy = k.fx;                                         // square pixels (D456 RGB)
    k.cx = (double(int(p.width)) - 1.0) * 0.5;           // principal point at sensor centre
    k.cy = (double(int(p.height)) - 1.0) * 0.5;
    return k;
}

Distortion Distortion::fromRgb(const RgbProfile& p) {
    return Distortion{ double(p.k1), double(p.k2), double(p.k3), double(p.p1), double(p.p2) };
}

glm::dvec2 distortNormalized(const glm::dvec2& n, const Distortion& d) {
    const double x = n.x, y = n.y;
    const double r2 = x * x + y * y;
    const double radial = 1.0 + d.k1 * r2 + d.k2 * r2 * r2 + d.k3 * r2 * r2 * r2;
    const double xd = x * radial + 2.0 * d.p1 * x * y + d.p2 * (r2 + 2.0 * x * x);
    const double yd = y * radial + d.p1 * (r2 + 2.0 * y * y) + 2.0 * d.p2 * x * y;
    return { xd, yd };
}

glm::dvec2 undistortNormalized(const glm::dvec2& dist, const Distortion& d, int iters) {
    // Fixed-point: solve distort(x,y) == dist for (x,y). Start from the distorted point.
    double x = dist.x, y = dist.y;
    for (int i = 0; i < iters; ++i) {
        const double r2 = x * x + y * y;
        const double radial = 1.0 + d.k1 * r2 + d.k2 * r2 * r2 + d.k3 * r2 * r2 * r2;
        const double dx = 2.0 * d.p1 * x * y + d.p2 * (r2 + 2.0 * x * x);   // tangential
        const double dy = d.p1 * (r2 + 2.0 * y * y) + 2.0 * d.p2 * x * y;
        x = (dist.x - dx) / radial;
        y = (dist.y - dy) / radial;
    }
    return { x, y };
}

glm::dvec2 project(const glm::dvec2& n, const Intrinsics& K, const Distortion& d) {
    const glm::dvec2 dd = distortNormalized(n, d);
    return { K.fx * dd.x + K.cx, K.fy * dd.y + K.cy };
}

glm::dvec2 unproject(const glm::dvec2& px, const Intrinsics& K, const Distortion& d, int iters) {
    const glm::dvec2 dist{ (px.x - K.cx) / K.fx, (px.y - K.cy) / K.fy };   // distorted normalized
    return undistortNormalized(dist, d, iters);
}

} // namespace krs::sensor
