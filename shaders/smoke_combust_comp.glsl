#version 430 core
// Scalar reactions: fuel burns into heat + soot (fire), temperature cools
// toward ambient, density dissipates. In place on the scalars volume.
layout(local_size_x = 8, local_size_y = 8, local_size_z = 8) in;

layout(rgba16f, binding = 0) uniform image3D u_scalars; // r density, g temp, b fuel

uniform ivec3 u_grid;
uniform float u_dt;
uniform float u_cooling;
uniform float u_densityDissipation;
uniform float u_burnRate;
uniform float u_ambient;

void main()
{
    ivec3 c = ivec3(gl_GlobalInvocationID);
    if (any(greaterThanEqual(c, u_grid))) return;

    vec4 s = imageLoad(u_scalars, c);

    // Combustion: consume fuel, release heat + soot.
    float burn = min(s.b, u_burnRate * u_dt);
    s.b -= burn;
    s.g += burn * 5.0;          // strong heat yield -> bright flame
    s.r += burn * 1.6;          // soot

    // Cooling toward ambient and density fade (exponential).
    s.g = u_ambient + (s.g - u_ambient) * exp(-u_cooling * u_dt);
    s.r *= exp(-u_densityDissipation * u_dt);

    s.r = max(s.r, 0.0);
    s.g = max(s.g, 0.0);
    s.b = max(s.b, 0.0);
    imageStore(u_scalars, c, s);
}
