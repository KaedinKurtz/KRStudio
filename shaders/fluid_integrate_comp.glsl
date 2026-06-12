#version 430 core
// PBF stage 1: apply external forces, predict positions.
layout(local_size_x = 256) in;

struct Particle {
    vec4 posLife; // xyz pos, w life
    vec4 vel;     // xyz velocity
    vec4 pred;    // xyz predicted pos, w lambda
};
layout(std430, binding = 0) buffer Particles { Particle p[]; };

uniform int u_particleCount;
uniform float u_dt;
uniform float u_h;
uniform vec3 u_gravity;

void main()
{
    uint i = gl_GlobalInvocationID.x;
    if (i >= uint(u_particleCount)) return;
    if (p[i].posLife.w <= 0.0) return; // inert

    vec3 v = p[i].vel.xyz + u_gravity * u_dt;

    // CFL clamp: never travel more than ~0.5 kernel radii per step, which
    // also prevents tunneling through thin collider walls.
    float vmax = 0.5 * u_h / u_dt;
    float speed = length(v);
    if (speed > vmax) v *= vmax / speed;

    p[i].vel.xyz = v;
    p[i].pred.xyz = p[i].posLife.xyz + v * u_dt;
}
