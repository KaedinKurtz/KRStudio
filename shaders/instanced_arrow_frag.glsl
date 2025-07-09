/*
================================================================================
|                           instanced_arrow_frag.glsl                          |
================================================================================
*/
#version 430 core

in  vec4 vs_color;     // colour interpolated from the vertex shader
out vec4 FragColor;

void main()
{
    FragColor = vs_color;
}
