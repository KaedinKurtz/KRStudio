#version 430 core
// MLS-MPM thermal stage 2: convert scattered (mass*T, mass) into a per-cell
// temperature. Empty cells get a sentinel so diffusion ignores them.
layout(local_size_x = 64) in;

layout(std430, binding = 3) coherent buffer GridTherm { int gt[]; };
layout(std430, binding = 4) buffer TempA { float ta[]; };

uniform int u_N;
const float INV = 1.0 / 1.0e5;
const float SENTINEL = -1.0e9;

void main()
{
    uint idx = gl_GlobalInvocationID.x;
    int cells = u_N * u_N * u_N;
    if (idx >= uint(cells)) return;
    float m = float(gt[idx * 2 + 1]) * INV;
    ta[idx] = (m > 1.0e-9) ? (float(gt[idx * 2 + 0]) * INV) / m : SENTINEL;
}
