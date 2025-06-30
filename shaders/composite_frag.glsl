#version 410 core
// -----------------------------------------------------------------------------
// Re-combines the linear scene colour with the blurred glow.
// Outputs *linear* RGB – the driver converts to sRGB if GL_FRAMEBUFFER_SRGB
// is enabled (see the C++ tweak below).
// -----------------------------------------------------------------------------
in  vec2 vUV;
layout(location = 0) out vec4 fragColor;

uniform sampler2D sceneTexture;   // unit 0
uniform sampler2D glowTexture;    // unit 1
uniform float     glowIntensity   = 1.0;
uniform float     exposure        = 1.0;   // simple tone-map knob

void main()
{
    vec3 scene = texture(sceneTexture, vUV).rgb;
    vec3 glow  = texture(glowTexture , vUV).rgb * glowIntensity;

    // --- screen-style blend (1 - (1-A)(1-B)) -----------------------
    vec3 blended = 1.0 - (1.0 - scene) * (1.0 - glow);

    // --- minimalist Reinhard tone-mapping --------------------------
    vec3 mapped  = vec3(1.0) - exp( -blended * exposure );

    fragColor = vec4(mapped, 1.0);   // ***linear*** output
}
