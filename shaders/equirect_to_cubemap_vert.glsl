// File: equirect_to_cubemap_vert.glsl
#version 430 core
layout(location=0) in vec3 aPos;

out vec3 WorldDir; // This can be named anything, as long as the fragment shader matches

uniform mat4 projection;
uniform mat4 view;

void main() {
    // This is used by the fragment shaders that sample the source texture
    WorldDir = aPos; 

    // THE FIX: Multiply by the view matrix here!
    gl_Position = projection * view * vec4(aPos, 1.0);
}