#version 430 core

// Sphere/ellipsoid imposter sprites for the screen-space fluid pipeline.
// Attributeless: indexes the solver's particle SSBO by gl_VertexID.
// With u_aniso=1 each particle carries a fitted ellipsoid (fluid_aniso_comp):
// the fragment shader ray-traces it via vInvM (view -> unit-sphere space).

struct Particle {
    vec4 posLife;
    vec4 vel;
    vec4 pred;
};
struct Aniso {
    vec4 quat;   // world-space orientation
    vec4 radiiN; // radii as multiples of render radius; w = valid
    vec4 center; // smoothed splat centre
};

layout(std430, binding = 0) buffer Particles { Particle particles[]; };
layout(std430, binding = 6) buffer AnisoBuf { Aniso aniso[]; };

uniform mat4 u_view;
uniform mat4 u_projection;
uniform float u_particleRadius;
uniform float u_sizeScale;
uniform float u_viewportHeight;
uniform int u_aniso;

out vec3 vViewCenter;
out float vRadius;
out float vLife;
flat out mat3 vInvM; // unit-sphere transform (ellipsoid path only)

mat3 mat3FromQuat(vec4 q)
{
    float x = q.x, y = q.y, z = q.z, w = q.w;
    return mat3(1.0 - 2.0 * (y * y + z * z), 2.0 * (x * y + z * w),       2.0 * (x * z - y * w),
                2.0 * (x * y - z * w),       1.0 - 2.0 * (x * x + z * z), 2.0 * (y * z + x * w),
                2.0 * (x * z + y * w),       2.0 * (y * z - x * w),       1.0 - 2.0 * (x * x + y * y));
}

void main()
{
    Particle p = particles[gl_VertexID];
    vLife = p.posLife.w;
    vec3 worldPos = p.posLife.xyz;
    float baseR = u_particleRadius * u_sizeScale * 1.5; // overlap for a closed surface
    vRadius = baseR;
    vInvM = mat3(1.0);
    float maxR = baseR;

    if (u_aniso == 1) {
        Aniso a = aniso[gl_VertexID];
        if (a.radiiN.w > 0.5) {
            worldPos = a.center.xyz;
            vec3 radii = a.radiiN.xyz * baseR;
            maxR = max(radii.x, max(radii.y, radii.z));
            mat3 Rview = mat3(u_view) * mat3FromQuat(a.quat);
            // view point -> unit-sphere space: S^-1 * R^T
            vInvM = mat3(1.0 / radii.x, 0.0, 0.0,
                         0.0, 1.0 / radii.y, 0.0,
                         0.0, 0.0, 1.0 / radii.z) * transpose(Rview);
        }
    }

    vec4 viewPos = u_view * vec4(worldPos, 1.0);
    vViewCenter = viewPos.xyz;

    gl_Position = u_projection * viewPos;

    // Conservative projected footprint in pixels (margin covers the
    // off-centre projection of a stretched ellipsoid).
    float pixels = u_viewportHeight * u_projection[1][1] * maxR * 1.15 / max(0.001, -viewPos.z);
    gl_PointSize = clamp(pixels, 1.0, 256.0);

    // Cull dead particles and near-field strays: a single droplet brushing
    // the camera would otherwise become a screen-filling disc.
    if (vLife <= 0.0 || -viewPos.z < 0.25) gl_Position = vec4(0, 0, -10, 1);
}
