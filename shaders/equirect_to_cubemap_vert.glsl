#version 430 core
layout(location=0) in vec3 aPos;

out vec3 WorldDir;
uniform mat4 projection;
uniform mat4 view;

void main() {
    WorldDir = (view * vec4(aPos, 0.0)).xyz;
    gl_Position = projection * vec4(aPos, 1.0);
}
