#version 430 core
// Fluid particle billboard: pulls positions straight from the solver SSBO.

struct Particle {
    vec4 posLife;
    vec4 vel;
    vec4 pred;
};
layout(std430, binding = 0) buffer Particles { Particle p[]; };

uniform mat4 u_view;
uniform mat4 u_projection;
uniform float u_particleRadius;
uniform float u_viewportHeight;

out float vSpeed;
out float vLife;

void main()
{
    vec4 posLife = p[gl_VertexID].posLife;
    vLife = posLife.w;
    vSpeed = length(p[gl_VertexID].vel.xyz);

    vec4 viewPos = u_view * vec4(posLife.xyz, 1.0);
    gl_Position = u_projection * viewPos;

    // Perspective-correct point size (pixels)
    float dist = max(0.1, -viewPos.z);
    gl_PointSize = clamp(u_particleRadius * 2.2 * u_viewportHeight *
                         u_projection[1][1] / dist, 1.0, 64.0);
}
