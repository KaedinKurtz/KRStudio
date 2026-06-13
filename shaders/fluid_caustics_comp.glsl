#version 430 core
// Water caustics, particle-splat approximation of Wallace area-ratio:
// every SURFACE particle refracts the key light at its color-field normal
// and deposits intensity where the ray lands on the floor plane. Where the
// surface curves, neighbouring splats converge -> bright focal filaments;
// flat water deposits evenly -> no pattern. Cleared and rebuilt per frame.
layout(local_size_x = 256) in;

struct Particle {
    vec4 posLife;
    vec4 vel;
    vec4 pred;
};
layout(std430, binding = 0) buffer Particles { Particle p[]; };
layout(std430, binding = 7) buffer NormalsBuf { vec4 normals[]; };

layout(r32ui, binding = 0) uniform uimage2D u_caustics;

uniform int u_particleCount;
uniform vec2 u_worldMin;   // fluid domain XZ
uniform vec2 u_worldSize;
uniform vec3 u_lightDir;   // FROM the light (normalized, points down-ish)
uniform float u_floorY;

void main()
{
    uint i = gl_GlobalInvocationID.x;
    if (i >= uint(u_particleCount)) return;
    if (p[i].posLife.w <= 0.0) return;

    vec4 nd = normals[i];
    vec3 n = nd.xyz;
    // Surface particles only (interior normals are zeroed by finalize),
    // facing up enough to be the air-water interface.
    if (dot(n, n) < 0.25 || n.y < 0.35) return;

    vec3 refr = refract(u_lightDir, n, 1.0 / 1.333);
    if (refr.y >= -1e-3) return; // not heading down

    vec3 pos = p[i].posLife.xyz;
    float t = (u_floorY - pos.y) / refr.y;
    if (t <= 0.0 || t > 8.0) return;
    vec2 land = pos.xz + refr.xz * t;

    vec2 uv = (land - u_worldMin) / u_worldSize;
    if (any(lessThan(uv, vec2(0.0))) || any(greaterThanEqual(uv, vec2(1.0)))) return;
    ivec2 texel = ivec2(uv * vec2(imageSize(u_caustics)));
    imageAtomicAdd(u_caustics, texel, 64u);
}
