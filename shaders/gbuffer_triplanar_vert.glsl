#version 450 core
// INPUTS: We only need position and normal from the mesh.
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;

// OUTPUTS: We only need to send the world-space position and normal.
out VS_OUT {
    vec3 WorldPos;
    vec3 WorldNormal;
} vs_out;

uniform mat4 projection;
uniform mat4 view;
uniform mat4 model;

void main()
{
    gl_Position        = projection * view * model * vec4(aPos, 1.0);
    vs_out.WorldPos    = vec3(model * vec4(aPos, 1.0));
    vs_out.WorldNormal = normalize(transpose(inverse(mat3(model))) * aNormal);
}