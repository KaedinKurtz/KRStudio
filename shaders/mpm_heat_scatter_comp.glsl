#version 430 core
// MLS-MPM thermal stage 1: scatter thermal ENERGY (m*c_p*T), thermal MASS (m*c_p)
// and the k-weighted conductivity (m*c_p*k) to the grid using the same quadratic
// B-spline weights as the mechanical P2G. Energy basis (not mean temperature) so
// grid Fourier conduction conserves energy. Runs once per frame.
layout(local_size_x = 64) in;

struct Particle {
    vec4 posMass; vec4 velVol; vec4 c0, c1, c2; vec4 f0, f1, f2;
    vec4 plastic; vec4 matl; vec4 color; vec4 therm2; // therm2.x = k (W/m.K)
};
layout(std430, binding = 0) buffer Particles { Particle p[]; };
layout(std430, binding = 3) coherent buffer GridTherm { int gt[]; };
layout(std430, binding = 6) coherent buffer HeatAccum { int hacc[]; }; // sum(m*c_p) per source

uniform int   u_count;
uniform int   u_N;
uniform vec3  u_origin;
uniform float u_invDx;

// Heat sources: accumulate the thermal mass of particles inside each radius so
// the gather can inject a mass-weighted, energy-conserving dT from Watts.
const int MAX_HEAT = 8;
uniform int   u_heatCount;
uniform vec4  u_heatSrc[MAX_HEAT];     // xyz world pos, w nominal temperature
uniform float u_heatRadius[MAX_HEAT];

// Fixed point: m*c_p*T magnitudes are ~1e4-1e5 per node; 1e3 keeps the summed
// int well under the +/-2e9 clamp while preserving sub-degree precision.
const float SCALE = 1.0e3;
int enc(float f) { return clamp(int(round(f * SCALE)), -2000000000, 2000000000); }

void main()
{
    uint i = gl_GlobalInvocationID.x;
    if (i >= uint(u_count)) return;
    if (p[i].color.w <= 0.0) return;
    float m  = p[i].posMass.w;
    float T  = p[i].plastic.y;
    float cp = p[i].plastic.z;          // heat capacity J/(kg.K)
    float k  = p[i].therm2.x;           // thermal conductivity W/(m.K)
    float cm = m * cp;                  // thermal mass m*c_p (J/K)

    // Tally this particle's thermal mass to every heat source whose sphere it is
    // inside (energy is later split by this so total injected = power*dt exactly).
    vec3 wpos = p[i].posMass.xyz;
    for (int h = 0; h < u_heatCount; ++h)
        if (distance(wpos, u_heatSrc[h].xyz) <= u_heatRadius[h])
            atomicAdd(hacc[h], enc(cm));

    vec3 Xp = (p[i].posMass.xyz - u_origin) * u_invDx;
    ivec3 base = ivec3(floor(Xp - 0.5));
    vec3 fx = Xp - vec3(base);
    vec3 w0 = 0.5 * (1.5 - fx) * (1.5 - fx);
    vec3 w1 = 0.75 - (fx - 1.0) * (fx - 1.0);
    vec3 w2 = 0.5 * (fx - 0.5) * (fx - 0.5);
    vec3 wx = vec3(w0.x, w1.x, w2.x);
    vec3 wy = vec3(w0.y, w1.y, w2.y);
    vec3 wz = vec3(w0.z, w1.z, w2.z);

    for (int a = 0; a < 3; ++a)
    for (int b = 0; b < 3; ++b)
    for (int c = 0; c < 3; ++c) {
        ivec3 node = base + ivec3(a, b, c);
        if (any(lessThan(node, ivec3(0))) || any(greaterThanEqual(node, ivec3(u_N)))) continue;
        float wt = wx[a] * wy[b] * wz[c];
        int cell = (node.z * u_N + node.y) * u_N + node.x;
        atomicAdd(gt[cell * 3 + 0], enc(wt * cm * T));   // thermal energy  m*c_p*T
        atomicAdd(gt[cell * 3 + 1], enc(wt * cm));       // thermal mass    m*c_p
        atomicAdd(gt[cell * 3 + 2], enc(wt * cm * k));   // k-weighted mass m*c_p*k
    }
}
