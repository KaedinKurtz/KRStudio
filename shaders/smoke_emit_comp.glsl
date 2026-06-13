#version 430 core
// Inject emitters into the gas grid: splat density/temperature/fuel into a
// world-space sphere and add an upward jet velocity. In-place add (read +
// write the same cell, no cross-cell hazard).
layout(local_size_x = 8, local_size_y = 8, local_size_z = 8) in;

layout(rgba16f, binding = 0) uniform image3D u_scalars;  // r density, g temp, b fuel
layout(rgba16f, binding = 1) uniform image3D u_velocity; // xyz velocity

uniform ivec3 u_grid;
uniform vec3 u_origin;
uniform vec3 u_size;
uniform float u_dt;

const int MAX_EMIT = 8;
uniform int u_emitterCount;
uniform vec4 u_emitter[MAX_EMIT];   // xyz centre (world), w radius
uniform vec4 u_emitterP[MAX_EMIT];  // x densityRate, y temperature, z fuelRate, w jetSpeed

void main()
{
    ivec3 c = ivec3(gl_GlobalInvocationID);
    if (any(greaterThanEqual(c, u_grid))) return;

    vec3 wp = u_origin + (vec3(c) + 0.5) / vec3(u_grid) * u_size;

    vec4 scl = imageLoad(u_scalars, c);
    vec4 vel = imageLoad(u_velocity, c);

    for (int e = 0; e < u_emitterCount; ++e) {
        float r = u_emitter[e].w;
        float d = length(wp - u_emitter[e].xyz);
        if (d >= r) continue;
        float fall = smoothstep(r, 0.0, d);
        scl.r += u_emitterP[e].x * fall * u_dt;                  // density
        scl.g = max(scl.g, u_emitterP[e].y * fall);             // temperature (hold)
        scl.b += u_emitterP[e].z * fall * u_dt;                 // fuel
        vel.y += u_emitterP[e].w * fall * u_dt;                 // upward jet
    }

    imageStore(u_scalars, c, scl);
    imageStore(u_velocity, c, vel);
}
