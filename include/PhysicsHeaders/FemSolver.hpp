#pragma once
// ===========================================================================
// Phase 5 — FEM oracle. Linear-elastic stress + heat conduction on a body's
// volume, discretised as an immersed voxel/hexahedral FE mesh on a regular
// background grid (occupancy from a predicate / SDF / surface mesh). Trilinear
// 8-node hexes; one precomputed element matrix reused for every (cubic) cell of
// a material; sparse SPD assembly solved with Eigen (SimplicialLDLT). This is
// the IMPLICIT oracle, so it handles REAL metal modulus (no explicit-MPM
// stiffness ceiling). CPU/Eigen only — no GL, no Qt; runnable on a worker thread.
//
// Discretisation decision + trade-offs: ROADMAP §L.
// ===========================================================================
#include <glm/glm.hpp>
#include <vector>
#include <array>
#include <functional>
#include <future>

namespace krs::fem {

struct FemMaterial {
    double E   = 68.9e9;  // Young's modulus (Pa)        — 6061-T6 default
    double nu  = 0.33;    // Poisson ratio (-)
    double rho = 2700.0;  // density (kg/m^3)
    double k   = 167.0;   // thermal conductivity (W/m.K)
    double cp  = 896.0;   // specific heat (J/kg.K)
};

// Voxel-hex discretisation: cells flagged solid become trilinear 8-node hexes;
// shared nodes are merged; only nodes touching a solid cell get a DOF.
struct VoxelFemModel {
    glm::dvec3 origin{ 0.0 };  // world position of grid node (0,0,0)
    double h = 0.05;           // cubic cell size (m)
    int nx = 0, ny = 0, nz = 0;          // CELL counts per axis
    std::vector<int> nodeId;             // (nx+1)(ny+1)(nz+1); active DOF index or -1
    std::vector<glm::dvec3> nodePos;     // active node world positions (numNodes)
    std::vector<std::array<int, 8>> elements; // 8 global node indices per solid cell
    int numNodes = 0;

    int gridIdx(int i, int j, int k) const { return (k * (ny + 1) + j) * (nx + 1) + i; }
    bool valid() const { return numNodes > 0 && !elements.empty(); }
    // Nearest active node to a world point (brute force; small models).
    int nearestNode(const glm::dvec3& p) const;
};

struct ElasticBC {
    std::vector<int> fixedNodes;                          // u = 0 (all 3 DOF)
    std::vector<std::pair<int, glm::dvec3>> nodalForces;  // (node, force N)
    glm::dvec3 gravity{ 0.0, 0.0, 0.0 };                  // body-force accel (m/s^2)
};
struct ElasticResult {
    std::vector<glm::dvec3> displacement;  // per node (m)
    std::vector<double> vonMises;          // per node (Pa), element-averaged
    std::vector<double> strainNorm;        // per node, ||Green strain|| (-)
    double maxVonMises = 0.0, maxDisp = 0.0, maxStrain = 0.0;
    bool ok = false;
};

struct ThermalBC {
    std::vector<std::pair<int, double>> dirichlet;    // (node, T °C) fixed
    std::vector<std::pair<int, double>> nodalSource;  // (node, W) lumped heat input
    double ambientT = 20.0;
    double convection = 0.0;  // lumped h·A per surface node (W/K); 0 = adiabatic
    std::vector<int> surfaceNodes;  // nodes the convection applies to
};
struct ThermalResult {
    std::vector<double> temperature;  // per node (°C)
    double minT = 0.0, maxT = 0.0;
    bool ok = false;
};

class FemSolver {
public:
    // --- discretisation ---
    // Generic: flag a cell solid if `inside(cellCenterWorld)` is true.
    static VoxelFemModel voxelize(const glm::dvec3& origin, double h, int nx, int ny, int nz,
                                  const std::function<bool(const glm::dvec3&)>& inside);
    // Axis-aligned solid box at resolution h (all cells solid).
    static VoxelFemModel voxelizeBox(const glm::dvec3& center, const glm::dvec3& half, double h);
    // Surface triangle mesh -> occupancy via robust inside test (libigl winding number).
    static VoxelFemModel voxelizeMesh(const std::vector<glm::vec3>& verts,
                                      const std::vector<unsigned int>& indices, double h);

    // --- solves (synchronous; Eigen SimplicialLDLT) ---
    static ElasticResult solveElastic(const VoxelFemModel& m, const FemMaterial& mat, const ElasticBC& bc);
    static ThermalResult solveThermalSteady(const VoxelFemModel& m, const FemMaterial& mat, const ThermalBC& bc);
    static ThermalResult stepThermalTransient(const VoxelFemModel& m, const FemMaterial& mat,
                                              const ThermalBC& bc, const std::vector<double>& Tprev, double dt);

    // --- async (worker pool; mirrors TrajectoryVerifier) ---
    static std::future<ElasticResult> solveElasticAsync(VoxelFemModel m, FemMaterial mat, ElasticBC bc);
    static std::future<ThermalResult> solveThermalSteadyAsync(VoxelFemModel m, FemMaterial mat, ThermalBC bc);

    // KRS_FEM_SELFTEST: axial bar (exact), cantilever vs Euler-Bernoulli,
    // 1D-bar steady conduction vs linear gradient, plate-with-hole concentration.
    static bool runSelfTests();
};

} // namespace krs::fem
