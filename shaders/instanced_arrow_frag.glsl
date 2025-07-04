
/*
================================================================================
|                           instanced_arrow_frag.glsl                          |
================================================================================
*/
#version 430 core

// Input from the vertex shader, matching the 'out' variable name and type.
in vec4 vs_color;

// The final output color of the fragment.
out vec4 FragColor;

void main()
{
    // Set the fragment's color to the interpolated color received from the vertex shader.
    FragColor = vs_color;
}
