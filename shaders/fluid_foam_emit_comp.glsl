#version 430 core
// Whitewater emission — faithful Ihmsen et al. 2012 ("Unified Spray, Foam
// and Bubbles for Particle-Based Fluids"):
//   I_ta  trapped air     = converging relative velocity        (Eq. 2)
//   I_wc  wave crests     = surface-normal disagreement, gated   (Eqs. 4-7)
//   I_k   kinetic energy  = 1/2 m v^2 replaces any hard speed gate
//   n_d   = I_k * (k_ta*I_ta + k_wc*I_wc) * dt                   (Eq. 8)
// The ADDITIVE combination is what makes the foam slider feel continuous —
// a multiplicative gate is exactly what made the old version binary.
// Potentials are clamped by the linear ramp Phi against taus that auto-
// normalise to the running per-frame maximum (Bender 2019), so no per-scene
// tuning is needed.
layout(local_size_x = 256) in;

struct Particle {
    vec4 posLife;
    vec4 vel;
    vec4 pred;
};
struct Diffuse {
    vec4 posLife; // xyz position, w remaining life (<=0 dead)
    vec4 velType; // xyz velocity, w type (0 spray, 1 foam, 2 bubble)
};
layout(std430, binding = 0) buffer Particles { Particle p[]; };
layout(std430, binding = 1) buffer GridHead { int head[]; };
layout(std430, binding = 2) buffer GridNext { int nxt[]; };
layout(std430, binding = 3) buffer DiffuseBuf { Diffuse d[]; };
layout(std430, binding = 4) buffer DiffuseCounter { uint nextSlot; };
// Per-particle surface normals + neighbour count, written by finalize.
layout(std430, binding = 7) buffer NormalsBuf { vec4 normals[]; };
// Running per-frame maxima of the RAW potentials, fixed point x1000
// (ta, wc, ke). Zeroed by the CPU each frame, read back and EMA-smoothed
// into next frame's taus.
layout(std430, binding = 8) buffer FoamNorm { int rawMax[3]; };

uniform int u_particleCount;
uniform int u_maxDiffuse;
uniform float u_h;
uniform float u_particleMass;
uniform float u_particleRadius;
uniform vec3 u_domainMin;
uniform vec3 u_domainMax;
uniform int u_gridNx;
uniform int u_gridNy;
uniform int u_gridNz;
uniform float u_dt;
uniform float u_foaminess; // artist slider 0..1
uniform float u_foamScale; // global gain multiplier
uniform vec2 u_tauTa;      // (tau_min, tau_max) trapped air
uniform vec2 u_tauWc;      // wave crest
uniform vec2 u_tauKe;      // kinetic energy
uniform uint u_frame;

const float K_TA = 10.0;  // trapped-air emission rate (1/s at full potential)
const float K_WC = 25.0;  // wave-crest emission rate
const int   MAX_EMIT = 8; // per particle per step

ivec3 cellOf(vec3 pos)
{
    vec3 rel = (pos - u_domainMin) / u_h;
    return clamp(ivec3(rel), ivec3(0), ivec3(u_gridNx - 1, u_gridNy - 1, u_gridNz - 1));
}

// Ihmsen Eq. 1: linear clamp ramp, never a step.
float Phi(float I, vec2 tau)
{
    return clamp((min(I, tau.y) - min(I, tau.x)) / max(tau.y - tau.x, 1e-6), 0.0, 1.0);
}

// Cheap per-thread RNG (PCG-ish hash).
uint hash(uint x)
{
    x ^= x >> 16; x *= 0x7feb352du;
    x ^= x >> 15; x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}
float rand01(inout uint state)
{
    state = hash(state);
    return float(state) * (1.0 / 4294967296.0);
}

void main()
{
    uint i = gl_GlobalInvocationID.x;
    if (i >= uint(u_particleCount)) return;
    if (p[i].posLife.w <= 0.0) return;

    vec3 xi = p[i].posLife.xyz;
    vec3 vi = p[i].vel.xyz;
    float speed = length(vi);
    vec3 ni = normals[i].xyz;
    bool hasNormal = dot(ni, ni) > 0.25;

    // Raw potentials over the neighbourhood (weight 1 - r/h, unnormalised:
    // deliberately not an SPH kernel — robust at deficient free surfaces).
    float vDiff = 0.0;   // trapped air
    float kappa = 0.0;   // wave crest curvature
    int neighbors = 0;
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
            if (uint(j) != i && p[j].posLife.w > 0.0) {
                vec3 xij = xi - p[j].posLife.xyz;
                float r = length(xij);
                if (r < u_h && r > 1e-6) {
                    ++neighbors;
                    float w = 1.0 - r / u_h;
                    vec3 vij = vi - p[j].vel.xyz;
                    float vmag = length(vij);
                    if (vmag > 1e-6)
                        vDiff += vmag * (1.0 - dot(vij / vmag, xij / r)) * w;

                    // Crest (Eqs. 4-7): normal disagreement with CONVEX
                    // neighbours only (x_ji . n_i < 0), both normals valid.
                    vec3 nj = normals[j].xyz;
                    if (hasNormal && dot(nj, nj) > 0.25
                        && dot(-xij / r, ni) < 0.0)
                        kappa += (1.0 - dot(ni, nj)) * w;
                }
            }
            j = nxt[j];
        }
    }

    // Per-particle crest gate: moving along its own normal (kills false
    // positives at static sharp edges).
    float crestGate = 0.0;
    if (hasNormal && speed > 1e-5 && dot(vi / speed, ni) >= 0.6) crestGate = 1.0;
    kappa *= crestGate;

    float eKin = 0.5 * u_particleMass * speed * speed;

    // Publish raw maxima for the CPU-side auto-normalisation (Bender 2019).
    atomicMax(rawMax[0], int(vDiff * 1000.0));
    atomicMax(rawMax[1], int(kappa * 1000.0));
    atomicMax(rawMax[2], int(eKin * 1000.0));

    // Isolated splash particles don't emit (paper gate).
    if (neighbors < 8) return;

    float Ita = Phi(vDiff, u_tauTa);
    float Iwc = Phi(kappa, u_tauWc);
    float Ik  = Phi(eKin, u_tauKe);

    float expectedCount = u_foaminess * u_foamScale * Ik
                          * (K_TA * Ita + K_WC * Iwc) * u_dt;

    uint rng = hash(uint(i) * 9781u + u_frame * 6271u);
    int emitCount = int(expectedCount);
    if (rand01(rng) < fract(expectedCount)) ++emitCount;
    emitCount = min(emitCount, MAX_EMIT);
    if (emitCount == 0) return;

    // Spawn uniformly in a cylinder along the velocity (paper sampling).
    vec3 vHat = speed > 1e-5 ? vi / speed : vec3(0, 1, 0);
    vec3 e1 = normalize(abs(vHat.y) < 0.9 ? cross(vHat, vec3(0, 1, 0))
                                          : cross(vHat, vec3(1, 0, 0)));
    vec3 e2 = cross(vHat, e1);
    float rV = u_particleRadius;

    for (int k = 0; k < emitCount; ++k) {
        uint slot = atomicAdd(nextSlot, 1u) % uint(u_maxDiffuse);
        float r = rV * sqrt(rand01(rng));
        float theta = 6.2831853 * rand01(rng);
        float hgt = rand01(rng) * u_dt * speed;
        vec3 radial = r * cos(theta) * e1 + r * sin(theta) * e2;
        // High-energy events make longer-lived foam (2..5 s).
        float life = 2.0 + Ik * rand01(rng) * 3.0;
        d[slot].posLife = vec4(xi + radial + hgt * vHat, life);
        d[slot].velType = vec4(vi + radial / max(u_dt, 1e-4) * 0.05, 1.0);
    }
}
