#pragma once
// Layer2Gpis.hpp -- LAYER 2: the reconstruction-uncertainty field. The robot's BELIEF reads this, NOT the L1
// truth. It is L1 observed through the Phase-2 depth sensor (quadratic noise + material-driven holes + match
// noise) and FUSED over frames into a per-cell (mu, sigma^2): mu = depth estimate, sigma = how uncertain the
// reconstruction is there. sigma is HIGH where poorly observed (specular/dark/occluded) and LOW where many
// clean observations agree -- a GPIS-style belief (per-cell Bayesian fusion: mean + calibrated uncertainty).
#include "DepthModel.hpp"
#include "MaterialField.hpp"
#include <vector>
#include <random>

namespace krs::sensor {

struct ReconCell {
    double mu{0.0};        // fused depth estimate (m)
    double sigma{0.0};     // uncertainty of the estimate (m), == standard error sqrt(obsVar/n)
    double m2{0.0};        // Welford running sum of squared deviations
    int    n{0};           // valid observation count
    bool   hole{true};     // true == never observed (recon hole)
};

struct ReconField {
    int W{0}, H{0};
    std::vector<ReconCell> cells;   // row-major W*H
    const ReconCell& at(int u, int v) const { return cells[size_t(v) * W + u]; }
    ReconCell& at(int u, int v) { return cells[size_t(v) * W + u]; }
};

// Inputs to the reconstruction (all W*H, row-major).
struct ReconScene {
    int W{0}, H{0};
    std::vector<double> trueDepth;        // L1 ground-truth depth (m)
    std::vector<MaterialSample> material; // per-cell material (the shared field)
    std::vector<int> framesObserving;     // how many of the K frames see this cell (0 == occluded)
};

// Observe the scene through `model` over K frames and fuse into a (mu, sigma) field. sigmaPrior is the
// uncertainty assigned to never-observed (recon-hole) cells.
ReconField reconstruct(const ReconScene& scene, const DepthModel& model, int K, double sigmaPrior, std::mt19937_64& rng);

} // namespace krs::sensor
