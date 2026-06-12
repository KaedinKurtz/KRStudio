#pragma once

#include <glm/glm.hpp>
#include <vector>

struct Vertex; // components.hpp

/// Output of an OpenVDB mesh -> signed distance field bake.
struct SdfBakeResult {
    std::vector<float> field; // dims.x * dims.y * dims.z, world-unit distances
    glm::ivec3 dims{ 0 };
    glm::vec3 aabbMin{ 0 };
    glm::vec3 aabbMax{ 0 };
};

/// Bakes a world-transformed triangle mesh into a dense SDF block.
/// Returns false (with a warning) when OpenVDB is unavailable or the bake
/// fails. Isolated in its own TU: OpenVDB 12 headers require /permissive-.
bool bakeMeshToSdf(const std::vector<Vertex>& vertices,
                   const std::vector<unsigned int>& indices,
                   const glm::mat4& worldTransform,
                   float voxelSize,
                   SdfBakeResult& out);
