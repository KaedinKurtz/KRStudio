#pragma once
// ===========================================================================
// Phase 5 Task 5 — learned-surrogate scaffold. A SurrogateField is a (future)
// trained model that predicts an FEM field (e.g. nodal von Mises) directly from
// (voxel geometry, per-voxel material, boundary conditions), to replace the FEM
// SOLVE for speed once trained. THIS PHASE SHIPS ONLY THE INTERFACE + A STUB +
// THE TRAINING-DATA EXPORTER — the FEM oracle (FemSolver) remains the source of
// truth. There is NO drop-in pretrained FEA foundation model (ROADMAP §L.3);
// the surrogate must be trained on data exported from our own oracle.
//
// The ONNX Runtime inference path is behind KR_WITH_ONNX (build-optional, mirrors
// KR_WITH_OCCT); onnxruntime is a vcpkg port to be added when a model exists.
// ===========================================================================
#include "FemSolver.hpp"
#include <vector>
#include <string>
#include <memory>

namespace krs::fem {

// Interface a trained model implements; the engine calls predict*, and on failure
// (no model / unavailable) falls back to the FEM oracle.
class ISurrogateField {
public:
    virtual ~ISurrogateField() = default;
    virtual bool available() const = 0;  // false -> caller must use the FEM solve
    // Predict per-NODE von Mises stress (Pa) for the model under bc. Returns false
    // if the surrogate cannot serve this input (wrong shape, not loaded, ...).
    virtual bool predictVonMises(const VoxelFemModel& m, const FemMaterial& mat,
                                 const ElasticBC& bc, std::vector<double>& outNodal) = 0;
};

// Default: no surrogate -> always fall back to FEM. This is the active impl this phase.
class NullSurrogate : public ISurrogateField {
public:
    bool available() const override { return false; }
    bool predictVonMises(const VoxelFemModel&, const FemMaterial&, const ElasticBC&,
                         std::vector<double>&) override { return false; }
};

// Factory: returns an OnnxSurrogate (KR_WITH_ONNX + a model path) else NullSurrogate.
std::unique_ptr<ISurrogateField> makeSurrogate(const std::string& onnxModelPath);

// ---------------------------------------------------------------------------
// Training-data exporter — the FEM oracle is the data source. Writes one sample
// (input feature grid + output field) per solve to `dir`, plus a one-time
// FORMAT.txt describing the layout. Documented, CNN/U-Net-ready (the recommended
// architecture for our structured voxel grid; ROADMAP §L.3 / §L.6).
//   input  : float32[IN_CHANNELS * nx*ny*nz]  (per cell, channel-major then z,y,x)
//            channels: 0 occupancy, 1 E/200e9, 2 nu, 3 k/400, 4 cp/1000,
//                      5 fixed-mask, 6 load-mask, 7 heat-source-mask
//   output : float32[OUT_CHANNELS * nx*ny*nz] (per cell, sampled from nodal field)
//            channels: 0 vonMises (Pa), 1 temperature (C), 2 strain-norm
void exportTrainingSample(const std::string& dir, int index,
                          const VoxelFemModel& m, const FemMaterial& mat,
                          const ElasticBC& ebc, const std::vector<int>& sourceCells,
                          const ElasticResult& er, const ThermalResult& tr);

} // namespace krs::fem
