#pragma once
// Phase 5 — FEM system: drives the async FEM oracle for FemBodyComponent entities,
// interpolates the nodal field onto each body's render-mesh vertices, and uploads
// the per-vertex scalar so FemVizPass can recolour the SOLID surface through the
// same ramp + (shared with MPM) dynamic range. Runs on the engine GL thread.
#include <entt/entt.hpp>
#include <unordered_map>
#include <future>
#include <vector>
#include <glm/glm.hpp>
#include "FemSolver.hpp"

class QOpenGLFunctions_4_3_Core;
class MpmSystem;

namespace krs::fem {

class FemSystem {
public:
    // Launch/poll async solves, publish results, upload the active-mode scalar.
    // `vizMode` is the active visualization mode (0 Default,1 Thermal,2 VonMises,3 Strain).
    // Unions the FEM scalar range into `mpm` so MPM + FEM share one range.
    void update(entt::registry& reg, QOpenGLFunctions_4_3_Core* gl, MpmSystem* mpm, int vizMode);
    void shutdown(QOpenGLFunctions_4_3_Core* gl);

private:
    struct Solve {
        bool running = false, hasThermal = false;
        std::future<ElasticResult> elastic;
        std::future<ThermalResult> thermal;
        VoxelFemModel model;                       // world-space voxel model
        std::vector<glm::vec3> localVerts;         // render-mesh local positions
        std::vector<glm::dvec3> worldVerts;        // for nodal->vertex sampling
        std::vector<unsigned int> indices;
        FemMaterial material;                      // for the training-data export
        ElasticBC ebc;                             // retained for export (fixed/load masks)
        std::vector<int> sourceCells;              // cells overlapping a heat source
    };
    std::unordered_map<entt::entity, Solve> m_solves;
    int m_exportIndex = 0;                         // KRS_FEM_EXPORT running sample index
};

} // namespace krs::fem
