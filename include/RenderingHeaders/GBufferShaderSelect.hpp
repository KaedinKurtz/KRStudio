#pragma once
// GBufferShaderSelect.hpp -- SINGLE SOURCE OF TRUTH for which deferred G-buffer shader a body
// uses, given its material tags. Consumed by BOTH OpaquePass (the real draw path) and the
// applied-texture gate (AC1/AC3), so the gate is non-vacuous: it exercises the exact decision
// the renderer makes. Changing the policy here changes both, and the gate re-validates it.
//
// THE RULE (the operator's bug, encoded as policy): a body with REAL per-vertex UVs
// (UVTexturedMaterialTag, e.g. imported CAD) ALWAYS samples its material in OBJECT space through
// those UVs -- "UV wins". Even if a material apply added TriPlanar/Parallax tags (e.g. a
// height-map pack), the UV path is used, so the texture rides the body in its own frame instead
// of being projected from world space. Only primitives WITHOUT UVs fall back to world-space
// triplanar / parallax-POM.

namespace krs::render {

enum class GBufferShaderKind {
    Untextured,            // gbuffer_untextured     (flat colour, no albedoMap)
    UVTextured,            // gbuffer_textured        (object-space, samples vertex UVs)  <- UV wins
    Triplanar,             // gbuffer_triplanar       (world-space projection)
    Tessellated,           // gbuffer_tessellated
    TessellatedTriplanar,  // gbuffer_tessellated_triplanar
    ParallaxPOM,           // gbuffer_triplanar_pom   (world-space parallax)
};

// Pure decision. Inputs are the raw tag presence + whether the body has an albedo texture.
// hasUVTag overrides the world-space tags (see THE RULE above). Mirrors the if/else ladder that
// used to live inline in OpaquePass::execute().
inline GBufferShaderKind selectGBufferShaderKind(bool hasUVTag,
                                                 bool hasTessellatedTag,
                                                 bool hasTriPlanarTag,
                                                 bool hasParallaxTag,
                                                 bool hasTexture)
{
    // UV wins: a body with real UVs never takes a world-space (triplanar/parallax) path.
    const bool isTriPlanar = !hasUVTag && hasTriPlanarTag;
    const bool isParallax  = !hasUVTag && hasParallaxTag;

    if (isParallax && isTriPlanar && hasTexture)      return GBufferShaderKind::ParallaxPOM;
    if (hasTessellatedTag && isTriPlanar && hasTexture) return GBufferShaderKind::TessellatedTriplanar;
    if (hasTessellatedTag && hasTexture)              return GBufferShaderKind::Tessellated;
    if (isTriPlanar && hasTexture)                    return GBufferShaderKind::Triplanar;
    if (hasTexture)                                   return GBufferShaderKind::UVTextured;
    return GBufferShaderKind::Untextured;
}

// True iff the selected kind samples in world space (the "swimming" projection). Used by the
// gate's assertions and negative control.
inline bool isWorldSpaceKind(GBufferShaderKind k)
{
    return k == GBufferShaderKind::Triplanar
        || k == GBufferShaderKind::TessellatedTriplanar
        || k == GBufferShaderKind::ParallaxPOM;
}

} // namespace krs::render
