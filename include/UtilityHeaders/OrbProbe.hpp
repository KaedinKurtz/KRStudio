#pragma once
// ===========================================================================
// Velocity-probe orb: the shared, headless-testable core behind the
// VelocityProbeOrb node. Two responsibilities, both independent of GL/Qt so the
// gates can exercise the EXACT runtime logic:
//   1. averageVelocityInSphere -- the VOLUME containment query: the mean
//      velocity of the particles whose position lies inside the probe sphere
//      (NOT a flux-through-plane, NOT a global average).
//   2. the orb<->node binding lifecycle on an entt::registry (decorate / find /
//      remove / count) -- so spawning, colour assignment, and bidirectional
//      deletion are gateable without the QtNodes graph or a window.
// ===========================================================================
#include <glm/glm.hpp>
#include <entt/entt.hpp>
#include <vector>
#include <cstdint>

namespace krs::orb {

struct OrbVelocity {
    glm::vec3 avg{ 0.0f };
    int count = 0;
};

// Mean velocity of the particles INSIDE the sphere (|pos - center| <= radius). Particles outside the
// sphere contribute nothing; an empty sphere returns {0,0}. positions[i] pairs with velocities[i]
// (both already filtered to the alive set by the caller).
OrbVelocity averageVelocityInSphere(const std::vector<glm::vec3>& positions,
                                    const std::vector<glm::vec3>& velocities,
                                    const glm::vec3& center, float radius);

// Attach the probe-orb components to an existing entity (the runtime first spawns a glass IcoSphere
// mesh via SceneBuilder, then calls this; the gate calls it on a bare entity). Emplaces/updates a
// TransformComponent (center, scale = radius), a GlassComponent tinted by color, and the
// OrbBindingComponent{nodeId, color, radius}; guarantees the entity carries NO AutoCollisionComponent
// (it is a measurement volume, not a rigid body).
void decorateProbeOrb(entt::registry& reg, entt::entity e, std::uint64_t nodeId,
                      const glm::vec3& color, const glm::vec3& center, float radius);

// The orb entity bound to nodeId (entt::null if none).
entt::entity findOrbForNode(entt::registry& reg, std::uint64_t nodeId);

// Destroy the orb entity bound to nodeId. Returns true if one was removed.
bool removeOrbForNode(entt::registry& reg, std::uint64_t nodeId);

// Number of probe orbs currently in the registry.
int orbCount(entt::registry& reg);

// GATE ORB-OWNERSHIP (KRS_ORBOWN_SELFTEST; folded into the bench): the VelocityProbeOrb node's compute()
// reconciles the probe radius (== TransformComponent.scale.x) across a wired Radius, the in-node widget, and the
// transform gizmo, so a gizmo resize PERSISTS instead of being clobbered every eval tick. Neg-control = the old
// unconditional scale write. Headless (drives the real compute() on an entt registry).
bool runOrbOwnershipGate();

} // namespace krs::orb
