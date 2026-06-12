#version 430 core
// Whitewater emission (Ihmsen et al. 2012, "Unified Spray, Foam and
// Bubbles"): fluid particles generate diffuse particles where air gets
// trapped (high relative velocities between converging neighbours) and at
// wave crests (fast, sparsely covered surface particles).
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

uniform int u_particleCount;
uniform int u_maxDiffuse;
uniform float u_h;
uniform vec3 u_domainMin;
uniform vec3 u_domainMax;
uniform int u_gridNx;
uniform int u_gridNy;
uniform int u_gridNz;
uniform float u_dt;
uniform float u_foaminess; // 0..1 emission scale
uniform uint u_frame;

ivec3 cellOf(vec3 pos)
{
    vec3 rel = (pos - u_domainMin) / u_h;
    return clamp(ivec3(rel), ivec3(0), ivec3(u_gridNx - 1, u_gridNy - 1, u_gridNz - 1));
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
    if (speed < 0.5) return; // calm water makes no whitewater

    // Trapped-air potential: relative velocity against approaching
    // neighbours; crest proxy: few neighbours (near surface) while fast.
    float trappedAir = 0.0;
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
                            vec3 vij = vi - p[j].vel.xyz;
                            float vmag = length(vij);
                            if (vmag > 1e-6)
                                trappedAir += vmag * (1.0 - dot(vij / vmag, xij / r))
                                              * (1.0 - r / u_h);
                        }
                    }
                    j = nxt[j];
                }
            }

    const float kSurfaceNeighbors = 18.0;
    float crest = clamp(1.0 - float(neighbors) / kSurfaceNeighbors, 0.0, 1.0) * speed;
    float potential = 0.12 * trappedAir + 0.5 * crest;

    // Expected emissions this step.
    float expectedCount = u_foaminess * potential * u_dt * 12.0;
    uint rng = hash(uint(i) * 9781u + u_frame * 6271u);
    int emitCount = int(expectedCount);
    if (rand01(rng) < fract(expectedCount)) ++emitCount;
    emitCount = min(emitCount, 4);

    for (int k = 0; k < emitCount; ++k) {
        uint slot = atomicAdd(nextSlot, 1u) % uint(u_maxDiffuse);
        vec3 jitter = vec3(rand01(rng), rand01(rng), rand01(rng)) * 2.0 - 1.0;
        float life = 1.0 + 2.5 * rand01(rng);
        d[slot].posLife = vec4(xi + jitter * u_h * 0.3, life);
        d[slot].velType = vec4(vi + jitter * 0.4, 1.0);
    }
}
