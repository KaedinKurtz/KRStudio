#version 430 core

// Additive foam splat (blended MAX-ish via additive + decay clamp).

in float vStrength;

out float outFoam;

void main()
{
    vec2 uv = gl_PointCoord * 2.0 - 1.0;
    float r2 = dot(uv, uv);
    if (r2 > 1.0) discard;
    outFoam = (1.0 - r2) * 0.25 * vStrength;
}
