#pragma once
// Composition.hpp -- Phase 5: wires the three layers into one composable, toggleable, deterministic scene.
//   physics reads L1 TRUE; the robot's BELIEF reads L2 (mu,sigma); the CAMERA's live reading is the belief
//   surface observed through L3 (fresh per-frame sensor noise + material dropout). The SHARED material field
//   drives BOTH L2 sigma (baked into the belief) AND L3 dropout/noise -- so they correlate by construction,
//   not by coincident independent draws. Per-layer RNG substreams are drawn in a fixed order from the master
//   seed, so toggling one layer never shifts another's stream (determinism survives composition changes).
#include "DepthModel.hpp"
#include "Layer2Gpis.hpp"
#include <cstdint>
#include <random>

namespace krs::sensor {

struct CompositionConfig {
    bool useL1{true};          // L1 true world (physics)
    bool useL2{true};          // L2 belief field (reconstruction)
    bool useL3{true};          // L3 live per-frame sensor noise
    std::uint64_t seed{0x5E11507Eull};
    int  beliefFrames{24};     // K frames used to build the L2 belief
    double sigmaPrior{0.5};
    bool independentL3{false}; // NEG-CTRL: L3 dropout from a fixed-rate field INDEPENDENT of the material
    double independentDropRate{0.18};
};

class Composition {
public:
    Composition(const ReconScene& scene, const DepthModel& model, const CompositionConfig& cfg);

    int cells() const { return scene_.W * scene_.H; }

    // L1: what physics acts on.
    double physicsDepth(int i) const { return scene_.trueDepth[i]; }
    // L2: the robot's belief (mu, sigma). Falls back to L1 truth when L2 is toggled off.
    double beliefMu(int i) const;
    double beliefSigma(int i) const;
    bool   beliefHole(int i) const;
    const MaterialSample& material(int i) const { return scene_.material[i]; }

    // L3: the camera's live reading = belief-or-truth surface observed through the sensor (noise + dropout).
    // Returns DEPTH_INVALID on a live dropout. Mutates the supplied L3 rng.
    double cameraRead(int i, std::mt19937_64& rngL3) const;

    std::uint64_t seedL2() const { return seedL2_; }
    std::uint64_t seedL3() const { return seedL3_; }

private:
    ReconScene scene_;
    DepthModel model_;
    CompositionConfig cfg_;
    ReconField belief_;
    std::uint64_t seedL2_{0}, seedL3_{0};
};

} // namespace krs::sensor
