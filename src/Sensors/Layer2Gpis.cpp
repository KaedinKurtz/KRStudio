#include "Layer2Gpis.hpp"
#include <cmath>

namespace krs::sensor {

ReconField reconstruct(const ReconScene& scene, const DepthModel& model, int K, double sigmaPrior, std::mt19937_64& rng) {
    ReconField f;
    f.W = scene.W; f.H = scene.H;
    f.cells.assign(size_t(scene.W) * scene.H, ReconCell{});

    for (int frame = 0; frame < K; ++frame) {
        for (int i = 0; i < scene.W * scene.H; ++i) {
            if (frame >= scene.framesObserving[i]) continue;     // this frame does not see the cell (occlusion)
            const double z = model.sample(scene.trueDepth[i], scene.material[i], rng);
            if (z == DEPTH_INVALID) continue;                    // sensor hole this frame
            // Welford online mean/variance.
            ReconCell& c = f.cells[i];
            ++c.n;
            const double delta = z - c.mu;
            c.mu += delta / c.n;
            c.m2 += delta * (z - c.mu);
        }
    }

    for (auto& c : f.cells) {
        if (c.n == 0) { c.hole = true; c.sigma = sigmaPrior; c.mu = 0.0; continue; }
        c.hole = false;
        if (c.n >= 2) {
            const double obsVar = c.m2 / (c.n - 1);              // per-observation variance (~ effective sigma_z^2)
            c.sigma = std::sqrt(obsVar / c.n);                  // standard error of the fused estimate
        } else {
            c.sigma = sigmaPrior;                               // a single observation: cannot estimate spread
        }
    }
    return f;
}

} // namespace krs::sensor
