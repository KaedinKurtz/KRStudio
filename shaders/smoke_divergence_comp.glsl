#version 430 core
// Velocity divergence (grid-difference units), input to the pressure solve.
layout(local_size_x = 8, local_size_y = 8, local_size_z = 8) in;

layout(rgba16f, binding = 0) uniform image3D u_velocity;
layout(r32f, binding = 1) uniform image3D u_divergence;

uniform ivec3 u_grid;

vec3 vel(ivec3 c) { return imageLoad(u_velocity, clamp(c, ivec3(0), u_grid - 1)).xyz; }

void main()
{
    ivec3 c = ivec3(gl_GlobalInvocationID);
    if (any(greaterThanEqual(c, u_grid))) return;

    float div = 0.5 * (
        (vel(c + ivec3(1, 0, 0)).x - vel(c - ivec3(1, 0, 0)).x) +
        (vel(c + ivec3(0, 1, 0)).y - vel(c - ivec3(0, 1, 0)).y) +
        (vel(c + ivec3(0, 0, 1)).z - vel(c - ivec3(0, 0, 1)).z));
    imageStore(u_divergence, c, vec4(div));
}
