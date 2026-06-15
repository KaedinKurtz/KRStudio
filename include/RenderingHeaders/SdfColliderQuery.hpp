#pragma once
// SdfColliderQuery.hpp -- SINGLE SOURCE OF TRUTH for how a WORLD point maps into an SDF mesh
// collider's LOCAL signed-distance field. The GPU collision shader (fluid_deltap_comp.glsl) mirrors
// this EXACT convention, and GATE C (CollisionSyncGate.cpp) tests it on the CPU:
//
//     local = invModel * world;   uvw = (local - aabbMin) / (aabbMax - aabbMin);   d = field(uvw)
//
// THE FIX (Phase B / C2): the SDF is baked ONCE in the body's LOCAL frame; `invModel` is refreshed
// from the body's LIVE TransformComponent every step, so the distance field RIDES the body. The old
// code baked the field in WORLD space at play time and never refreshed it -> collision happened at
// the start pose forever (a ghost at the vacated pose, nothing at the live pose). With `invModel`
// frozen at the bake pose, that ghost reappears -- the negative control for the gate.

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

namespace krs::fluid {

// SINGLE SOURCE OF TRUTH for an SDF collider's placement matrix: a RIGID model (translation +
// rotation, NO scale). Scale is baked INTO the local field instead (bake transform = scale-only),
// so `mat3(model)` is orthonormal and correctly rotates the local SDF gradient to world for any
// (incl. non-uniform) scale, and the sampled distance stays in world units. Used by the bake, the
// per-step refresh (uploadColliders), and GATE C, so they cannot drift.
inline glm::mat4 sdfRigidModel(const glm::vec3& translation, const glm::quat& rotation)
{
    return glm::translate(glm::mat4(1.0f), translation) * glm::mat4_cast(rotation);
}

struct SdfColliderView {
    glm::mat4 invModel{ 1.0f };       // world -> collider local (refreshed from the live transform)
    glm::vec3 aabbMin{ 0.0f };        // LOCAL-frame field AABB
    glm::vec3 aabbMax{ 0.0f };
    glm::ivec3 dims{ 0 };
    const float* field = nullptr;     // dims.x*dims.y*dims.z signed distances (local == world units, no scale)
};

constexpr float kSdfOutside = 1.0e9f;

// Trilinear signed distance at a WORLD point (rides the body via invModel). Returns +kSdfOutside if
// the mapped local point is outside the field's AABB. Matches GL_LINEAR sampler3D sampling.
float sdfDistanceWorld(const SdfColliderView& c, const glm::vec3& worldPoint);

// GATE C (C1/C2/C4): proves the SDF collision query RIDES a moving body (no start-pose ghost, zero
// lag), with a negative control (frozen transform reproduces the ghost). CPU/OpenVDB, headless;
// gated by KRS_COLLISIONSYNC_SELFTEST + KRS_OVERNIGHT_BENCH.
bool runCollisionSyncGateC();

} // namespace krs::fluid
