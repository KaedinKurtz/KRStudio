#version 430 core
// PBF stage 4: derive velocity, apply XSPH viscosity, commit positions.
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
uniform float u_dt;
uniform float u_h;
uniform float u_particleMass;
uniform float u_restDensity;
uniform vec3 u_domainMin;
uniform vec3 u_domainMax;
uniform int u_gridNx;
uniform int u_gridNy;
uniform int u_gridNz;

const float PI = 3.14159265358979;
const float XSPH_C = 0.05;

float wPoly6(float r2, float h)
{
    float h2 = h * h;
    if (r2 >= h2) return 0.0;
    float t = h2 - r2;
    return 315.0 / (64.0 * PI * pow(h, 9.0)) * t * t * t;
}

ivec3 cellOf(vec3 pos)
{
    vec3 rel = (pos - u_domainMin) / u_h;
    return clamp(ivec3(rel), ivec3(0), ivec3(u_gridNx - 1, u_gridNy - 1, u_gridNz - 1));
}

void main()
{
    uint i = gl_GlobalInvocationID.x;
    if (i >= uint(u_particleCount)) return;
    if (p[i].posLife.w <= 0.0) return;

    vec3 newVel = (p[i].pred.xyz - p[i].posLife.xyz) / u_dt;

    // XSPH viscosity: blend toward neighborhood velocity for coherent motion
    vec3 pi = p[i].pred.xyz;
    ivec3 cc = cellOf(pi);
    vec3 xsph = vec3(0.0);
    for (int dz = -1; dz <= 1; ++dz)
    for (int dy = -1; dy <= 1; ++dy)
    for (int dx = -1; dx <= 1; ++dx) {
        ivec3 c = cc + ivec3(dx, dy, dz);
        if (any(lessThan(c, ivec3(0))) ||
            any(greaterThanEqual(c, ivec3(u_gridNx, u_gridNy, u_gridNz)))) continue;
        int j = head[(c.z * u_gridNy + c.y) * u_gridNx + c.x];
        while (j >= 0) {
            if (j != int(i)) {
                vec3 rij = pi - p[j].pred.xyz;
                xsph += (p[j].vel.xyz - newVel) * wPoly6(dot(rij, rij), u_h)
                        * (u_particleMass / u_restDensity);
            }
            j = nxt[j];
        }
    }
    newVel += XSPH_C * xsph;

    float life = p[i].posLife.w - u_dt;
    if (life <= 0.0) {
        // park dead particles far below the domain, inert
        p[i].posLife = vec4(u_domainMin.x, -1000.0, u_domainMin.z, 0.0);
        p[i].vel = vec4(0.0);
        return;
    }

    p[i].vel.xyz = newVel;
    p[i].posLife = vec4(p[i].pred.xyz, life);
}
