#version 430 core
// Vorticity (curl of the velocity field), grid-difference units.
layout(local_size_x = 8, local_size_y = 8, local_size_z = 8) in;

layout(rgba16f, binding = 0) uniform image3D u_velocity;
layout(rgba16f, binding = 1) uniform image3D u_curl;

uniform ivec3 u_grid;

vec3 vel(ivec3 c) { return imageLoad(u_velocity, clamp(c, ivec3(0), u_grid - 1)).xyz; }

void main()
{
    ivec3 c = ivec3(gl_GlobalInvocationID);
    if (any(greaterThanEqual(c, u_grid))) return;

    vec3 vxp = vel(c + ivec3(1, 0, 0)), vxm = vel(c - ivec3(1, 0, 0));
    vec3 vyp = vel(c + ivec3(0, 1, 0)), vym = vel(c - ivec3(0, 1, 0));
    vec3 vzp = vel(c + ivec3(0, 0, 1)), vzm = vel(c - ivec3(0, 0, 1));

    vec3 curl;
    curl.x = 0.5 * ((vyp.z - vym.z) - (vzp.y - vzm.y));
    curl.y = 0.5 * ((vzp.x - vzm.x) - (vxp.z - vxm.z));
    curl.z = 0.5 * ((vxp.y - vxm.y) - (vyp.x - vym.x));
    imageStore(u_curl, c, vec4(curl, 0.0));
}
