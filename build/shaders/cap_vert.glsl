#version 430 core
layout (location = 0) in vec3 aPos;

void main()
{
    // Just pass the world-space position directly to the geometry shader
    gl_Position = vec4(aPos, 1.0);
}