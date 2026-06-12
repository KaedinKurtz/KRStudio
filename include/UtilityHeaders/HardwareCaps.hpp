#pragma once

#include <string>

namespace krs {

/**
 * @brief Hardware capabilities probed once at engine init (by the PhysX
 * core bring-up) and read anywhere a backend decision is made:
 * fluid solver tier selection, PhysX GPU rigid dynamics, SDF dynamic
 * trimesh collision. On non-NVIDIA machines everything falls back to the
 * CPU/GL-compute tiers automatically.
 */
struct HardwareCaps {
    bool probed = false;
    bool cudaPhysics = false;   // PxCudaContextManager created and valid
    std::string cudaDeviceName; // CUDA device name when available
};

HardwareCaps& hardwareCaps();

} // namespace krs
