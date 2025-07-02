#version 430 core

// The particle struct must match the one in the compute shader
struct Particle {
    vec4 position;
    vec4 velocity;
    vec4 color;
    float age;
    float lifetime;
    float size;
    float padding;
};

layout (location = 0) in vec4 in_position;
layout (location = 1) in vec4 in_color;
layout (location = 2) in float in_size;

uniform mat4 u_view;
uniform mat4 u_projection;

out vec4 fragColor;

void main()
{
    gl_Position = u_projection * u_view * in_position;
    gl_PointSize = in_size;
    fragColor = in_color;
}
