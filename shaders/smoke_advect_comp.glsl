#version 430 core
// Semi-Lagrangian advection: trace the cell centre back along the velocity
// field and trilinearly sample the source field there. Dissipation thins
// the result over time (1.0 = conserved). Stable by construction; vorticity
// confinement (separate pass) re-injects the small-scale detail this
// dissipation removes.
layout(local_size_x = 8, local_size_y = 8, local_size_z = 8) in;

uniform sampler3D u_vel;   // advecting velocity field (m/s)
uniform sampler3D u_src;   // field being advected
layout(rgba16f, binding = 0) uniform image3D u_dst;

uniform ivec3 u_grid;
uniform vec3 u_origin;
uniform vec3 u_size;
uniform float u_dt;
uniform float u_dissipation; // multiply result (density/temp fade); 1.0 for velocity

vec3 worldToUVW(vec3 wp) { return (wp - u_origin) / u_size; }

void main()
{
    ivec3 c = ivec3(gl_GlobalInvocationID);
    if (any(greaterThanEqual(c, u_grid))) return;

    vec3 wp = u_origin + (vec3(c) + 0.5) / vec3(u_grid) * u_size;
    vec3 vel = texture(u_vel, worldToUVW(wp)).xyz;
    vec3 back = wp - vel * u_dt;
    vec4 result = texture(u_src, clamp(worldToUVW(back), 0.0, 1.0));
    imageStore(u_dst, c, result * u_dissipation);
}
