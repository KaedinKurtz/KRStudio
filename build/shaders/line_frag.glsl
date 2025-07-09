/**
 * @file line_frag.glsl
 * @brief Fragment shader for drawing simple colored lines.
 */
#version 430 core

out vec4 FragColor;

// Corrected uniform name and type to match the C++ side
uniform vec4 u_colour;

void main()
{
    FragColor = u_colour;
}