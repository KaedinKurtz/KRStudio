#version 430 core
// MLS-MPM thermal stage 2: convert the scattered (energy, thermalMass, k-weighted)
// into per-cell temperature T = energy/thermalMass, node conductivity
// k = kAccum/thermalMass, and the decoded node thermal mass C (J/K) used by the
// energy-conserving Fourier diffusion. Empty cells get a sentinel so diffusion
// treats the material boundary as insulated.
layout(local_size_x = 64) in;

layout(std430, binding = 3) coherent buffer GridTherm { int gt[]; }; // [energy, m*c_p, m*c_p*k] x cells
layout(std430, binding = 4) buffer TempA  { float ta[]; };           // node temperature
layout(std430, binding = 7) buffer GridC  { float gc[]; };           // node thermal mass C (J/K)
layout(std430, binding = 8) buffer GridK  { float gk[]; };           // node conductivity k (W/m.K)

uniform int u_N;
const float INV = 1.0 / 1.0e3;       // matches SCALE in mpm_heat_scatter
const float SENTINEL = -1.0e9;

void main()
{
    uint idx = gl_GlobalInvocationID.x;
    int cells = u_N * u_N * u_N;
    if (idx >= uint(cells)) return;
    float energy = float(gt[idx * 3 + 0]);   // encoded; scale cancels in ratios
    float cmass  = float(gt[idx * 3 + 1]);
    float kacc   = float(gt[idx * 3 + 2]);
    if (cmass * INV > 1.0e-9) {
        ta[idx] = energy / cmass;            // T = sum(w m c_p T)/sum(w m c_p) (scale cancels)
        gk[idx] = kacc   / cmass;            // k = sum(w m c_p k)/sum(w m c_p)
        gc[idx] = cmass * INV;               // decoded node thermal mass (J/K)
    } else {
        ta[idx] = SENTINEL; gk[idx] = 0.0; gc[idx] = 0.0;
    }
}
