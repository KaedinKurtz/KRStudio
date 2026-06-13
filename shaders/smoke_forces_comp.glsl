#version 430 core
// Adds vorticity-confinement (turbulent detail) + thermal buoyancy to the
// velocity field, in place.
layout(local_size_x = 8, local_size_y = 8, local_size_z = 8) in;

layout(rgba16f, binding = 0) uniform image3D u_velocity;
layout(rgba16f, binding = 1) uniform image3D u_curl;
layout(rgba16f, binding = 2) uniform image3D u_scalars; // r density, g temperature

uniform ivec3 u_grid;
uniform float u_dt;
uniform float u_vorticity;
uniform float u_buoyancy;
uniform float u_densityWeight;
uniform float u_ambient;

float curlMag(ivec3 c) { return length(imageLoad(u_curl, clamp(c, ivec3(0), u_grid - 1)).xyz); }

void main()
{
    ivec3 c = ivec3(gl_GlobalInvocationID);
    if (any(greaterThanEqual(c, u_grid))) return;

    vec3 v = imageLoad(u_velocity, c).xyz;

    // Vorticity confinement: push velocity toward higher vorticity, scaled
    // by the local curl, restoring detail lost to advection dissipation.
    vec3 omega = imageLoad(u_curl, c).xyz;
    vec3 gradEta = 0.5 * vec3(
        curlMag(c + ivec3(1, 0, 0)) - curlMag(c - ivec3(1, 0, 0)),
        curlMag(c + ivec3(0, 1, 0)) - curlMag(c - ivec3(0, 1, 0)),
        curlMag(c + ivec3(0, 0, 1)) - curlMag(c - ivec3(0, 0, 1)));
    vec3 N = gradEta / (length(gradEta) + 1e-5);
    v += u_vorticity * cross(N, omega) * u_dt;

    // Buoyancy: hot gas rises, soot mass sinks.
    vec4 scl = imageLoad(u_scalars, c);
    float lift = u_buoyancy * (scl.g - u_ambient) - u_densityWeight * scl.r;
    v.y += lift * u_dt;

    imageStore(u_velocity, c, vec4(v, 0.0));
}
