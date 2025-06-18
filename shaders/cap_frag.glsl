#version 410 core
out vec4 FragColor;

in vec2 g_uv; // Interpolated UVs from the geometry shader (-1 to 1)

uniform vec4 u_glowColour;
uniform vec4 u_coreColour;

void main()
{
    // Calculate the distance from the center of the quad (0,0)
    float dist = length(g_uv);

    // If the pixel is outside our circle (radius 1.0), discard it completely
    if (dist > 1.0) {
        discard;
    }

    // Use the distance to create the same glow falloff effect as the lines
    float falloff = 1.0 - dist;
    falloff = pow(falloff, 1.0);

    float core_strength = smoothstep(0.5, 1.0, falloff);
    vec4 core = vec4(u_coreColour.rgb, u_coreColour.a * core_strength);
    vec4 halo = vec4(u_glowColour.rgb, u_glowColour.a * falloff);

    // Blend the core over the halo
    vec3 mixed_rgb = mix(halo.rgb, core.rgb, core.a);
    float final_alpha = halo.a + core.a * (1.0 - halo.a);

    FragColor = vec4(mixed_rgb, final_alpha);
}