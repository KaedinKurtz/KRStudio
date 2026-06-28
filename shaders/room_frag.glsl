#version 450 core
// Flat "grey room" background for the Robot View: replaces the environment
// skybox with a uniform, gently-shaded grey (no horizon line), like a level
// editor. Paired with skybox_vert.glsl (far-plane z=w trick + translation
// stripped), so TexCoords is the per-pixel view direction.
out vec4 fragColor;

in vec3 TexCoords;

uniform vec3  u_roomColor;    // LDR grey (e.g. ~0.5)
uniform float u_invExposure;  // 1 / exposureMultiplier(): keeps the grey at the
                              // authored LDR value after the photometric tonemap

void main()
{
    // A whisper of vertical shading (brighter overhead, a touch darker low) gives
    // depth without a hard horizon: smooth, never a visible seam.
    vec3 dir = normalize(TexCoords);
    float t  = clamp(dir.y * 0.5 + 0.5, 0.0, 1.0);   // 0 below, 1 above
    float shade = mix(0.82, 1.10, t);                // subtle, no horizon line
    fragColor = vec4(u_roomColor * shade * u_invExposure, 1.0);
}
