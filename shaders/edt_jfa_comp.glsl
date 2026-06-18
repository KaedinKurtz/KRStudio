#version 430 core
// Live-fluid SDF, stage 2 (Jump Flooding pass). One thread per grid cell. For the
// current step, look at the 26 neighbours at +/-step; among this cell's seed and
// the neighbours' seeds (the valid ones), keep the one CLOSEST to this cell's
// world centre. Dispatched log2(N) times with step = N/2, N/4, ..., 1 (ping-pong
// SeedsIn->SeedsOut). After the passes seed.xyz = the nearest live-particle world
// position for every cell -> distance = |cellPos - seed| - radius, gradient =
// normalize(cellPos - seed).
layout(local_size_x = 64) in;

layout(std430, binding = 1) readonly buffer SeedsIn  { vec4 seedIn[]; };
layout(std430, binding = 2) buffer SeedsOut { vec4 seedOut[]; };

uniform int  u_N;
uniform int  u_step;
uniform vec3 u_origin;
uniform vec3 u_extent;

vec3 cellPos(ivec3 c) { return u_origin + u_extent * ((vec3(c) + 0.5) / float(u_N)); }

void main()
{
    uint gid = gl_GlobalInvocationID.x;
    int cells = u_N * u_N * u_N;
    if (gid >= uint(cells)) return;

    int cz = int(gid) / (u_N * u_N);
    int rem = int(gid) - cz * u_N * u_N;
    int cy = rem / u_N;
    int cx = rem - cy * u_N;
    ivec3 c = ivec3(cx, cy, cz);
    vec3 wp = cellPos(c);

    vec4 best = seedIn[int(gid)];
    float bestD = (best.w > 0.5) ? distance(wp, best.xyz) : 1.0e30;

    for (int dz = -1; dz <= 1; ++dz)
        for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx) {
                if (dx == 0 && dy == 0 && dz == 0) continue;
                ivec3 nc = c + ivec3(dx, dy, dz) * u_step;
                if (any(lessThan(nc, ivec3(0))) || any(greaterThanEqual(nc, ivec3(u_N)))) continue;
                int ni = (nc.z * u_N + nc.y) * u_N + nc.x;
                vec4 ns = seedIn[ni];
                if (ns.w > 0.5) {
                    float d = distance(wp, ns.xyz);
                    if (d < bestD) { bestD = d; best = ns; }
                }
            }
    seedOut[int(gid)] = best;
}
