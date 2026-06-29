#version 330 core
// Ghost validity robot (Phase 7): the robot's own mesh, drawn at the PRE-CLAMP (commanded) pose as a
// translucent tinted overlay. Same vertex layout as every scene mesh VAO (pos @0, normal @1).
layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aNormal;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;

out vec3 vWorldPos;
out vec3 vWorldNormal;

void main() {
    vec4 wp = model * vec4(aPos, 1.0);
    vWorldPos    = wp.xyz;
    vWorldNormal = mat3(transpose(inverse(model))) * aNormal;
    gl_Position  = projection * view * wp;
}
