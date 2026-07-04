#pragma once
// ===========================================================================
// FACE MATERIAL SERVICE -- the "color this entire body" vs "color just this face" ops.
//
// WHOLE BODY: writes the entity's MaterialComponent (albedo/metallic/roughness) -- the existing,
// already-rendered path.
// THIS FACE:  records a FaceMaterial in the entity's FaceMaterialComponent[faceId], then
// regenerates OVERLAY sub-meshes -- one child entity per painted face, its triangles offset a hair
// along the surface normal, carrying the override MaterialComponent. Reuses the whole existing
// render path (every entity renders with its own MaterialComponent); no gbuffer/vertex-format
// change. Overlays are parented to the body so they move with it (robot links included).
// ===========================================================================
#include <glm/glm.hpp>
#include <entt/entt.hpp>
#include "components.hpp"

class Scene;

namespace krs::facemat {

// WHOLE-BODY: set the entity's base MaterialComponent PBR channels.
void applyToBody(entt::registry& reg, entt::entity e, const FaceMaterial& m);

// THIS-FACE: record the override for B-Rep face `faceId` (>=0). Does NOT rebuild overlays -- call
// rebuildFaceOverlays after a batch of edits. Returns false if the entity has no B-Rep faces.
bool applyToFace(entt::registry& reg, entt::entity e, int faceId, const FaceMaterial& m);

// Remove one face's override (revert it to the body material). Returns true if one was removed.
bool clearFace(entt::registry& reg, entt::entity e, int faceId);

// Regenerate the overlay sub-meshes for `e` from its FaceMaterialComponent. Destroys the previous
// overlays first (idempotent). Safe to call on an entity with no FaceMaterialComponent (no-op).
// `scene` is needed to create child entities in the same registry. Returns the overlay count.
int rebuildFaceOverlays(Scene& scene, entt::entity e);

// Destroy every overlay of `e` (e.g. before deleting the body). Leaves the data map intact.
void destroyOverlays(entt::registry& reg, entt::entity e);

// Headless gate (KRS_FACEMAT_SELFTEST): whole-body write; per-face record + overlay generation
// (right triangle counts + colors); clearing a face drops its overlay; NEG-CTRL: a face id with no
// triangles produces no overlay. Returns true iff all pass.
bool runFaceMaterialGate();

} // namespace krs::facemat
