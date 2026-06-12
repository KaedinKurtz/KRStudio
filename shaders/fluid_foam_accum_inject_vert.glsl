#version 430 core

// Splat living FOAM-type diffuse particles into the world-anchored
// (top-down XZ) accumulation buffer.

struct Diffuse {
    vec4 posLife;
    vec4 velType;
};
layout(std430, binding = 0) buffer DiffuseBuf { Diffuse d[]; };

uniform vec2 u_worldMin;  // domain XZ min
uniform vec2 u_worldSize; // domain XZ extent

out float vStrength;

void main()
{
    Diffuse dp = d[gl_VertexID];
    float type = dp.velType.w;
    bool foamType = type > 0.5 && type < 1.5;
    vStrength = clamp(dp.posLife.w / 3.0, 0.0, 1.0);

    vec2 uv = (dp.posLife.xz - u_worldMin) / u_worldSize;
    gl_Position = vec4(uv * 2.0 - 1.0, 0.0, 1.0);
    gl_PointSize = 3.0;

    if (dp.posLife.w <= 0.0 || !foamType) gl_Position = vec4(0, 0, -10, 1);
}
