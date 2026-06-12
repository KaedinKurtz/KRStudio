#version 430 core
// Whitewater advection + classification (Ihmsen et al. 2012): diffuse
// particles behave by where they live — SPRAY (airborne, ballistic),
// FOAM (on the surface, pinned to the local fluid velocity), BUBBLES
// (submerged: buoyancy k_b + drag k_d toward the fluid motion).
layout(local_size_x = 256) in;

struct Particle {
    vec4 posLife;
    vec4 vel;
    vec4 pred;
};
struct Diffuse {
    vec4 posLife;
    vec4 velType;
};
layout(std430, binding = 0) buffer Particles { Particle p[]; };
layout(std430, binding = 1) buffer GridHead { int head[]; };
layout(std430, binding = 2) buffer GridNext { int nxt[]; };
layout(std430, binding = 3) buffer DiffuseBuf { Diffuse d[]; };

uniform int u_maxDiffuse;
uniform float u_h;
uniform vec3 u_domainMin;
uniform vec3 u_domainMax;
uniform int u_gridNx;
uniform int u_gridNy;
uniform int u_gridNz;
uniform float u_dt;
uniform vec3 u_gravity;
uniform float u_foamDecay; // user lifetime control (>1 dies faster)

const float K_BUOY = 2.0; // bubble buoyancy (SPlisHSPlasH default)
const float K_DRAG = 0.8; // bubble drag toward fluid velocity

ivec3 cellOf(vec3 pos)
{
    vec3 rel = (pos - u_domainMin) / u_h;
    return clamp(ivec3(rel), ivec3(0), ivec3(u_gridNx - 1, u_gridNy - 1, u_gridNz - 1));
}

void main()
{
    uint i = gl_GlobalInvocationID.x;
    if (i >= uint(u_maxDiffuse)) return;
    if (d[i].posLife.w <= 0.0) return;

    vec3 xi = d[i].posLife.xyz;
    vec3 vi = d[i].velType.xyz;

    // Shepard-normalised fluid velocity at the diffuse position (cubic-ish
    // smooth weight; plain averaging let distant samples dominate).
    int neighbors = 0;
    vec3 fluidVel = vec3(0.0);
    float wsum = 0.0;
    ivec3 cc = cellOf(xi);
    for (int dz = -1; dz <= 1; ++dz)
        for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx) {
                ivec3 c = cc + ivec3(dx, dy, dz);
                if (any(lessThan(c, ivec3(0))) ||
                    any(greaterThanEqual(c, ivec3(u_gridNx, u_gridNy, u_gridNz))))
                    continue;
                int j = head[c.x + u_gridNx * (c.y + u_gridNy * c.z)];
                int guard = 0;
                while (j >= 0 && guard++ < 64) {
                    if (p[j].posLife.w > 0.0) {
                        vec3 rij = xi - p[j].posLife.xyz;
                        float q2 = dot(rij, rij) / (u_h * u_h);
                        if (q2 < 1.0) {
                            float t = 1.0 - q2;
                            float w = t * t * t;
                            ++neighbors;
                            wsum += w;
                            fluidVel += w * p[j].vel.xyz;
                        }
                    }
                    j = nxt[j];
                }
            }
    if (wsum > 1e-6) fluidVel /= wsum;

    // Ihmsen classification by fluid-neighbour count.
    float type;
    float lifeDecay;
    if (neighbors < 6) {
        type = 0.0; // SPRAY: ballistic
        vi += u_gravity * u_dt;
        lifeDecay = 0.6; // dies on its own, slower than foam
    }
    else if (neighbors <= 20) {
        type = 1.0; // FOAM: position pinned to the fluid surface motion
        vi = fluidVel;
        lifeDecay = 1.0; // only foam dissolves at the user rate
    }
    else {
        type = 2.0; // BUBBLE: buoyancy + drag (Euler-Cromer)
        vi += u_dt * (-K_BUOY * u_gravity) + K_DRAG * (fluidVel - vi);
        lifeDecay = 0.5; // mostly reclassifies when it surfaces
    }

    xi += vi * u_dt;
    if (xi.y < 0.005) {
        xi.y = 0.005;
        vi.y = abs(vi.y) * 0.2;
        lifeDecay *= 3.0; // grounded whitewater is scum: fade it fast
    }
    xi = clamp(xi, u_domainMin, u_domainMax);

    d[i].posLife = vec4(xi, d[i].posLife.w - lifeDecay * u_foamDecay * u_dt);
    d[i].velType = vec4(vi, type);
}
