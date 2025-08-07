// File: prefilter_vert.glsl
#version 450 core
layout (location = 0) in vec3 aPos;

out vec3 localPos;

uniform mat4 projection;
uniform mat4 view;

void main()
{
    // The vertex position is passed through as the localPos. The fragment shader
    // will use this as the direction vector to sample the cubemap.
    localPos = aPos;

    // The final vertex position MUST be transformed by both the view and projection matrices.
    // This ensures we render the scene from 6 different perspectives.
    gl_Position = projection * view * vec4(aPos, 1.0);
}