#version 410 core
out vec4 FragColor;

in float g_fade_coord; // The -1 to 1 fade coordinate from the geometry shader

uniform vec4 u_glowColour;
uniform vec4 u_coreColour;

void main()
{
    // Create a soft falloff from the center of the line to the edge
    float falloff = 1.0 - abs(g_fade_coord);
    falloff = pow(falloff, 1.0); // You can tweak this power for different softness

    // 1. Calculate the halo color. This is our "background" layer.
    vec4 halo = vec4(u_glowColour.rgb, u_glowColour.a * falloff);

    // 2. Calculate the core color. This is our "foreground" layer.
    // The core's alpha is calculated to be strong in the center and fade out.
    float core_alpha = smoothstep(0.5, 1.0, falloff);
    vec4 core = vec4(u_coreColour.rgb, u_coreColour.a * core_alpha);

    // 3. Alpha-blend the core ON TOP of the halo.
    // The `mix` function performs linear interpolation: (1-a)*x + a*y
    // We are blending the halo's RGB (x) and the core's RGB (y)
    // using the core's calculated alpha (a) as the mixing factor.
    vec3 final_rgb = mix(halo.rgb, core.rgb, core.a);

    // The final alpha is the combination of both layers, ensuring the whole
    // thing correctly blends with the 3D scene behind it.
    float final_alpha = halo.a + core.a * (1.0 - halo.a);

    FragColor = vec4(final_rgb, final_alpha);
}