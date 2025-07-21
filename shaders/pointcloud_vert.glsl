#version 430 core

layout (location = 0) in vec3 aPos;
layout (location = 1) in vec2 aTexCoord; // ADDED: Input for texture coordinates

out vec2 TexCoord; // ADDED: Output to the fragment shader

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;

void main()
{
    gl_Position = projection * view * model * vec4(aPos, 1.0);
    TexCoord = aTexCoord; // Pass the texture coordinate through
    gl_PointSize = 2.0;
}
