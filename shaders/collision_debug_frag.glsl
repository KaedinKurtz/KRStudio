#version 430 core

uniform vec3 u_color;
// These line overlays are drawn into the HDR buffer BEFORE TonemapPass, so the
// photometric EV exposure (~8e-4) would crush authored LDR colours to black
// (the "bore/collision highlight is black" bug, same as the grid + gold outline).
// Pre-divide by the exposure so the colour survives the tonemap unchanged.
uniform float u_invExposure;

out vec4 FragColor;

void main()
{
    FragColor = vec4(u_color * u_invExposure, 1.0);
}
