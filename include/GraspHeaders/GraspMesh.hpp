#pragma once
// GraspMesh.hpp -- pure-CPU geometric metrics over a loaded mesh (no PhysX): AABB, enclosed volume (signed
// tetrahedra), volume-weighted centroid (center of mass under uniform density), finiteness. Used by GATE
// IMPORT (scale/mass/NaN) and later by the planner (CoM-aware ranking).
#include "components.hpp"   // RenderableMeshComponent, Vertex
#include <glm/glm.hpp>

namespace krs::grasp {

struct MeshMetrics {
    glm::dvec3 aabbMin{0}, aabbMax{0}, extent{0};
    double longest{0}, shortest{0};
    double volume{0};         // |enclosed volume|, m^3 (signed-tet sum)
    double signedVolume{0};   // signed (negative => inward-facing winding)
    glm::dvec3 centroid{0};   // volume-weighted centroid (= CoM if uniform density)
    bool finite{false};       // all positions/normals finite AND volume finite & > 0
    int nVerts{0}, nTris{0};
};

MeshMetrics computeMetrics(const RenderableMeshComponent& mesh);

// scale every vertex position by s (used by the GATE IMPORT negative control: a mm-as-meters x1000 mesh).
RenderableMeshComponent scaledCopy(const RenderableMeshComponent& mesh, double s);

} // namespace krs::grasp
