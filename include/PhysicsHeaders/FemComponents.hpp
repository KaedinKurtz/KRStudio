#pragma once
// Phase 5 — FEM result component (heavy; only included by FemSystem / FemVizPass,
// not by the global components.hpp, so FemSolver/<future> don't leak everywhere).
#include "FemSolver.hpp"
#include <vector>
#include <array>
#include <cstdint>

namespace krs::fem {

// Published FEM solve output for an entity, consumed by FemVizPass to recolour
// the body's RENDER mesh. Per-render-vertex scalars for each viz mode, plus the
// dedicated GL buffers (local position + active scalar + indices).
struct FemResultComponent {
    // Per render-mesh vertex scalar for each viz mode index (1=Thermal °C,
    // 2=VonMises Pa, 3=Strain -). [0] unused (Default mode = normal PBR).
    std::array<std::vector<float>, 4> vertScalar;
    std::array<glm::vec2, 4> range{};   // min/max per mode (for the shared ramp range)

    // GL buffers (engine context): local positions (loc 0) + scalar (loc 1) + EBO.
    std::uint32_t vao = 0, vboPos = 0, vboScalar = 0, ebo = 0;
    int indexCount = 0;
    int uploadedMode = -1;   // which mode's scalar currently lives in vboScalar
    bool ready = false;      // a solve has been published
    bool buffersBuilt = false;
};

} // namespace krs::fem
