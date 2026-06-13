#version 430 core
// MLS-MPM stage 2: grid update. Decode fixed-point momentum/mass -> velocity,
// apply gravity, enforce separating wall boundary conditions. One thread/cell.
layout(local_size_x = 64) in;

layout(std430, binding = 1) coherent buffer GridInt { int gi[]; };
layout(std430, binding = 2) buffer GridVel { vec4 gv[]; };

uniform int   u_N;
uniform float u_dt;
uniform vec3  u_gravity;
uniform int   u_bound;     // guard-cell band width for wall BC

const float INV_SCALE = 1.0 / 1.0e7;

void main()
{
    uint idx = gl_GlobalInvocationID.x;
    int cells = u_N * u_N * u_N;
    if (idx >= uint(cells)) return;

    float m = float(gi[idx * 4 + 3]) * INV_SCALE;
    vec3 v = vec3(0.0);
    if (m > 1e-10) {
        v = vec3(float(gi[idx * 4 + 0]), float(gi[idx * 4 + 1]), float(gi[idx * 4 + 2]))
            * INV_SCALE / m;
        v += u_dt * u_gravity;

        int cz = int(idx) / (u_N * u_N);
        int rem = int(idx) - cz * u_N * u_N;
        int cy = rem / u_N;
        int cx = rem - cy * u_N;
        // Separating BC: cancel only the velocity component pointing INTO a
        // wall (material slides along and can leave, but cannot penetrate).
        if (cx < u_bound        && v.x < 0.0) v.x = 0.0;
        if (cx >= u_N - u_bound && v.x > 0.0) v.x = 0.0;
        if (cy < u_bound        && v.y < 0.0) v.y = 0.0;
        if (cy >= u_N - u_bound && v.y > 0.0) v.y = 0.0;
        if (cz < u_bound        && v.z < 0.0) v.z = 0.0;
        if (cz >= u_N - u_bound && v.z > 0.0) v.z = 0.0;
    }
    gv[idx] = vec4(v, m);
}
