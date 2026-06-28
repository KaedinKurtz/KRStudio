#pragma once
// MaterialApply.hpp -- SINGLE SOURCE OF TRUTH for the material-tag mutation performed when a
// texture pack is applied to a body. Consumed by BOTH TextureBrowserWidget::applyToSelection
// (the real UI path) and the applied-texture gate (AC3), so the gate exercises the exact tag
// mutation the UI performs.
//
// THE RULE (see GBufferShaderSelect.hpp): bodies with REAL per-vertex UVs (UVTexturedMaterialTag)
// STAY on their object-space UV mapping when a pack is applied -- they keep UVTexturedMaterialTag
// and never gain TriPlanarMaterialTag, so the applied texture rides the body in its own frame.
// A height-map (parallax) pack still records ParallaxMaterialTag (harmless on a UV body: there is
// no UV+parallax shader, so the UV path renders the albedo without displacement -- UV correctness
// wins), but ONLY primitives without UVs switch to world-space triplanar.

#include <entt/entt.hpp>
#include "components.hpp"

namespace krs::material {

// Mutate the tags on `e` to reflect applying a pack. `packHasHeightMap` == the pack ships a
// height/parallax map. Does NOT touch MaterialDirectoryTag/MaterialReloadRequest/MaterialComponent
// (the caller owns those); this is purely the render-path tag policy.
inline void applyPackTags(entt::registry& reg, entt::entity e, bool packHasHeightMap)
{
    if (packHasHeightMap) {
        reg.emplace_or_replace<ParallaxMaterialTag>(e);
        // Real-UV bodies keep UVTexturedMaterialTag (object space). Only no-UV primitives go
        // to the world-space triplanar/parallax path.
        if (!reg.all_of<UVTexturedMaterialTag>(e)) {
            reg.emplace_or_replace<TriPlanarMaterialTag>(e);
        }
    } else {
        // Plain pack: no parallax.
        reg.remove<ParallaxMaterialTag>(e);
        // A no-UV primitive must STILL take the world-space triplanar path. Without this it
        // falls through to the UV shader (selectGBufferShaderKind's hasTexture fallback) and
        // samples its nonexistent per-vertex UVs -> the object renders BLACK. Real-UV bodies
        // (UVTexturedMaterialTag) keep their object-space UV mapping.
        if (!reg.all_of<UVTexturedMaterialTag>(e))
            reg.emplace_or_replace<TriPlanarMaterialTag>(e);
    }
}

} // namespace krs::material
