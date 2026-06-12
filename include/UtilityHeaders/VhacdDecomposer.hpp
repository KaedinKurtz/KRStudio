#pragma once

#include <glm/glm.hpp>
#include <vector>

struct Vertex; // components.hpp

namespace krs {

/// Approximate convex decomposition (V-HACD 4): splits a concave mesh into
/// hulls that together preserve its cavities — the best collision a dynamic
/// concave body can get on the CPU solver. Runs SYNCHRONOUSLY (seconds for
/// large meshes) — call from a worker thread only.
/// Returns one point cloud per hull (already float, PhysX-cookable).
std::vector<std::vector<glm::vec3>>
decomposeMesh(const std::vector<Vertex>& vertices,
              const std::vector<unsigned int>& indices,
              int maxHulls = 16,
              int voxelResolution = 100000,
              int maxVertsPerHull = 64);

} // namespace krs
