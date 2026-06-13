#pragma once

#include "components.hpp"

#include <glm/glm.hpp>
#include <vector>

namespace krs {

/**
 * @brief Hero-still surface reconstruction: turns fluid particle positions
 * into a REAL triangle mesh (OpenVDB ParticlesToLevelSet -> LevelSetFilter
 * smoothing -> volumeToMesh). Unlike the screen-space surface this is a
 * view-independent, film-quality surface you can light, texture and keep.
 * CPU-side; a 100k-particle frame takes a few seconds.
 */
bool meshFluidParticles(const std::vector<glm::vec3>& positions, float particleRadius,
                        RenderableMeshComponent& out);

} // namespace krs
