#version 430 core

// — Vertex Attributes —
// Positions, normals, texture coords, tangent/bitangent for normal mapping
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aTexCoords;
layout(location = 3) in vec3 aTangent;
layout(location = 4) in vec3 aBitangent;

// — Outputs to Fragment —
// World?space position, normal, TBN basis, plus UVs
out vec3 FragPos;
out vec3 Normal;
out vec2 TexCoords;
out vec3 Tangent;
out vec3 Bitangent;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;



void main()
{
    // World?space position
    FragPos = vec3(model * vec4(aPos, 1.0));

    // Normal transformed by inverse?transpose
    Normal = mat3(transpose(inverse(model))) * aNormal;

    // Tangent & bitangent must also be transformed (no scaling assumed here; if you support scaled tangents, use the full inverse?transpose).
    Tangent   = mat3(model) * aTangent;
    Bitangent = mat3(model) * aBitangent;

    // Pass UVs through unchanged
    TexCoords = aTexCoords;

    // Compute clip?space position
    gl_Position = projection * view * vec4(FragPos, 1.0);
}
