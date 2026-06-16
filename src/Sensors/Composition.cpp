#include "Composition.hpp"

namespace krs::sensor {

Composition::Composition(const ReconScene& scene, const DepthModel& model, const CompositionConfig& cfg)
    : scene_(scene), model_(model), cfg_(cfg) {
    // Per-layer substreams drawn in a FIXED order from the master seed -> toggling a layer never shifts the
    // other layer's stream (deterministic regardless of which layers are active).
    std::mt19937_64 master(cfg_.seed);
    seedL2_ = master();
    seedL3_ = master();
    if (cfg_.useL2) {
        std::mt19937_64 rngL2(seedL2_);
        belief_ = reconstruct(scene_, model_, cfg_.beliefFrames, cfg_.sigmaPrior, rngL2);
    }
}

double Composition::beliefMu(int i) const {
    if (cfg_.useL2 && !belief_.cells.empty() && !belief_.cells[i].hole) return belief_.cells[i].mu;
    return scene_.trueDepth[i];     // L2 off (or hole) -> fall back to truth
}

double Composition::beliefSigma(int i) const {
    if (cfg_.useL2 && !belief_.cells.empty()) return belief_.cells[i].sigma;
    return 0.0;
}

bool Composition::beliefHole(int i) const {
    return cfg_.useL2 && !belief_.cells.empty() && belief_.cells[i].hole;
}

double Composition::cameraRead(int i, std::mt19937_64& rngL3) const {
    // base surface the camera looks at: the L2 belief if active, else the L1 truth.
    const double base = (cfg_.useL2 && !belief_.cells.empty() && !belief_.cells[i].hole)
                            ? belief_.cells[i].mu
                            : scene_.trueDepth[i];
    if (!cfg_.useL3) return base;   // no live layer -> clean read of the base surface

    if (cfg_.independentL3) {
        // NEG-CTRL: dropout decided by a fixed rate INDEPENDENT of the material (breaks the shared cause),
        // then the same range noise. Correlation between L2 sigma and L3 dropout should vanish.
        std::uniform_real_distribution<double> u(0.0, 1.0);
        if (u(rngL3) < cfg_.independentDropRate) return DEPTH_INVALID;
        DepthModel clean = model_; clean.materialHoles = false;
        return clean.sample(base, scene_.material[i], rngL3);
    }
    // real: observe the base surface through the live sensor; the SHARED material drives noise + dropout.
    return model_.sample(base, scene_.material[i], rngL3);
}

} // namespace krs::sensor
