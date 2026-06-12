#version 430 core
// Whitewater sprites: small soft points, attributeless from the diffuse SSBO.

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

    float r = u_particleRadius * 0.45;
    float pixels = u_viewportHeight * u_projection[1][1] * r / max(0.05, -viewPos.z);
    gl_PointSize = clamp(pixels, 1.0, 24.0);

    if (vLife <= 0.0 || -viewPos.z < 0.25) gl_Position = vec4(0, 0, -10, 1);
}
