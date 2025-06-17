#version 410 core
layout (location = 0) in vec3 aPos;

void main()
{
    // Pass position through to the geometry shader
    gl_Position = vec4(aPos, 1.0);
}