#version 430 core
// PBF stage 3a: per-particle density constraint multiplier (lambda).
layout(local_size_x = 256) in;

struct Particle {
    vec4 posLife;
    vec4 vel;
    vec4 pred; // w receives lambda
};
layout(std430, binding = 0) buffer Particles { Particle p[]; };
layout(std430, binding = 1) buffer GridHead { int head[]; };
layout(std430, binding = 2) buffer GridNext { int nxt[]; };

uniform int u_particleCount;
uniform float u_h;
uniform float u_restDensity;
uniform float u_particleMass;
uniform vec3 u_domainMin;
uniform vec3 u_domainMax;
uniform int u_gridNx;
uniform int u_gridNy;
uniform int u_gridNz;

const float PI = 3.14159265358979;
const float EPS_RELAX = 100.0;

float wPoly6(float r2, float h)
{
    float h2 = h * h;
    if (r2 >= h2) return 0.0;
    float t = h2 - r2;
    return 315.0 / (64.0 * PI * pow(h, 9.0)) * t * t * t;
}

vec3 gradSpiky(vec3 rij, float h)
{
    float r = length(rij);
    if (r <= 1e-6 || r >= h) return vec3(0.0);
    float t = h - r;
    return -45.0 / (PI * pow(h, 6.0)) * t * t * (rij / r);
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

    vec3 pi = p[i].pred.xyz;
    ivec3 cc = cellOf(pi);

    float density = 0.0;
    vec3 gradI = vec3(0.0);
    float sumGrad2 = 0.0;

    for (int dz = -1; dz <= 1; ++dz)
    for (int dy = -1; dy <= 1; ++dy)
    for (int dx = -1; dx <= 1; ++dx) {
        ivec3 c = cc + ivec3(dx, dy, dz);
        if (any(lessThan(c, ivec3(0))) ||
            any(greaterThanEqual(c, ivec3(u_gridNx, u_gridNy, u_gridNz)))) continue;
        int j = head[(c.z * u_gridNy + c.y) * u_gridNx + c.x];
        while (j >= 0) {
            vec3 rij = pi - p[j].pred.xyz;
            float r2 = dot(rij, rij);
            density += u_particleMass * wPoly6(r2, u_h);
            if (j != int(i)) {
                vec3 g = gradSpiky(rij, u_h) * (u_particleMass / u_restDensity);
                gradI += g;
                sumGrad2 += dot(g, g);
            }
            j = nxt[j];
        }
    }
    sumGrad2 += dot(gradI, gradI);

    float C = density / u_restDensity - 1.0;
    // Only correct compression, not expansion at free surfaces
    C = max(C, 0.0);
    p[i].pred.w = clamp(-C / (sumGrad2 + EPS_RELAX), -0.05, 0.0);
}
