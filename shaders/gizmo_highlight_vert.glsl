#version 330 core
layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aNormal;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;

out vec3 vWorldPos;
out vec3 vWorldNormal;

void main() {
    vec4 wp = model * vec4(aPos, 1.0);
    vWorldPos = wp.xyz;
    // Proper normal transform (assumes model non-uniform scale can exist)
    vWorldNormal = mat3(transpose(inverse(model))) * aNormal;
    gl_Position = projection * view * wp;
}
