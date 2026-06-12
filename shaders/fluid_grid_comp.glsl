#version 430 core
// PBF stage 2: insert particles into the uniform grid (atomic linked lists).
layout(local_size_x = 256) in;

struct Particle {
    vec4 posLife;
    vec4 vel;
    vec4 pred;
};
layout(std430, binding = 0) buffer Particles { Particle p[]; };
layout(std430, binding = 1) buffer GridHead { int head[]; };
layout(std430, binding = 2) buffer GridNext { int nxt[]; };

uniform int u_particleCount;
uniform float u_h;
uniform vec3 u_domainMin;
uniform vec3 u_domainMax;
uniform int u_gridNx;
uniform int u_gridNy;
uniform int u_gridNz;

int cellIndex(vec3 pos)
{
    vec3 rel = (pos - u_domainMin) / u_h;
    ivec3 c = clamp(ivec3(rel), ivec3(0), ivec3(u_gridNx - 1, u_gridNy - 1, u_gridNz - 1));
    return (c.z * u_gridNy + c.y) * u_gridNx + c.x;
}

void main()
{
    uint i = gl_GlobalInvocationID.x;
    if (i >= uint(u_particleCount)) return;
    if (p[i].posLife.w <= 0.0) { nxt[i] = -1; return; }

    int cell = cellIndex(p[i].pred.xyz);
    nxt[i] = atomicExchange(head[cell], int(i));
}
