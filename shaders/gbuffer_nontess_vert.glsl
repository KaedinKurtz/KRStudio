#version 430 core

// — Inputs —
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec3 aTangent;
layout(location = 3) in vec3 aBitangent;
layout(location = 4) in vec2 aTexCoords;

// — Uniforms —
uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;

// — Outputs directly to Fragment —
out vec3 FragPos_TangentSpace;
out vec3 ViewPos_TangentSpace;
out mat3 TBN;
out vec2 TexCoords;

void main() {
    vec3 FragPos_world = vec3(model * vec4(aPos, 1.0));
    vec3 ViewPos_world = vec3(view  * model * vec4(aPos, 1.0));

    mat3 normalMat = transpose(inverse(mat3(model)));
    vec3 T = normalize(normalMat * aTangent);
    vec3 B = normalize(normalMat * aBitangent);
    vec3 N = normalize(normalMat * aNormal);
    mat3 tbn = mat3(T, B, N);

    FragPos_TangentSpace = transpose(tbn) * FragPos_world;
    ViewPos_TangentSpace = transpose(tbn) * ViewPos_world;
    TBN                   = tbn;
    TexCoords             = aTexCoords;

    gl_Position = projection * view * model * vec4(aPos, 1.0);
}
