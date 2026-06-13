#version 430 core
// MLS-MPM thermal stage 3: explicit heat diffusion on the grid. One Jacobi
// sweep of the discrete Laplacian over occupied neighbours. The coefficient
// u_diffuse = kappa*dt/dx^2 must be <= 1/6 for stability (3D explicit).
layout(local_size_x = 64) in;

layout(std430, binding = 4) buffer TempA { float ta[]; };
layout(std430, binding = 5) buffer TempB { float tb[]; };

uniform int u_N;
uniform float u_diffuse;
const float SENTINEL = -1.0e9;

void main()
{
    uint idx = gl_GlobalInvocationID.x;
    int cells = u_N * u_N * u_N;
    if (idx >= uint(cells)) return;
    float T = ta[idx];
    if (T < -1.0e8) { tb[idx] = SENTINEL; return; }

    int cz = int(idx) / (u_N * u_N);
    int rem = int(idx) - cz * u_N * u_N;
    int cy = rem / u_N;
    int cx = rem - cy * u_N;

    float acc = 0.0;
    ivec3 offs[6] = ivec3[6](ivec3(1,0,0), ivec3(-1,0,0), ivec3(0,1,0),
                             ivec3(0,-1,0), ivec3(0,0,1), ivec3(0,0,-1));
    for (int k = 0; k < 6; ++k) {
        ivec3 nc = ivec3(cx, cy, cz) + offs[k];
        if (any(lessThan(nc, ivec3(0))) || any(greaterThanEqual(nc, ivec3(u_N)))) continue;
        float Tn = ta[(nc.z * u_N + nc.y) * u_N + nc.x];
        if (Tn > -1.0e8) acc += (Tn - T);
    }
    tb[idx] = T + u_diffuse * acc;
}
