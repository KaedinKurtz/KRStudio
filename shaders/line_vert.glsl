/**
 * @file line_vert.glsl
 * @brief Vertex shader for drawing simple transformed lines.
 */
#version 410 core

layout (location = 0) in vec3 aPos;

// Corrected uniform names to match the C++ side
uniform mat4 u_view;
uniform mat4 u_proj;

void main()
{
    gl_Position = u_proj * u_view * vec4(aPos, 1.0);
}