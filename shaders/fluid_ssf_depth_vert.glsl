#version 430 core

// Sphere-imposter sprites for the screen-space fluid pipeline.
// Attributeless: indexes the solver's particle SSBO by gl_VertexID.

struct Particle {
    vec4 posLife;
    vec4 vel;
    vec4 pred;
};

layout(std430, binding = 0) buffer Particles { Particle particles[]; };

uniform mat4 u_view;
uniform mat4 u_projection;
uniform float u_particleRadius;
uniform float u_sizeScale;
uniform float u_viewportHeight;

out vec3 vViewCenter;
out float vRadius;
out float vLife;

void main()
{
    Particle p = particles[gl_VertexID];
    vLife = p.posLife.w;
    vec4 viewPos = u_view * vec4(p.posLife.xyz, 1.0);
    vViewCenter = viewPos.xyz;
    vRadius = u_particleRadius * u_sizeScale * 1.5; // overlap for a closed surface

    gl_Position = u_projection * viewPos;

    // Exact projected sphere footprint in pixels.
    float pixels = u_viewportHeight * u_projection[1][1] * vRadius / max(0.001, -viewPos.z);
    gl_PointSize = clamp(pixels, 1.0, 256.0);

    // Cull dead particles and near-field strays: a single droplet brushing
    // the camera would otherwise become a screen-filling disc.
    if (vLife <= 0.0 || -viewPos.z < 0.25) gl_Position = vec4(0, 0, -10, 1);
}
