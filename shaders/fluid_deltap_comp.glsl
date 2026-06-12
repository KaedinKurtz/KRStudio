#version 430 core
// PBF stage 3b: position correction from lambdas + collision projection.
layout(local_size_x = 256) in;

struct Particle {
    vec4 posLife;
    vec4 vel;
    vec4 pred; // w = lambda (from stage 3a)
};
layout(std430, binding = 0) buffer Particles { Particle p[]; };
layout(std430, binding = 1) buffer GridHead { int head[]; };
layout(std430, binding = 2) buffer GridNext { int nxt[]; };

struct Box { vec4 center; vec4 halfExtents; vec4 rotation; };
struct Sph { vec4 centerRadius; };
layout(std140, binding = 3) uniform Colliders {
    Box boxes[32];
    Sph spheres[32];
    ivec4 counts; // x boxes, y spheres
};

// Fluid -> rigid reaction impulses, fixed point (1e7), 4 ints per slot:
// 32 box slots then 32 sphere slots. J = -m * dx_boundary / dt per pushout.
// (Flat int array: GLSL atomics don't work on vector components.)
layout(std430, binding = 5) buffer ImpulseBuf { int impulseRaw[]; };
uniform float u_dt;

uniform int u_particleCount;
uniform float u_h;
uniform float u_restDensity;
uniform float u_particleMass;
uniform float u_particleRadius;
uniform vec3 u_domainMin;
uniform vec3 u_domainMax;
uniform int u_gridNx;
uniform int u_gridNy;
uniform int u_gridNz;

// Signed distance field colliders (baked from meshes via OpenVDB)
uniform int u_sdfCount;
uniform sampler3D u_sdf[4];
uniform vec3 u_sdfMin[4];
uniform vec3 u_sdfMax[4];

const float IMP_SCALE = 1.0e7;
void accumulateImpulse(int slot, vec3 push)
{
    vec3 J = -u_particleMass * push / max(u_dt, 1e-5);
    atomicAdd(impulseRaw[slot * 4 + 0], int(J.x * IMP_SCALE));
    atomicAdd(impulseRaw[slot * 4 + 1], int(J.y * IMP_SCALE));
    atomicAdd(impulseRaw[slot * 4 + 2], int(J.z * IMP_SCALE));
}

const float PI = 3.14159265358979;

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

vec3 rotateByQuat(vec3 v, vec4 q)
{
    return v + 2.0 * cross(q.xyz, cross(q.xyz, v) + q.w * v);
}
vec3 rotateByQuatInv(vec3 v, vec4 q)
{
    vec4 qc = vec4(-q.xyz, q.w);
    return rotateByQuat(v, qc);
}

vec3 collide(vec3 pos, vec3 prevPos)
{
    float r = u_particleRadius;

    // Ground plane and domain walls
    pos.y = max(pos.y, r);
    pos.x = clamp(pos.x, u_domainMin.x + r, u_domainMax.x - r);
    pos.z = clamp(pos.z, u_domainMin.z + r, u_domainMax.z - r);
    pos.y = min(pos.y, u_domainMax.y - r);

    // Oriented boxes: push the particle out along the smallest-penetration
    // axis, toward the side it STARTED the step on. Using the current side
    // leaks through thin walls: once the pressure solve carries a particle
    // past the wall midplane it would be ejected out the far side.
    for (int b = 0; b < counts.x; ++b) {
        vec3 local = rotateByQuatInv(pos - boxes[b].center.xyz, boxes[b].rotation);
        vec3 he = boxes[b].halfExtents.xyz + vec3(r);
        vec3 d = he - abs(local);
        if (all(greaterThan(d, vec3(0.0)))) {
            vec3 prevLocal = rotateByQuatInv(prevPos - boxes[b].center.xyz, boxes[b].rotation);
            vec3 side = vec3(prevLocal.x >= 0.0 ? 1.0 : -1.0,
                             prevLocal.y >= 0.0 ? 1.0 : -1.0,
                             prevLocal.z >= 0.0 ? 1.0 : -1.0);
            if (d.x < d.y && d.x < d.z)      local.x = side.x * he.x;
            else if (d.y < d.z)              local.y = side.y * he.y;
            else                             local.z = side.z * he.z;
            vec3 newPos = boxes[b].center.xyz + rotateByQuat(local, boxes[b].rotation);
            accumulateImpulse(b, newPos - pos);
            pos = newPos;
        }
    }

    // Spheres
    for (int s = 0; s < counts.y; ++s) {
        vec3 d = pos - spheres[s].centerRadius.xyz;
        float minDist = spheres[s].centerRadius.w + r;
        float len = length(d);
        if (len < minDist && len > 1e-6) {
            vec3 newPos = spheres[s].centerRadius.xyz + d * (minDist / len);
            accumulateImpulse(32 + s, newPos - pos);
            pos = newPos;
        }
    }

    // SDF colliders: exact mesh shapes. Distance sampled in world units;
    // surface normal from central differences.
    for (int k = 0; k < u_sdfCount; ++k) {
        vec3 ext = u_sdfMax[k] - u_sdfMin[k];
        vec3 uvw = (pos - u_sdfMin[k]) / ext;
        if (any(lessThan(uvw, vec3(0.0))) || any(greaterThan(uvw, vec3(1.0)))) continue;
        float d = texture(u_sdf[k], uvw).r;
        if (d < r) {
            vec3 eps = vec3(1.0) / vec3(textureSize(u_sdf[k], 0));
            vec3 n = normalize(vec3(
                texture(u_sdf[k], uvw + vec3(eps.x, 0, 0)).r - texture(u_sdf[k], uvw - vec3(eps.x, 0, 0)).r,
                texture(u_sdf[k], uvw + vec3(0, eps.y, 0)).r - texture(u_sdf[k], uvw - vec3(0, eps.y, 0)).r,
                texture(u_sdf[k], uvw + vec3(0, 0, eps.z)).r - texture(u_sdf[k], uvw - vec3(0, 0, eps.z)).r) + vec3(1e-9));
            pos += n * (r - d);
        }
    }
    return pos;
}

void main()
{
    uint i = gl_GlobalInvocationID.x;
    if (i >= uint(u_particleCount)) return;
    if (p[i].posLife.w <= 0.0) return;

    vec3 pi = p[i].pred.xyz;
    float lambdaI = p[i].pred.w;
    ivec3 cc = cellOf(pi);

    // Anti-clustering term reference value. NOTE: with our gradient
    // normalization lambda is O(1e-4), so k must be scaled accordingly —
    // the paper's k=0.1 assumes unit-scale lambdas and detonates the fluid.
    float wDq = wPoly6(0.04 * u_h * u_h, u_h); // |dq| = 0.2 h
    const float K_CORR = 1.0e-5;

    vec3 dp = vec3(0.0);
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
                float scorr = -K_CORR * pow(wPoly6(dot(rij, rij), u_h) / max(wDq, 1e-12), 4.0);
                dp += (lambdaI + p[j].pred.w + scorr) * gradSpiky(rij, u_h);
            }
            j = nxt[j];
        }
    }
    dp *= u_particleMass / u_restDensity;
    // Trust region: a single iteration may not move a particle further than
    // a fraction of the kernel radius (kills any residual instability).
    float dpLen = length(dp);
    if (dpLen > 0.2 * u_h) dp *= (0.2 * u_h) / dpLen;

    // prevPos = position at the start of the step: the side-of-wall truth.
    vec3 corrected = collide(pi + dp, p[i].posLife.xyz);
    p[i].pred.xyz = corrected;
}
