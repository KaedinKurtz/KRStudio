#version 430 core
// One Jacobi sweep of the pressure Poisson equation (Harris GPU-Gems form,
// dx=1): p = (sum of 6 neighbours - divergence) / 6. Ping-pong src->dst.
layout(local_size_x = 8, local_size_y = 8, local_size_z = 8) in;

layout(r32f, binding = 0) uniform image3D u_pressureSrc;
layout(r32f, binding = 1) uniform image3D u_pressureDst;
layout(r32f, binding = 2) uniform image3D u_divergence;

uniform ivec3 u_grid;

float P(ivec3 c) { return imageLoad(u_pressureSrc, clamp(c, ivec3(0), u_grid - 1)).r; }

void main()
{
    ivec3 c = ivec3(gl_GlobalInvocationID);
    if (any(greaterThanEqual(c, u_grid))) return;

    float div = imageLoad(u_divergence, c).r;
    float p = (P(c + ivec3(1, 0, 0)) + P(c - ivec3(1, 0, 0)) +
               P(c + ivec3(0, 1, 0)) + P(c - ivec3(0, 1, 0)) +
               P(c + ivec3(0, 0, 1)) + P(c - ivec3(0, 0, 1)) - div) / 6.0;
    imageStore(u_pressureDst, c, vec4(p));
}
