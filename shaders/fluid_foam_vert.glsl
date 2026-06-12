#version 430 core
// Whitewater sprites: small soft points, attributeless from the diffuse SSBO.
// Size grows over the particle's first moments then shrinks as it dissolves
// (real foam bubbles coalesce, then pop).

struct Diffuse {
    vec4 posLife;
    vec4 velType;
};
layout(std430, binding = 0) buffer DiffuseBuf { Diffuse d[]; };

uniform mat4 u_view;
uniform mat4 u_projection;
uniform float u_particleRadius;
uniform float u_viewportHeight;

out float vLife;
out float vType;

void main()
{
    Diffuse dp = d[gl_VertexID];
    vLife = dp.posLife.w;
    vType = dp.velType.w;

    vec4 viewPos = u_view * vec4(dp.posLife.xyz, 1.0);
    gl_Position = u_projection * viewPos;

    // Size over life: pops in fast, lingers, shrinks away. Lifetimes are
    // 2-5 s so normalise against a nominal 3 s.
    float lifeFrac = clamp(vLife / 3.0, 0.0, 1.0);
    float sizeCurve = mix(0.25, 0.65, smoothstep(0.0, 0.35, lifeFrac));
    float r = u_particleRadius * sizeCurve;
    float pixels = u_viewportHeight * u_projection[1][1] * r / max(0.05, -viewPos.z);
    gl_PointSize = clamp(pixels, 1.0, 28.0);

    if (vLife <= 0.0 || -viewPos.z < 0.25) gl_Position = vec4(0, 0, -10, 1);
}
