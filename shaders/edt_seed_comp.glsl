#version 430 core
// Live-fluid SDF, stage 1 (Jump Flooding seed). One thread per particle: read the
// live particle SSBO (generic float-stride layout so it works for fluid AND MPM),
// and write the particle's WORLD position into the JFA seed grid at its cell. The
// grid is pre-cleared (seed.w = 0 = invalid) via glClearBufferData. Races between
// particles in the same cell are last-writer-wins, which is fine for JFA seeding
// (sub-cell accuracy; the flood corrects the field).
layout(local_size_x = 64) in;

layout(std430, binding = 0) readonly buffer Particles { float pdata[]; };
layout(std430, binding = 1) buffer Seeds { vec4 seed[]; };   // xyz = nearest particle pos, w = valid

uniform int  u_count;     // live particle count
uniform int  u_stride;    // floats per particle (fluid=12, MPM=48)
uniform int  u_posOff;    // float offset of pos.x within a particle
uniform int  u_aliveOff;  // float offset of the alive/life flag (>0 = live)
uniform int  u_N;
uniform vec3 u_origin;
uniform vec3 u_extent;

void main()
{
    uint i = gl_GlobalInvocationID.x;
    if (i >= uint(u_count)) return;
    int base = int(i) * u_stride;
    if (pdata[base + u_aliveOff] <= 0.0) return;                 // dead particle

    vec3 p = vec3(pdata[base + u_posOff], pdata[base + u_posOff + 1], pdata[base + u_posOff + 2]);
    vec3 uvw = (p - u_origin) / u_extent;                        // [0,1] within the grid
    ivec3 c = ivec3(floor(uvw * float(u_N)));
    if (any(lessThan(c, ivec3(0))) || any(greaterThanEqual(c, ivec3(u_N)))) return;  // outside grid

    int cell = (c.z * u_N + c.y) * u_N + c.x;
    seed[cell] = vec4(p, 1.0);                                   // last-writer-wins (OK for JFA)
}
