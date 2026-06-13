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
// Color-field surface normals for the whitewater crest potential
// (Ihmsen 2012): xyz = outward normal (zero for interior particles),
// w = fluid neighbour count.
layout(std430, binding = 7) buffer NormalsBuf { vec4 normals[]; };

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
uniform float u_viscosity; // XSPH blend coefficient
uniform float u_cohesion;  // Akinci 2013 gamma (mapped from sigma N/m)

// Drain regions (FluidSinkComponent): a particle whose committed position
// lands inside any sink box is killed this step. Keeps tap->drain flows
// from ever filling the domain.
const int MAX_SINKS = 8;
uniform int u_sinkCount;
uniform vec3 u_sinkMin[MAX_SINKS];
uniform vec3 u_sinkMax[MAX_SINKS];

// Curl-noise turbulence: a divergence-free eddy field added to velocity so
// water swirls instead of damping flat. Cheap procedural alternative to a
// full SPH vorticity-confinement pass.
uniform float u_turbulence; // m/s of swirl (0 = off)
uniform float u_time;

float hash31(vec3 p)
{
    p = fract(p * 0.3183099 + 0.1);
    p *= 17.0;
    return fract(p.x * p.y * p.z * (p.x + p.y + p.z));
}
float vnoise3(vec3 x)
{
    vec3 i = floor(x), f = fract(x);
    f = f * f * (3.0 - 2.0 * f);
    float n000 = hash31(i + vec3(0, 0, 0)), n100 = hash31(i + vec3(1, 0, 0));
    float n010 = hash31(i + vec3(0, 1, 0)), n110 = hash31(i + vec3(1, 1, 0));
    float n001 = hash31(i + vec3(0, 0, 1)), n101 = hash31(i + vec3(1, 0, 1));
    float n011 = hash31(i + vec3(0, 1, 1)), n111 = hash31(i + vec3(1, 1, 1));
    return mix(mix(mix(n000, n100, f.x), mix(n010, n110, f.x), f.y),
               mix(mix(n001, n101, f.x), mix(n011, n111, f.x), f.y), f.z);
}
// Scalar noise potential -> divergence-free field via curl of (psi advanced
// along three offset axes).
vec3 curlNoise(vec3 p)
{
    const float e = 0.35;
    vec3 dx = vec3(e, 0, 0), dy = vec3(0, e, 0), dz = vec3(0, 0, e);
    vec3 px = vec3(vnoise3(p + vec3(31.4)), vnoise3(p + vec3(57.7)), vnoise3(p));
    vec3 p_x1 = vec3(vnoise3(p + dx + vec3(31.4)), vnoise3(p + dx + vec3(57.7)), vnoise3(p + dx));
    vec3 p_y1 = vec3(vnoise3(p + dy + vec3(31.4)), vnoise3(p + dy + vec3(57.7)), vnoise3(p + dy));
    vec3 p_z1 = vec3(vnoise3(p + dz + vec3(31.4)), vnoise3(p + dz + vec3(57.7)), vnoise3(p + dz));
    vec3 dpdx = (p_x1 - px) / e, dpdy = (p_y1 - px) / e, dpdz = (p_z1 - px) / e;
    return vec3(dpdy.z - dpdz.y, dpdz.x - dpdx.z, dpdx.y - dpdy.x);
}

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

    // XSPH viscosity + Akinci 2013 cohesion (surface tension) in one
    // neighbor walk. The cohesion spline is negative at close range, which
    // doubles as anti-clustering — droplets bead, surfaces pull flat.
    vec3 pi = p[i].pred.xyz;
    ivec3 cc = cellOf(pi);
    vec3 xsph = vec3(0.0);
    vec3 cohesion = vec3(0.0);
    vec3 gradC = vec3(0.0); // color-field gradient: outward at the surface
    int neighbors = 0;
    float kCoh = 32.0 / (PI * pow(u_h, 9.0));
    float h6_64 = pow(u_h, 6.0) / 64.0;
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
                float r2 = dot(rij, rij);
                xsph += (p[j].vel.xyz - newVel) * wPoly6(r2, u_h)
                        * (u_particleMass / u_restDensity);
                float r = sqrt(r2);
                if (r > 1e-6 && r < u_h) {
                    float t = (u_h - r) * (u_h - r) * (u_h - r) * r * r * r;
                    float spline = (2.0 * r > u_h) ? t : (2.0 * t - h6_64);
                    cohesion += -u_cohesion * u_particleMass * kCoh * spline * (rij / r);
                    gradC += (rij / r) * (1.0 - r / u_h);
                    ++neighbors;
                }
            }
            j = nxt[j];
        }
    }
    // Surface normal only where the neighbourhood is deficient (Ihmsen:
    // interior particles keep a zero normal so they never read as crests).
    vec3 nSurf = vec3(0.0);
    if (neighbors <= 20 && dot(gradC, gradC) > 1e-8) nSurf = normalize(gradC);
    normals[i] = vec4(nSurf, float(neighbors));
    newVel += u_viscosity * xsph + cohesion * u_dt;
    // Curl-noise eddies (animated, divergence-free) for turbulent swirl.
    // Low spatial frequency = large rolling eddies; dt-scaled as an
    // acceleration so it stays contained (and framerate-independent).
    if (u_turbulence > 0.0001) {
        vec3 turb = curlNoise(pi * 0.55 + vec3(0.0, u_time * 0.2, 0.0));
        newVel += turb * u_turbulence * u_dt * 6.0;
    }
    // Surface tension limits the stable step: clamp runaway accelerations.
    float speed = length(newVel);
    float maxSpeed = 0.5 * u_h / u_dt;
    if (speed > maxSpeed) newVel *= maxSpeed / speed;

    // Drain: kill particles that entered a sink region.
    vec3 fp = p[i].pred.xyz;
    for (int sIdx = 0; sIdx < u_sinkCount; ++sIdx) {
        if (all(greaterThanEqual(fp, u_sinkMin[sIdx])) &&
            all(lessThanEqual(fp, u_sinkMax[sIdx]))) {
            p[i].posLife = vec4(u_domainMin.x, -1000.0, u_domainMin.z, 0.0);
            p[i].vel = vec4(0.0);
            return;
        }
    }

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
