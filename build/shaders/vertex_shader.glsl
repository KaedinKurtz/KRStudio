#version 330 core

layout (location = 0) in vec3 aPos;

// Transformation matrices sent from our C++ code
uniform mat4 model;      // Object's position/rotation/scale
uniform mat4 view;       // Camera's position/orientation
uniform mat4 projection; // The camera's perspective "lens"

void main()
{
    // Transform the vertex position from local space to screen space
    gl_Position = projection * view * model * vec4(aPos, 1.0);
}