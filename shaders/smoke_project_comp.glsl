#version 430 core
// Make the velocity field divergence-free: subtract the pressure gradient,
// then enforce free-slip (zero normal velocity) on the domain faces.
layout(local_size_x = 8, local_size_y = 8, local_size_z = 8) in;

layout(rgba16f, binding = 0) uniform image3D u_velocity;
layout(r32f, binding = 1) uniform image3D u_pressure;

uniform ivec3 u_grid;

float P(ivec3 c) { return imageLoad(u_pressure, clamp(c, ivec3(0), u_grid - 1)).r; }

void main()
{
    ivec3 c = ivec3(gl_GlobalInvocationID);
    if (any(greaterThanEqual(c, u_grid))) return;

    vec3 v = imageLoad(u_velocity, c).xyz;
    v -= 0.5 * vec3(
        P(c + ivec3(1, 0, 0)) - P(c - ivec3(1, 0, 0)),
        P(c + ivec3(0, 1, 0)) - P(c - ivec3(0, 1, 0)),
        P(c + ivec3(0, 0, 1)) - P(c - ivec3(0, 0, 1)));

    // Free-slip walls: no flow through the domain boundary.
    if (c.x == 0 || c.x == u_grid.x - 1) v.x = 0.0;
    if (c.y == 0 || c.y == u_grid.y - 1) v.y = 0.0;
    if (c.z == 0 || c.z == u_grid.z - 1) v.z = 0.0;

    imageStore(u_velocity, c, vec4(v, 0.0));
}
